#include "EventLoop.h"

#include <ink/TimerWheel.h>

#include "Server/Session.h"
#include "Settings/Settings.h"

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

    // Create Epoll for this thread
    int epfd = epoll_create1(0);

    // Add Listener to Epoll
    void* LISTENER_KEY = &listenFd; // dummy ptr for listener

    struct epoll_event ev;
    ev.data.ptr = LISTENER_KEY;
    ev.events = EPOLLIN | EPOLLET;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenFd, &ev);

    ink::TimerWheel timerWheel(60);

    // Using one session table per thread if lazy routine
    // Using Vector for O(1) access instead of Map
    ObjectPool<Session, 1024> sessionPool;

    auto releaseSession = [&](Session* s) {
        if (!s) return;

        timerWheel.unlink(s);

        epoll_ctl(epfd, EPOLL_CTL_DEL, s->getSocket(), nullptr);

        s->close();
        sessionPool.release(s);
    };

    struct epoll_event events[MAX_EVENTS];

    INK_DEBUG << "Thread " << threadIdx << " listening on port " << settings.port;

    while (_running)
    {
        int timeout = timerWheel.timeToNextTickMillis();
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, timeout);

        for (int i = 0; i < nfds; ++i)
        {
            void* ptr = events[i].data.ptr;
            uint32_t evs = events[i].events;

            if (ptr == LISTENER_KEY)
            {
                while (true)
                {
                    int clientSock = accept4(
                        listenFd, nullptr, nullptr,
                        SOCK_NONBLOCK | SOCK_CLOEXEC
                    );

                    if (clientSock < 0) break;

                    Session* session = sessionPool.acquire();
                    new (session) Session(clientSock, epfd);

                    epoll_event ev{};
                    ev.data.ptr = session;
                    ev.events = EPOLLIN | EPOLLET;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, clientSock, &ev);
                }
                continue;
            }
            else
            {
                Session* session = static_cast<Session*>(ptr);

                // null session we continue
                if (!session) continue;

                bool activity = false;
                // read
                if (evs & (EPOLLIN | EPOLLRDHUP))
                    activity |= session->onReadReady();

                // write
                if (session->getSocket() != SOCKET_ERROR_VALUE && (evs & EPOLLOUT))
                    activity |= session->onWriteReady();

                // We DO NOT close on RDHUP here, because we might still be sending the response (Half-Close).
                if (evs & (EPOLLERR | EPOLLHUP)) {
                    releaseSession(session);
                    continue;
                }

                if (activity)
                    timerWheel.update(session);
            }
        }

        if (timerWheel.timeToNextTickMillis() == 0)
        {
            timerWheel.processExpired([&](ink::TimerNode* n) {
                Session* s = static_cast<Session*>(n);
                releaseSession(s);
            });
        }
    }

    close(listenFd);
    close(epfd);
}
