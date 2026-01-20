#include "EventLoop.h"

#include "Server/Session.h"
#include "Settings/Settings.h"

EventLoop::EventLoop() :
    _running(false)
{
}

EventLoop::~EventLoop() {
    stop();
}

void EventLoop::start() {
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

void EventLoop::stop() {
    if (!_running) return;
    _running = false;

    for (auto& t : _threads) {
        if (t.joinable()) t.join();
    }
    _threads.clear();
    INK_DEBUG << "EventLoop stopped";
}

void EventLoop::runWorker(i32 threadIdx) {
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

    if (bind(listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        INK_ERROR << "Thread " << threadIdx << " bind failed: " << strerror(errno);
        close(listenFd);
        return;
    }

    if (listen(listenFd, settings.backlog_size) < 0) {
        INK_ERROR << "Thread " << threadIdx << " listen failed";
        close(listenFd);
        return;
    }

    // Create Epoll for this thread
    int epfd = epoll_create1(0);

    // Add Listener to Epoll
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listenFd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenFd, &ev);

    // Using one session table per thread if lazy routine
    SessionTable sessions;
    sessions.reserve(MAX_EVENTS);

    struct epoll_event events[MAX_EVENTS];

    INK_DEBUG << "Thread " << threadIdx << " listening on port " << settings.port;

    while (_running)
    {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, EPOLL_WAIT_TIMEOUT);

        for (int i = 0; i < nfds; ++i)
        {
            int fd = events[i].data.fd;
            uint32_t evs = events[i].events;

            if (fd == listenFd)
            {
                while (true)
                {
                    sockaddr_in clientAddr;
                    socklen_t len = sizeof(clientAddr);
                    int clientSock = accept4(
                        listenFd,
                        (sockaddr*)&clientAddr,
                        &len,
                        SOCK_NONBLOCK | SOCK_CLOEXEC
                    );

                    if (clientSock < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        break;
                    }

                    auto session = std::make_shared<Session>(clientSock, epfd);

                    // Add to Map
                    sessions[clientSock] = session;

                    // Add to Epoll
                    struct epoll_event clientEv;
                    clientEv.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
                    clientEv.data.fd = clientSock;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, clientSock, &clientEv);
                }
                continue;
            }

            auto it = sessions.find(fd);
            if (it != sessions.end())
            {
                auto& session = it->second;

                if (evs & (EPOLLERR | EPOLLHUP))
                {
                    session->close();
                }
                else
                {
                    if (evs & EPOLLIN)
                    {
                        session->onReadReady();
                    }

                    if (session->getSocket() != SOCKET_ERROR_VALUE && (evs & EPOLLOUT))
                    {
                        session->onWriteReady();
                    }
                }
            }
        }

        // // Lazy cleanupclosed or idle sessions
        // for (auto it = sessions.begin(); it != sessions.end(); )
        // {
        //     if (it->second->getSocket() == SOCKET_ERROR_VALUE || it->second->isIdle(std::chrono::milliseconds(settings.connection_timeout_ms)))
        //     {
        //         epoll_ctl(epfd, EPOLL_CTL_DEL, it->first, nullptr);

        //         it = sessions.erase(it);
        //         INK_DEBUG << "WHAT";
        //     }
        //     else
        //     {
        //         ++it;
        //     }
        // }
    }

    close(listenFd);
    close(epfd);
}
