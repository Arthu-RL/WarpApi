#include "EventLoop.h"
#include "Server/Session.h"
#include "Settings/Settings.h"

ink_u16 EventLoop::_currentThread = 0;

EventLoop::EventLoop() :
    _running(false),
    _workerEpollFd({}),
    _workerWakeupFd({})
{
    INK_DEBUG << "EventLoop created";
}

EventLoop::~EventLoop() {
    stop();

#ifdef _WIN32
    if (_completionPort != NULL) {
        CloseHandle(_completionPort);
    }
#else
    for (auto it = _workerEpollFd.begin(); it != _workerEpollFd.end(); ++it)
    {
        if (it->second >= 0) {
            close(it->second);
        }
    }
#endif

    INK_DEBUG << "EventLoop destroyed";
}

void EventLoop::start() {
    if (_running) return;

    _running = true;

    uint max_threads = Settings::getSettings().max_threads;

#ifdef _WIN32
    // Initialize Windows IOCP
    _completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (_completionPort == NULL) {
        throw std::runtime_error("Failed to create I/O completion port");
    }

    for (int i = 0; i < max_threads; i++) {
        _threads.emplace_back(&EventLoop::run, this);

#else
    for (int i = 0; i < max_threads; i++) {
        _threads.emplace_back(&EventLoop::run, this);

        // Initialize Linux epoll with a large size hint
        _workerEpollFd[_threads[i].get_id()] = epoll_create1(0);
        if (_workerEpollFd[_threads[i].get_id()] < 0) {
            throw std::runtime_error("Failed to create epoll instance");
        }
#endif

#ifdef _WIN32
        SetThreadAffinityMask(_threads.back().native_handle(), 1ULL << i);
#else
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i, &cpuset);
        pthread_setaffinity_np(_threads.back().native_handle(), sizeof(cpu_set_t), &cpuset);
#endif
    }

    INK_DEBUG << "EventLoop started with " << max_threads << " threads";
}

void EventLoop::stop() {
    if (!_running) return;

    _running = false;

// Signal the event loop to wake up
#ifdef _WIN32
    // Post a completion status for each thread
    for (size_t i = 0; i < _threads.size(); i++) {
        PostQueuedCompletionStatus(_completionPort, 0, ULONG_PTR(EXIT_CODE), NULL);
    }
#else
    // Use eventfd to wake up epoll
    for (auto it = _workerWakeupFd.begin(); it != _workerWakeupFd.end(); ++it)
    {
        if (it->second >= 0) {
            uint64_t one = 1;
            write(it->second, &one, sizeof(one));
        }
    }

#endif

    for (auto& thread : _threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    _threads.clear();

    INK_DEBUG << "EventLoop stopped";
}

void EventLoop::addSession(std::shared_ptr<Session> session) {
    if (!session) return;

    socket_t sockfd = session->getSocket();

    _sessions[sockfd] = session;

#ifdef _WIN32
    // Associate socket with completion port
    if (CreateIoCompletionPort((HANDLE)sockfd, _completionPort, (ULONG_PTR)sockfd, 0) == NULL) {
        INK_ERROR << "Failed to associate socket with completion port";
        removeSession(session);
        return;
    }

    // For Windows, we need to post an initial read operation
    WSABUF wsaBuf = { 0 };
    DWORD flags = 0;
    DWORD bytesReceived = 0;

    LPWSAOVERLAPPED overlapped = new WSAOVERLAPPED();
    ZeroMemory(overlapped, sizeof(WSAOVERLAPPED));
    overlapped->hEvent = (HANDLE)READ_OPERATION;

    if (WSARecv(sockfd, &wsaBuf, 1, &bytesReceived, &flags, overlapped, NULL) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            INK_ERROR << "WSARecv failed: " << error;
            delete overlapped;
            removeSession(session);
            return;
        }
    }
#else

    {
        std::lock_guard<std::mutex> lock(_addSessionMutex);

        _currentThread = _currentThread = (_currentThread+1)%Settings::getSettings().max_threads;
    }

    auto it = std::next(_workerEpollFd.begin(), _currentThread);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = sockfd;

    if (epoll_ctl(it->second, EPOLL_CTL_ADD, sockfd, &ev) < 0) {
        INK_ERROR << "Failed to add socket to epoll: " << strerror(errno);
        removeSession(session);
        return;
    }

    session->setWorkerId(it->first);

    // For Linux epoll, register the socket with EPOLLET (edge-triggered) for better performance

#endif

    INK_DEBUG << "Session added to EventLoop";
}

void EventLoop::updateSessionInterest(std::shared_ptr<Session> session, bool interestedInReading, bool interestedInWriting) {
    if (!session) return;

#ifdef _WIN32
    updateSessionInterestWindows(session, interestedInReading, interestedInWriting);
#else
    updateSessionInterestLinux(session, interestedInReading, interestedInWriting);
#endif
}

void EventLoop::removeSession(std::shared_ptr<Session> session) {
    if (!session) return;

    socket_t sockfd = session->getSocket();

    _sessions.unsafe_erase(sockfd);

#ifdef _WIN32
    // For Windows, IOCP operations should be canceled
    // CancelIoEx is preferred, but requires Vista+
    CancelIoEx((HANDLE)sockfd, NULL);
#else
    // Remove from epoll
    epoll_ctl(_workerEpollFd[session->getWorkerId()], EPOLL_CTL_DEL, sockfd, nullptr);
#endif

    INK_DEBUG << "Session removed from EventLoop";
}

void EventLoop::run() {
#ifdef _WIN32
    runWindows();
#else
    runLinux();
#endif
}

#ifdef _WIN32
void EventLoop::runWindows() {
    while (_running) {
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        LPOVERLAPPED pOverlapped = nullptr;

        // Wait for completion (1000ms timeout for responsiveness)
        BOOL success = GetQueuedCompletionStatus(
            _completionPort,
            &bytesTransferred,
            &completionKey,
            &pOverlapped,
            1000
            );

        if (!_running) break;

        // Check for exit signal
        if (completionKey == EXIT_CODE) {
            break;
        }

        if (!success && pOverlapped == nullptr) {
            // Timeout or error without an operation
            continue;
        }

        // Handle the completion
        socket_t sockfd = (socket_t)completionKey;

        if (!pOverlapped) {
            continue;
        }

        std::shared_ptr<Session> session;
        auto it = _sessions.find(sockfd);
        if (it != _sessions.end()) {
            session = it->second;
        }
        else
        {
            session = nullptr;
        }

        if (session) {
            OperationType opType = (OperationType)(ULONG_PTR)pOverlapped->hEvent;

            if (opType == READ_OPERATION) {
                session->onReadReady();

                // Post a new read operation
                WSABUF wsaBuf = { 0 };
                DWORD flags = 0;
                DWORD bytesReceived = 0;

                ZeroMemory(pOverlapped, sizeof(WSAOVERLAPPED));
                pOverlapped->hEvent = (HANDLE)READ_OPERATION;

                if (WSARecv(sockfd, &wsaBuf, 1, &bytesReceived, &flags, pOverlapped, NULL) == SOCKET_ERROR) {
                    int error = WSAGetLastError();
                    if (error != WSA_IO_PENDING) {
                        INK_ERROR << "WSARecv failed: " << error;
                        delete pOverlapped;
                        session->close();
                    }
                }
            }
            else if (opType == WRITE_OPERATION) {
                session->onWriteReady();
                delete pOverlapped;
            }
        } else {
            // Session not found, clean up overlapped
            delete pOverlapped;
        }
    }
}
#else
void EventLoop::runLinux() {
    struct epoll_event events[MAX_EVENTS];

    // Create eventfd for wakeup
    auto wid = std::this_thread::get_id();

    auto& wakeupfd = _workerWakeupFd[wid];
    wakeupfd = eventfd(0, EFD_NONBLOCK);

    auto& epollfd = _workerEpollFd[wid];

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = wakeupfd;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, wakeupfd, &ev);

    INK_TRACE << "wakeupfd register: " <<  wakeupfd << " for thread: " << wid; // TIRA

    while (_running) {
        int numEvents = epoll_wait(epollfd, events, MAX_EVENTS, EPOLL_WAIT_TIMEOUT);

        if (!_running) break;

        if (numEvents < 0) {
            if (errno == EINTR) {
                // Interrupted, just try again
                continue;
            }

            INK_ERROR << "epoll_wait error: " << strerror(errno);
            break;
        }

        for (int i = 0; i < numEvents; i++) {
            // Check if this is the wakeup event
            if (wakeupfd >= 0 && events[i].data.fd == wakeupfd) {
                uint64_t value;
                read(wakeupfd, &value, sizeof(value));
                INK_TRACE << "wakeup thread: " << wid;
                continue;
            }
            INK_TRACE << "thread handle: " << wid;

            socket_t sockfd = events[i].data.fd;

            std::shared_ptr<Session> session;
            auto it = _sessions.find(sockfd);
            if (it != _sessions.end()) {
                session = it->second;
            }
            else
            {
                session = nullptr;
            }

            if (session)
            {
                if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    // Error or hang-up
                    session->close();
                    continue;
                }

                if (events[i].events & EPOLLIN) {
                    // Socket is readable
                    session->onReadReady();
                }

                if (events[i].events & EPOLLOUT) {
                    // Socket is writable
                    session->onWriteReady();
                }
            }
        }
    }

    // Close wakeup eventfd
    if (wakeupfd >= 0) {
        close(wakeupfd);
        wakeupfd = -1;
    }
}
#endif

#ifdef _WIN32
void EventLoop::updateSessionInterestWindows(std::shared_ptr<Session> session, bool interestedInReading, bool interestedInWriting) {
    socket_t sockfd = session->getSocket();

    if (interestedInWriting) {
        // Post a write notification to IOCP
        LPWSAOVERLAPPED overlapped = new WSAOVERLAPPED();
        ZeroMemory(overlapped, sizeof(WSAOVERLAPPED));
        overlapped->hEvent = (HANDLE)WRITE_OPERATION;

        PostQueuedCompletionStatus(_completionPort, 0, (ULONG_PTR)sockfd, overlapped);
    }

    // For reading, we rely on the continuous WSARecv cycle in the runWindows method

    INK_DEBUG << "Session interest updated (Windows): read=" << interestedInReading
              << ", write=" << interestedInWriting;
}
#else
void EventLoop::updateSessionInterestLinux(std::shared_ptr<Session> session, bool interestedInReading, bool interestedInWriting) {
    socket_t sockfd = session->getSocket();

    // Set up epoll events
    struct epoll_event ev;
    ev.data.fd = sockfd;
    ev.events = EPOLLET;  // Edge-triggered mode for better performance

    if (interestedInReading) {
        ev.events |= EPOLLIN;
    }

    if (interestedInWriting) {
        ev.events |= EPOLLOUT;
    }

    if (epoll_ctl(_workerEpollFd[session->getWorkerId()], EPOLL_CTL_MOD, sockfd, &ev) < 0) {
        INK_ERROR << "epoll_ctl error: " << strerror(errno);
        return;
    }

    INK_DEBUG << "Session interest updated (Linux): read=" << interestedInReading
              << ", write=" << interestedInWriting;
}
#endif
