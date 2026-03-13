#include "EventLoop.h"

#include <ink/TimerWheel.h>

#include "Server/Session.h"
#include "Settings/Settings.h"

static inline char listener_marker;
#define LISTENER_TAG ((u64)&listener_marker)

EventLoop::EventLoop() :
    _running(false)
{
}

EventLoop::~EventLoop()
{
    stop();
}

void EventLoop::start()
{
    if (_running) return;
    _running = true;

    uint max_threads = Settings::getSettings().max_threads;

    // Spawn Worker Threads
    for (u32 i = 0; i < max_threads; i++) {
        _threads.emplace_back(&EventLoop::runWorker, this, i);

        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i % std::thread::hardware_concurrency(), &cpuset);
        pthread_setaffinity_np(_threads.back().native_handle(), sizeof(cpu_set_t), &cpuset);
    }

    INK_INFO << "EventLoop started with " << max_threads << " independent listeners.";
}

void EventLoop::stop()
{
    if (!_running) return;
    _running = false;

    for (auto& t : _threads) {
        if (t.joinable()) t.join();
    }
    _threads.clear();
    INK_DEBUG << "EventLoop stopped";
}

void EventLoop::runWorker(i32 threadIdx)
{
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0)
    {
        INK_ERROR << "Thread " << threadIdx << " failed to create socket";
        return;
    }

    int opt = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    // Allow multiple threads to listen on same port
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    // Set TCP_NODELAY to disable Nagle's algorithm
    setsockopt(listenFd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    // Set non-blocking mode for the server socket
    int flags = fcntl(listenFd, F_GETFL, 0);
    fcntl(listenFd, F_SETFL, flags | O_NONBLOCK);

    auto& settings = Settings::getSettings();

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(settings.port);

    if (bind(listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        INK_ERROR << "Thread " << threadIdx << " bind failed: " << strerror(errno);
        close(listenFd);
        return;
    }

    if (listen(listenFd, settings.backlog_size) < 0)
    {
        INK_ERROR << "Thread " << threadIdx << " listen failed";
        close(listenFd);
        return;
    }

    io_uring_params io_params = {};
    io_params.sq_entries = 4096;
    io_params.cq_entries = 8192;
    io_params.flags = IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
    io_params.sq_thread_idle = 3000;

    io_uring ring = {};

    io_uring_queue_init_params(io_params.sq_entries, &ring, &io_params);

    u32 required_features = IORING_FEAT_FAST_POLL | IORING_FEAT_SINGLE_MMAP | IORING_FEAT_NODROP;
    INK_ASSERT_MSG((io_params.features & required_features) == required_features, "Params flags were not setted.");

    // TimerWheel that marks n seconds until session expires
    // handling keep alives sessions
    ink::TimerWheel timerWheel(60);

    // ObjectPool to reduce session allocation
    ObjectPool<Session, SESSION_POOL_SIZE> sessionPool;

    // void* poolBase = sessionPool.getRawBuffer();
    // size_t poolSize = sessionPool.getRawBufferSize();

    // iovec iov;
    // iov.iov_base = poolBase;
    // iov.iov_len  = poolSize;

    // int ret = io_uring_register_buffers(&ring, &iov, 1);
    // INK_ASSERT_MSG(ret < 0, std::string("Buffer registration failed: ") + strerror(-ret));

    // Using one session table per thread
    // Using Vector for O(1) access instead of Map
    // std::vector<Session*> sessionTable;
    // sessionTable.resize(SESSION_POOL_SIZE);

    auto releaseSession = [&](Session* s) {
        if (!s || s->state != SessionState::Active) return;

        s->state = SessionState::Closing;
        s->close();             // Closing the FD triggers -ECANCELED for pending io_uring requests
        timerWheel.unlink(s);
    };

    auto tryFreeSession = [&](Session* s) {
        if (s->state == SessionState::Closing && s->pendingRequests <= 0) {
            INK_DEBUG << "[Final] Freeing session " << s;
            s->~Session();
            sessionPool.release(s);
        }
    };

    INK_INFO << "Thread " << threadIdx << " listening on port " << settings.port;

    io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    INK_ASSERT_MSG(sqe, "Sqe is null");

    io_uring_prep_accept(sqe, listenFd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);

    // keeps the request alive in the kernel
    sqe->ioprio |= IORING_ACCEPT_MULTISHOT;

    // identifier of the submition of the listener
    io_uring_sqe_set_data64(sqe, LISTENER_TAG);

    // submit the ring and notifies the kernel thread
    io_uring_submit(&ring);

    // Kernel timespec format to pass on io_uring_wait_cqe_timeout;
    __kernel_timespec kts = {};

    while (_running)
    {
        io_uring_cqe* cqe;
        u64 timeout = timerWheel.timeToNextTickMillis();

        kts.tv_sec  = timeout / 1000;
        kts.tv_nsec = (timeout % 1000) * 1000000;

        int ret = io_uring_wait_cqe_timeout(&ring, &cqe, &kts);

        u32 head;
        u32 count = 0;

        // Check if we actually have CQEs to process
        if (ret == 0 || ret == -ETIME) {
            io_uring_for_each_cqe(&ring, head, cqe)
            {
                count++;
                u64 tag = (u64)io_uring_cqe_get_data(cqe);

                if (tag == LISTENER_TAG)
                {
                    if (cqe->res >= 0)
                    {
                        Session* session = sessionPool.acquire();
                        new (session) Session(cqe->res);

                        INK_DEBUG << "[Conn] New Session: " << session << " FD: " << cqe->res;

                        timerWheel.update(session);

                        io_uring_sqe* rsqe = io_uring_get_sqe(&ring);
                        if (rsqe) {
                            session->onReadReady(rsqe);
                        }
                        else
                        {
                            releaseSession(session);
                            tryFreeSession(session);
                        }
                    }
                }
                else
                {
                    IoRequest* io_req = reinterpret_cast<IoRequest*>(tag);
                    Session* s = io_req->session;

                    if (!(cqe->flags & IORING_CQE_F_MORE)) {
                        s->pendingRequests--;
                    }

                    bool is_notif = (cqe->flags & IORING_CQE_F_NOTIF) != 0;

                    INK_DEBUG << "[IO] Completion for " << s << " Res: " << cqe->res
                              << " Pending: " << s->pendingRequests << " Notif: " << is_notif;

                    if (s->state == SessionState::Active && (cqe->res > 0 || is_notif))
                    {
                        if (io_req->optype == OperationType::Read)
                            s->processRead(cqe->res, &ring);
                        else
                            s->processWrite(cqe->res, &ring, is_notif);

                        timerWheel.update(s);
                    }
                    else
                    {
                        // If res <= 0, the kernel is done with this specific request
                        if (s->state == SessionState::Active) {
                            INK_DEBUG << "[IO] Closing session " << s << " due to res: " << cqe->res;
                            releaseSession(s);
                        }
                    }

                    tryFreeSession(s);
                }
            }
        }

        if (count > 0) io_uring_cq_advance(&ring, count);

        timerWheel.processExpired([&](ink::TimerNode* n) {
            Session* s = static_cast<Session*>(n);
            INK_DEBUG << "[Timer] Session timed out: " << s;
            releaseSession(s);
            tryFreeSession(s);
        });

        io_uring_submit(&ring);
    }

    io_uring_queue_exit(&ring);
    close(listenFd);
}
