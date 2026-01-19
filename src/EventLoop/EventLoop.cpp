#include "EventLoop.h"
#include "Server/Session.h"
#include "Settings/Settings.h"
#include "Managers/SessionManagerWorker.h"

EventLoop::EventLoop() :
    _workerEpollFds({}),
    _workerWakeupFds({}),
    _running(false)
{
    // Empty
}

EventLoop::~EventLoop() {
    stop();
    INK_DEBUG << "EventLoop destroyed";
}

void EventLoop::start() {
    if (_running) return;
    _running = true;

    uint max_threads = Settings::getSettings().max_threads;
    _workerEpollFds.resize(max_threads);
    _workerWakeupFds.resize(max_threads);

    for (u32 i = 0; i < max_threads; i++) {
        // Initialize Linux epoll with a large size hint
        int epfd = epoll_create1(0);
        if (epfd < 0) throw std::runtime_error("Failed to create epoll");
        _workerEpollFds[i] = epfd;

        // Wakeup EventFD
        int wakefd = eventfd(0, EFD_NONBLOCK);
        if (wakefd < 0) throw std::runtime_error("Failed to create eventfd");
        _workerWakeupFds[i] = wakefd;

        _threads.emplace_back(&EventLoop::run, this, i, epfd, wakefd);

        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i, &cpuset);
        pthread_setaffinity_np(_threads.back().native_handle(), sizeof(cpu_set_t), &cpuset);
    }

    INK_DEBUG << "EventLoop started with " << max_threads << " threads";
}

void EventLoop::stop() {
    if (!_running) return;

    _running = false;

    // Use eventfd to wake up epoll
    for (auto& fd : _workerWakeupFds) {
        uint64_t one = 1;
        ::write(fd, &one, sizeof(one));
    }

    for (auto& t : _threads) {
        if (t.joinable()) t.join();
    }

    _threads.clear();

    // Close Epoll FDs
    for (int fd : _workerEpollFds) {
        close(fd);
    }
    _workerEpollFds.clear();

    // Close Wakeup FDs
    for (int fd : _workerWakeupFds) {
        close(fd);
    }
    _workerWakeupFds.clear();

    INK_DEBUG << "EventLoop stopped";
}

void EventLoop::addSession(std::shared_ptr<Session> session) {
    _nextThreadIdx = (_nextThreadIdx+1) % _workerEpollFds.size();
    socket_t epfd = _workerEpollFds[_nextThreadIdx];
    socket_t sockfd = session->getSocket();

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = sockfd;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) < 0)
    {
        INK_ERROR << "Epoll ADD failed (epfd:" << epfd << ", sockfd:" << sockfd << "): " << strerror(errno);
        return;
    }

    session->setAssignedEpollFd(epfd);
}

void EventLoop::updateSessionInterest(std::shared_ptr<Session> session, const SessionInterest iOinterest) {
    int epfd = session->getAssignedEpollFd();
    socket_t sockfd = session->getSocket();

    struct epoll_event ev;
    ev.data.fd = sockfd;
    ev.events = EPOLLET;

    switch (iOinterest) {
        case SessionInterest::ON_READ:
            ev.events |= EPOLLIN;
            break;
        case SessionInterest::ON_WRITE:
            ev.events |= EPOLLOUT;
            break;
    }

    epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);

    INK_TRACE << "Session interest updated (Linux): ioInterest=" << iOinterest;
}

void EventLoop::run(i32 threadIdx, socket_t epollfd, socket_t wakeupfd) {
    struct epoll_event events[MAX_EVENTS];

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = wakeupfd;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, wakeupfd, &ev);

    SessionManagerWorker* sessionWorker = SessionManagerWorker::getInstance();

    INK_TRACE << "Wakeupfd register: " <<  wakeupfd << " for thread: " << threadIdx;

    while (_running) {
        int nfds = epoll_wait(epollfd, events, MAX_EVENTS, EPOLL_WAIT_TIMEOUT);

        if (nfds < 0)
        {
            // Interrupted, just try again
            if (errno == EINTR)
                continue;

            INK_ERROR << "epoll_wait error: " << strerror(errno);
            break;
        }

        for (int i = 0; i < nfds; i++)
        {
            if (events[i].data.fd == wakeupfd)
            {
                uint64_t value;
                read(wakeupfd, &value, sizeof(value));
                INK_TRACE << "Wakeup event thread: " << threadIdx;
                continue;
            }

            std::shared_ptr<Session> session = sessionWorker->getSession(events[i].data.fd);

            if (!session) {
                // Stale event or closed session
                epoll_ctl(epollfd, EPOLL_CTL_DEL, events[i].data.fd, nullptr);
                continue;
            }

            u32 evs = events[i].events;

            // Error or hang-up
            if (evs & (EPOLLERR | EPOLLHUP))
            {
                session->close();
                continue;
            }

            // Read interest EPOLLIN
            if (evs & EPOLLIN)
            {
                session->onReadReady();
            }

            // Write interest EPOLLOUT
            if (evs & EPOLLOUT && session->getSocket() != SOCKET_ERROR_VALUE)
            {
                session->onWriteReady();
            }
        }
    }
}
