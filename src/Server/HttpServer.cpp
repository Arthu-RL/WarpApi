#include "HttpServer.h"
#include "Server/Session.h"
#include "Settings/Settings.h"

HttpServer::HttpServer(uint16_t port,
                       size_t numThreads,
                       size_t connection_timeout_ms,
                       size_t backlog_size)
    : _port(port),
    _threadPool(numThreads),
    _running(false),
    _eventLoop(std::make_unique<EventLoop>()),
    _backlogSize(backlog_size),
    _connectionTimeout(connection_timeout_ms)
{
// Initialize platform-specific socket functionality
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        throw std::runtime_error("Failed to initialize Winsock");
    }
#endif

    INK_ASSERT_MSG(Settings::isValid(), "Settings not initialized!");
    INK_ASSERT_MSG(_eventLoop != nullptr, "EventLoop is NULL!");

    INK_INFO << "HTTP Server created with " << numThreads << " worker threads";
}

HttpServer::~HttpServer()
{
    stop();

#ifdef _WIN32
    WSACleanup();
#endif

    INK_DEBUG << "Server destroyed!";
}

void HttpServer::start()
{
    if (_running) {
        return;
    }

    _serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (_serverSocket == SOCKET_ERROR_VALUE) {
        throw std::runtime_error("Failed to create server socket");
    }

    // Set socket options for reuse
    int opt = 1;
    if (setsockopt(_serverSocket, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&opt), sizeof(opt)) < 0) {
        throw std::runtime_error("Failed to set socket reuse option");
    }

    // Set TCP_NODELAY to disable Nagle's algorithm
    if (setsockopt(_serverSocket, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&opt), sizeof(opt)) < 0) {
        INK_WARN << "Failed to set TCP_NODELAY option";
    }

    // Set socket buffer sizes for performance
    int bufSize = 1024 * 64; // 64kb buffer
    if (setsockopt(_serverSocket, SOL_SOCKET, SO_RCVBUF,
                   reinterpret_cast<const char*>(&bufSize), sizeof(bufSize)) < 0) {
        INK_WARN << "Failed to set receive buffer size";
    }

    if (setsockopt(_serverSocket, SOL_SOCKET, SO_SNDBUF,
                   reinterpret_cast<const char*>(&bufSize), sizeof(bufSize)) < 0) {
        INK_WARN << "Failed to set send buffer size";
    }

// Set non-blocking mode for the server socket
#ifdef _WIN32
    unsigned long nonBlocking = 1;
    if (ioctlsocket(_serverSocket, FIONBIO, &nonBlocking) != 0) {
        throw std::runtime_error("Failed to set non-blocking mode");
    }
#else
    int flags = fcntl(_serverSocket, F_GETFL, 0);
    if (flags == -1 || fcntl(_serverSocket, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::runtime_error("Failed to set non-blocking mode");
    }
#endif

    // Bind to port
    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(_port);

    if (bind(_serverSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        throw std::runtime_error("Failed to bind to port");
    }

    // Start listening with configured backlog
    if (listen(_serverSocket, _backlogSize) < 0) {
        throw std::runtime_error("Failed to listen on socket");
    }

    // Start the event loop
    _eventLoop->start();

    // Start connection cleanup timer if timeout is enabled
    if (_connectionTimeout > 0) {
        _cleanupThread = std::thread(&HttpServer::cleanupIdleConnections, this);
    }

    auto settings = Settings::getSettings();
    INK_INFO << "Server started successfully on " << settings.ip << ":" << settings.port;
    INK_INFO << "Thread pool size: " << settings.max_threads;
    INK_DEBUG << "Connection backlog: " << settings.backlog_size;
    INK_DEBUG << "Connection timeout: " << settings.connection_timeout_ms << "ms";

    _running = true;
    // _serverThread = std::thread(&HttpServer::acceptLoop, this);
    acceptLoop(); // Using the main thread
}

void HttpServer::stop()
{
    if (!_running) {
        return;
    }

    _running = false;

    // Stop event loop
    if (_eventLoop) {
        _eventLoop->stop();
    }

// Close server socket
#ifdef _WIN32
    closesocket(_serverSocket);
#else
    close(_serverSocket);
#endif

    // if (_serverThread.joinable()) {
    //     _serverThread.join();
    // }

    if (_cleanupThread.joinable()) {
        _cleanupThread.join();
    }

    INK_INFO << "Server stopped";
}

void HttpServer::acceptLoop() {
    while (_running) {
        sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);

        socket_t clientSocket = accept(_serverSocket,
                                       reinterpret_cast<sockaddr*>(&clientAddr),
                                       &clientAddrLen);

        if (clientSocket != SOCKET_ERROR_VALUE) {
// Set socket to non-blocking mode
#ifdef _WIN32
            unsigned long nonBlocking = 1;
            ioctlsocket(clientSocket, FIONBIO, &nonBlocking);
#else
            int flags = fcntl(clientSocket, F_GETFL, 0);
            if (flags != -1) {
                fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);
            }
#endif

            // Set TCP_NODELAY for client socket too
            int opt = 1;
            setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY,
                       reinterpret_cast<const char*>(&opt), sizeof(opt));

            // Create a session with the EventLoop
            auto session = std::make_shared<Session>(clientSocket, _eventLoop.get());

            _connections[clientSocket] = session;

            // Process the connection in the thread pool
            _threadPool.submit([session]() {
                session->start();
            });
        } else {
// Check if error is because we would block (no connections available)
#ifdef _WIN32
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                INK_ERROR << "Accept error: " << error;
            }
#else
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                INK_ERROR << "Accept error: " << strerror(errno);
            }
#endif

            // Sleep a bit to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void HttpServer::cleanupIdleConnections() {
    auto timeout = std::chrono::milliseconds(_connectionTimeout);

    while (_running) {
        // Check connections every second
        std::this_thread::sleep_for(std::chrono::seconds(1));

        for (auto it = _connections.begin(); it != _connections.end();)
        {
            if (it->second->isIdle(timeout)) {
                it->second->close();
                it = _connections.unsafe_erase(it);
            } else {
                ++it;
            }
        }
    }
}

void HttpServer::setBacklogSize(int size) {
    _backlogSize = size;
}

void HttpServer::setConnectionTimeout(int milliseconds) {
    _connectionTimeout = milliseconds;
}
