#include "EventLoop.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <linux/filter.h>
#include <ink/ObjectPool.h>
#include <ink/TimerWheel.h>
#include <ink/utils.h>

#include "Server/Session.h"
#include "Settings/Settings.h"

#ifdef USE_IOURING
static inline char listener_marker;
#define LISTENER_TAG ((u64)&listener_marker)

// Tolerate liburing headers older than the kernel we may be running on: an
// unknown setup flag is simply rejected by io_uring_setup, which the ladder
// below already treats as "try the next rung".
#ifndef IORING_SETUP_COOP_TASKRUN
#define IORING_SETUP_COOP_TASKRUN  (1U << 8)
#endif
#ifndef IORING_SETUP_SINGLE_ISSUER
#define IORING_SETUP_SINGLE_ISSUER (1U << 12)
#endif
#ifndef IORING_SETUP_DEFER_TASKRUN
#define IORING_SETUP_DEFER_TASKRUN (1U << 13)
#endif
#endif

namespace {

/**
 * @brief Session slot keyed by file descriptor.
 *
 * The generation counter closes a nasty race: a descriptor released early in an
 * epoll batch can be handed straight back out by accept() later in that same
 * batch, so a stale event still sitting in the array would be applied to a
 * brand new connection. Stamping the generation into epoll_event.data lets the
 * loop recognise and drop those leftovers.
 */
struct SessionSlot {
    Session* session = nullptr;
    u32 generation = 0;
};

/** Best-effort per-connection tuning; failures here are never fatal. */
void tuneClientSocket(socket_t fd)
{
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

bool isFatalAcceptError(int err)
{
    // Running out of descriptors is not something a retry loop can fix; spinning
    // on it burns a core. Back off and let the next event wake us instead.
    return err == EMFILE || err == ENFILE || err == ENOBUFS || err == ENOMEM;
}

int readSysfsInt(const std::string& path, int fallback)
{
    FILE* f = std::fopen(path.c_str(), "re");
    if (!f) return fallback;

    int v = fallback;
    if (std::fscanf(f, "%d", &v) != 1)
        v = fallback;
    std::fclose(f);
    return v;
}

/**
 * @brief Orders logical CPUs so that consecutive workers land on *distinct
 *        physical cores* before any SMT sibling is reused.
 *
 * Pinning worker i to logical CPU i is wrong on both SMT and hybrid parts. On
 * this machine's topology, for example:
 *
 *   cpu:core  0:0 1:0 2:1 3:1 ... 14:7 15:7   <- 8 P-cores, 2 threads each
 *             16:8 17:9 ... 23:15             <- 8 E-cores, 1 thread each
 *
 * the naive mapping puts 12 workers on CPUs 0-11, i.e. two workers fighting
 * over the execution units of each of just six physical cores, while twelve
 * other CPUs sit idle. Grouping by (package, core_id) and emitting one sibling
 * per core first turns that into twelve genuinely independent cores.
 *
 * Falls back to identity ordering if sysfs topology is unreadable.
 */
std::vector<u32> buildCpuOrder()
{
    const u32 n = std::max(1u, std::thread::hardware_concurrency());

    // std::map keeps cores in a stable, sorted order so the assignment is
    // deterministic run to run.
    std::map<std::pair<int, int>, std::vector<u32>> cores;

    for (u32 cpu = 0; cpu < n; ++cpu)
    {
        const std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/";
        const int pkg  = readSysfsInt(base + "physical_package_id", 0);
        const int core = readSysfsInt(base + "core_id", static_cast<int>(cpu));
        cores[{pkg, core}].push_back(cpu);
    }

    std::vector<u32> order;
    order.reserve(n);

    // Breadth-first across cores: every core contributes its 1st thread, then
    // every core its 2nd, and so on.
    for (usize depth = 0;; ++depth)
    {
        bool progressed = false;
        for (const auto& [key, siblings] : cores)
        {
            if (depth < siblings.size())
            {
                order.push_back(siblings[depth]);
                progressed = true;
            }
        }
        if (!progressed)
            break;
    }

    if (order.size() != n) // topology unreadable or inconsistent
    {
        order.clear();
        for (u32 i = 0; i < n; ++i)
            order.push_back(i);
    }

    return order;
}

} // namespace

EventLoop::EventLoop() :
    _running(false)
{
}

EventLoop::~EventLoop()
{
    stop();
}

bool EventLoop::setupListeners(u32 count)
{
    const auto& settings = Settings::getSettings();

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(settings.port);

    if (::inet_pton(AF_INET, settings.ip.c_str(), &addr.sin_addr) != 1)
    {
        INK_ERROR << "Invalid bind address: " << settings.ip;
        return false;
    }

    _listenFds.reserve(count);

    for (u32 i = 0; i < count; ++i)
    {
        const socket_t fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0)
        {
            INK_ERROR << "socket() failed: " << strerror(errno);
            closeListeners();
            return false;
        }

        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        // Every worker gets its own listening socket on the same port, so the
        // kernel does the accept load balancing and no lock is ever contended.
        if (::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) < 0)
        {
            INK_ERROR << "SO_REUSEPORT failed: " << strerror(errno);
            ::close(fd);
            closeListeners();
            return false;
        }

        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            INK_ERROR << "bind(" << settings.ip << ':' << settings.port
                      << ") failed: " << strerror(errno);
            ::close(fd);
            closeListeners();
            return false;
        }

        if (::listen(fd, settings.backlog_size) < 0)
        {
            INK_ERROR << "listen() failed: " << strerror(errno);
            ::close(fd);
            closeListeners();
            return false;
        }

        _listenFds.push_back(fd);
    }

    return true;
}

void EventLoop::attachReuseportSteering()
{
    if (!Settings::getSettings().reuseport_cbpf || _listenFds.empty())
        return;

    // Steer each incoming connection to the listener whose worker runs on the
    // CPU that took the interrupt. Without it the kernel hashes on the 4-tuple,
    // which is fine for many connections but skews badly for few.
    struct sock_filter code[] = {
        { BPF_LD  | BPF_W   | BPF_ABS, 0, 0,
          static_cast<u32>(SKF_AD_OFF + SKF_AD_CPU) },
        { BPF_ALU | BPF_MOD | BPF_K,   0, 0, static_cast<u32>(_listenFds.size()) },
        { BPF_RET | BPF_A,             0, 0, 0 },
    };

    struct sock_fprog prog{};
    prog.len = sizeof(code) / sizeof(code[0]);
    prog.filter = code;

    // The program lives on the reuseport group, so attaching it to any member
    // covers all of them - but only once every member has joined.
    if (::setsockopt(_listenFds.front(), SOL_SOCKET, SO_ATTACH_REUSEPORT_CBPF,
                     &prog, sizeof(prog)) < 0)
    {
        INK_WARN << "SO_ATTACH_REUSEPORT_CBPF unavailable (" << strerror(errno)
                 << "); falling back to kernel hash distribution.";
    }
    else
    {
        INK_INFO << "SO_REUSEPORT CPU steering attached across "
                 << _listenFds.size() << " listeners.";
    }
}

void EventLoop::closeListeners()
{
    for (socket_t fd : _listenFds)
    {
        if (fd >= 0) ::close(fd);
    }
    _listenFds.clear();
}

void EventLoop::start()
{
    if (_running.load(std::memory_order_relaxed)) return;

    const auto& settings = Settings::getSettings();
    const u32 max_threads = settings.max_threads;

    if (!setupListeners(max_threads))
        throw std::runtime_error("Failed to create listening sockets");

    attachReuseportSteering();

    _running.store(true, std::memory_order_release);

    const std::vector<u32> cpuOrder = buildCpuOrder();

    _threads.reserve(max_threads);
    for (u32 i = 0; i < max_threads; i++)
    {
        _threads.emplace_back(&EventLoop::runWorker, this, static_cast<i32>(i), _listenFds[i]);

        if (settings.cpu_affinity)
        {
            const u32 cpu = cpuOrder[i % cpuOrder.size()];
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(cpu, &cpuset);
            pthread_setaffinity_np(_threads.back().native_handle(), sizeof(cpu_set_t), &cpuset);
        }
    }

    if (settings.cpu_affinity)
    {
        std::string plan;
        for (u32 i = 0; i < max_threads; ++i)
            plan += std::to_string(cpuOrder[i % cpuOrder.size()]) + (i + 1 < max_threads ? "," : "");
        INK_INFO << "Worker CPU pinning (one per physical core first): " << plan;
    }

    INK_INFO << "EventLoop started with " << max_threads << " independent listeners on "
             << settings.ip << ':' << settings.port;
}

void EventLoop::stop()
{
    if (!_running.load(std::memory_order_relaxed)) return;
    _running.store(false, std::memory_order_release);

    for (auto& t : _threads) {
        if (t.joinable()) t.join();
    }
    _threads.clear();

    closeListeners();
    INK_DEBUG << "EventLoop stopped";
}

void EventLoop::runWorker(i32 threadIdx, socket_t listenFd)
{
    const auto& settings = Settings::getSettings();

    // Keep-alive reaper. One wheel per thread, so no session is ever visible to
    // two threads and the whole structure stays lock free.
    const u32 ticksToLive = static_cast<u32>(
        std::max<usize>(1, settings.connection_timeout_ms / TIMERWHELL_TICK_INTERVAL));
    ink::TimerWheel timerWheel(ticksToLive, TIMERWHELL_TICK_INTERVAL);

    // ObjectPool to reduce session allocation
    auto sessionPool = std::make_unique<ink::ObjectPool<Session, SESSION_POOL_SIZE>>();

#ifdef USE_EPOLL
    // One session table per thread, indexed by fd for O(1) lookup.
    std::vector<SessionSlot> sessionTable(4096);

    const int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0)
    {
        INK_ERROR << "Thread " << threadIdx << " epoll_create1 failed: " << strerror(errno);
        return;
    }

    {
        epoll_event ev{};
        ev.data.u64 = static_cast<u64>(listenFd);
        ev.events = EPOLLIN | EPOLLET;
        epoll_ctl(epfd, EPOLL_CTL_ADD, listenFd, &ev);
    }

    auto slotFor = [&](socket_t fd) -> SessionSlot& {
        if (static_cast<usize>(fd) >= sessionTable.size())
            sessionTable.resize(static_cast<usize>(fd) * 2 + 1);
        return sessionTable[static_cast<usize>(fd)];
    };

    auto releaseSession = [&](Session* s) {
        if (!s) return;

        const socket_t fd = s->getSocket();

        timerWheel.unlink(s);

        if (fd != SOCKET_ERROR_VALUE)
        {
            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
            if (static_cast<usize>(fd) < sessionTable.size())
                sessionTable[static_cast<usize>(fd)].session = nullptr;
        }

        // ObjectPool::release() runs the destructor itself; calling ~Session()
        // here as well would destroy the object twice and double-free its
        // buffers the moment the slot got reused.
        sessionPool->release(s);
    };

    std::vector<epoll_event> events(MAX_EVENTS);

    while (_running.load(std::memory_order_relaxed))
    {
        u64 currentLoopTime = ink::utils::nowMillis();
        const int timeout = static_cast<int>(timerWheel.timeToNextTickMillis(currentLoopTime));

        const int nfds = epoll_wait(epfd, events.data(), MAX_EVENTS, timeout);
        if (nfds < 0)
        {
            if (errno == EINTR) continue;
            INK_ERROR << "Thread " << threadIdx << " epoll_wait failed: " << strerror(errno);
            break;
        }

        currentLoopTime = ink::utils::nowMillis();

        for (int i = 0; i < nfds; ++i)
        {
            const u64 tag = events[i].data.u64;
            const socket_t fd = static_cast<socket_t>(tag & 0xFFFFFFFFull);
            const u32 generation = static_cast<u32>(tag >> 32);
            const u32 evs = events[i].events;

            if (fd == listenFd)
            {
                for (;;)
                {
                    const int clientSock = accept4(
                        listenFd, nullptr, nullptr,
                        SOCK_NONBLOCK | SOCK_CLOEXEC);

                    if (clientSock < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        if (errno == EINTR || errno == ECONNABORTED)
                            continue;
                        if (isFatalAcceptError(errno))
                        {
                            INK_WARN << "accept4 backing off: " << strerror(errno);
                            break;
                        }
                        break;
                    }

                    tuneClientSocket(clientSock);

                    SessionSlot& slot = slotFor(clientSock);
                    if (slot.session)
                    {
                        // Should be impossible; if it happens the old entry is
                        // stale and holding it would leak the session.
                        releaseSession(slot.session);
                    }

                    Session* session = sessionPool->acquire(clientSock, epfd);
                    slot.session = session;
                    slot.generation++;

                    // Arm the keep-alive timer immediately. A connection that
                    // opens and then goes silent used to never enter the wheel,
                    // so it held its descriptor forever.
                    session->lastActivityTick = currentLoopTime;
                    timerWheel.update(session);

                    epoll_event ev{};
                    ev.data.u64 = (static_cast<u64>(slot.generation) << 32) |
                                   static_cast<u32>(clientSock);
                    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, clientSock, &ev);
                }
                continue;
            }

            if (static_cast<usize>(fd) >= sessionTable.size())
                continue;

            SessionSlot& slot = sessionTable[static_cast<usize>(fd)];
            Session* session = slot.session;

            // Stale event for a descriptor that has already been recycled.
            if (!session || slot.generation != generation)
                continue;

            bool alive = true;

            if (evs & (EPOLLIN | EPOLLRDHUP))
                alive = session->onReadReady();

            if (alive && (evs & EPOLLOUT))
                alive = session->onWriteReady();

            if (!alive || (evs & (EPOLLERR | EPOLLHUP)))
            {
                releaseSession(session);
                continue;
            }

            if (currentLoopTime - session->lastActivityTick >= TIMERWHELL_TICK_INTERVAL)
            {
                timerWheel.update(session);
                session->lastActivityTick = currentLoopTime;
            }
        }

        while (timerWheel.timeToNextTickMillis(currentLoopTime) == 0)
        {
            timerWheel.processExpired([&](ink::TimerNode* n) {
                releaseSession(static_cast<Session*>(n));
            });
        }
    }

    // Drain every live session so the pool can be destroyed cleanly.
    for (auto& slot : sessionTable)
    {
        if (slot.session)
            releaseSession(slot.session);
    }

    close(epfd);
#endif

#ifdef USE_IOURING
    // Ring memory is mlock()'d and accounted against RLIMIT_MEMLOCK, which on a
    // constrained host (containers default to 8MB) a large ring times many
    // worker threads can exceed. Try progressively smaller/plainer setups
    // rather than losing the shard: a worker that gives up here leaves its
    // listening socket bound and in the SO_REUSEPORT group with nobody to
    // accept() on it, silently blackholing however many connections the
    // kernel happens to hash its way.
    //
    // Setup mode ladder, best first. Each worker owns its ring outright and is
    // the only thread that ever touches it, which is exactly the contract the
    // modern low-overhead flags want:
    //
    //   SINGLE_ISSUER  - promises only one task ever submits, letting the
    //                    kernel drop internal locking on the submission path.
    //   DEFER_TASKRUN  - stops completion work from being run in random task
    //                    context via IPIs; it is batched and executed when we
    //                    come back into io_uring_enter(). Requires
    //                    SINGLE_ISSUER, and is the single biggest win for a
    //                    network server that always reaps via wait_cqe.
    //   COOP_TASKRUN   - the weaker predecessor of the above: no inter-processor
    //                    interrupt to force completion processing.
    //   SQPOLL         - a kernel thread polls the submission queue so the hot
    //                    path issues no syscall at all. Needs CAP_SYS_NICE and
    //                    enough RLIMIT_MEMLOCK, so it is tried but never
    //                    required.
    //
    // DEFER_TASKRUN and SQPOLL are mutually exclusive in intent (the former
    // batches work into our enter() calls, the latter removes them), so they
    // are offered as separate rungs rather than combined.
    //
    struct RingAttempt { u32 sq; u32 cq; u32 extraFlags; bool sqpoll; const char* name; };
    const RingAttempt attempts[] = {
        { 4096, 16384, IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN, false, "DEFER_TASKRUN|SINGLE_ISSUER" },
        { 4096, 16384, IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_COOP_TASKRUN,  false, "COOP_TASKRUN|SINGLE_ISSUER"  },
        { 4096, 16384, IORING_SETUP_COOP_TASKRUN,                               false, "COOP_TASKRUN"                },
        { 4096, 16384, 0,                                                       true,  "SQPOLL"                      },
        { 4096, 16384, 0,                                                       false, "plain"                       },
        { 1024, 4096,  0,                                                       false, "plain(small)"                },
        { 256,  1024,  0,                                                       false, "plain(tiny)"                 },
    };

    io_uring ring = {};
    io_uring_params io_params = {};
    int ring_res = -1;
    const char* ringMode = "none";

    for (const auto& a : attempts)
    {
        io_params = {};
        io_params.sq_entries = a.sq;
        io_params.cq_entries = a.cq;
        io_params.flags = IORING_SETUP_CQSIZE | a.extraFlags;

        if (a.sqpoll)
        {
            io_params.flags |= IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
            io_params.sq_thread_idle = TIMERWHELL_TICK_INTERVAL;
            // Without SQ_AFF's companion field every worker's kernel poller
            // lands on CPU 0 and they all fight over it.
            io_params.sq_thread_cpu = static_cast<u32>(threadIdx) %
                                      std::max(1u, std::thread::hardware_concurrency());
        }

        ring_res = io_uring_queue_init_params(io_params.sq_entries, &ring, &io_params);
        if (ring_res >= 0)
        {
            ringMode = a.name;
            break;
        }

        INK_DEBUG << "Thread " << threadIdx << " io_uring setup (" << a.name
                  << ", sq=" << a.sq << ") unavailable: " << strerror(-ring_res);
    }

    if (ring_res < 0)
    {
        INK_ERROR << "Thread " << threadIdx << " could not create an io_uring instance; "
                 << "closing its listener so the SO_REUSEPORT group excludes this shard.";
        close(listenFd);
        return;
    }

    if (threadIdx == 0)
    {
        INK_INFO << "io_uring ring mode: " << ringMode;
        if ((io_params.features & IORING_FEAT_FAST_POLL) == 0)
            INK_WARN << "Kernel lacks IORING_FEAT_FAST_POLL; throughput will suffer.";
    }

    auto getSqeSafe = [&](io_uring* r) -> io_uring_sqe* {
        io_uring_sqe* s = io_uring_get_sqe(r);
        if (!s) {
            // SQ ring full: flush it to the kernel to free up space.
            io_uring_submit(r);
            s = io_uring_get_sqe(r);
        }
        return s;
    };

    auto armAccept = [&]() -> bool {
        io_uring_sqe* sqe = getSqeSafe(&ring);
        if (!sqe) return false;

        io_uring_prep_multishot_accept(sqe, listenFd, nullptr, nullptr,
                                       SOCK_NONBLOCK | SOCK_CLOEXEC);
        io_uring_sqe_set_data64(sqe, LISTENER_TAG);
        return true;
    };

    auto freeSession = [&](Session* s) {
        s->setStatus(SessionStatus::Closed);
        timerWheel.unlink(s);
        // release() destroys the object; an explicit ~Session() here would be
        // a second destruction of the same storage.
        sessionPool->release(s);
    };

    if (!armAccept())
    {
        INK_ERROR << "Thread " << threadIdx << " could not arm the accept queue";
        close(listenFd);
        io_uring_queue_exit(&ring);
        return;
    }

    io_uring_submit(&ring);

    __kernel_timespec kts = {};

    while (_running.load(std::memory_order_relaxed))
    {
        io_uring_cqe* cqe = nullptr;
        u64 currentLoopTime = ink::utils::nowMillis();
        const u64 timeout = timerWheel.timeToNextTickMillis(currentLoopTime);

        kts.tv_sec  = static_cast<i64>(timeout / 1000);
        kts.tv_nsec = static_cast<i64>((timeout % 1000) * 1000000);

        const int ret = io_uring_wait_cqe_timeout(&ring, &cqe, &kts);
        if (ret < 0 && ret != -ETIME && ret != -EINTR)
        {
            INK_ERROR << "Thread " << threadIdx << " wait_cqe failed: " << strerror(-ret);
            break;
        }

        currentLoopTime = ink::utils::nowMillis();

        u32 head = 0;
        u32 count = 0;

        io_uring_for_each_cqe(&ring, head, cqe)
        {
            count++;
            const u64 tag = reinterpret_cast<u64>(io_uring_cqe_get_data(cqe));

            if (tag == LISTENER_TAG)
            {
                if (cqe->res >= 0)
                {
                    tuneClientSocket(cqe->res);

                    Session* s = sessionPool->acquire(cqe->res);
                    s->lastActivityTick = currentLoopTime;
                    timerWheel.update(s);

                    io_uring_sqe* rsqe = getSqeSafe(&ring);
                    if (rsqe)
                    {
                        s->onReadReady(rsqe);
                    }
                    else
                    {
                        INK_WARN << "[Conn] Dropping connection, SQ is full!";
                        s->close();
                        freeSession(s);
                    }
                }
                else if (cqe->res != -EAGAIN && cqe->res != -ECONNABORTED)
                {
                    INK_ERROR << "Multishot accept failed: " << strerror(-cqe->res);
                }

                if (!(cqe->flags & IORING_CQE_F_MORE))
                    armAccept();

                continue;
            }

            if (tag == 0)
                continue; // nop submitted by a session that had nothing to do

            IoRequest* io_req = reinterpret_cast<IoRequest*>(tag);
            Session* s = io_req->session;

            const bool is_notif = (cqe->flags & IORING_CQE_F_NOTIF) != 0;
            const bool has_more = (cqe->flags & IORING_CQE_F_MORE) != 0;
            const i32 res = cqe->res;

            // Clear the in-flight bit for *this* completion first, and always.
            // Doing it only on the active path meant a closing session's flags
            // never dropped to zero, so it was never reclaimed and leaked both
            // its slot and its descriptor.
            if (io_req->optype == OperationType::Read)
            {
                s->updateIoState(IO_READING, false);
            }
            else if (is_notif)
            {
                s->updateIoState(IO_WAITING_ZC, false);
            }
            else
            {
                s->updateIoState(IO_WRITING, false);
                if (has_more)
                    s->updateIoState(IO_WAITING_ZC, true);
            }

            if (s->getStatus() != SessionStatus::Active)
            {
                if (!s->hasPendingIo())
                {
                    s->close();
                    freeSession(s);
                }
                continue;
            }

            bool alive;
            if (io_req->optype == OperationType::Read)
                alive = s->processRead(res, &ring);
            else
                alive = s->processWrite(res, is_notif, &ring);

            if (!alive)
            {
                s->setStatus(SessionStatus::Closing);
                s->shutdown();

                if (!s->hasPendingIo())
                {
                    s->close();
                    freeSession(s);
                }
                continue;
            }

            if (currentLoopTime - s->lastActivityTick >= TIMERWHELL_TICK_INTERVAL)
            {
                timerWheel.update(s);
                s->lastActivityTick = currentLoopTime;
            }
        }

        if (count > 0) io_uring_cq_advance(&ring, count);

        while (timerWheel.timeToNextTickMillis(currentLoopTime) == 0)
        {
            timerWheel.processExpired([&](ink::TimerNode* n) {
                Session* s = static_cast<Session*>(n);

                s->setStatus(SessionStatus::Closing);
                s->close(); // cancels anything still queued in the kernel

                if (!s->hasPendingIo())
                    freeSession(s);
            });
        }

        io_uring_submit(&ring);
    }

    io_uring_queue_exit(&ring);
#endif

    INK_DEBUG << "Worker " << threadIdx << " stopped";
}
