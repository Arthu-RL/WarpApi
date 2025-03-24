#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#pragma once

#include "WarpDefs.h"
#include "EventLoop/EventLoop.h"

class WARP_API HttpServer {
public:
    HttpServer(uint16_t port, size_t numThreads = 2);
    ~HttpServer();

    static void pushJob(std::function<void()>&& job);

    void start();
    void stop();

    // Configuration methods
    void setBacklogSize(int size);
    void setConnectionTimeout(int milliseconds);

    // Update connection activity timestamp
    void updateConnectionActivity(socket_t socket);

private:
    void acceptLoop();
    void cleanupIdleConnections();

    uint16_t _port;
    socket_t _serverSocket;
    std::thread _serverThread;
    std::thread _cleanupThread;
    std::atomic<bool> _running;
    ink::ThreadPool _threadPool;

    // EventLoop for I/O multiplexing
    std::unique_ptr<EventLoop> _eventLoop;

    // Connection management
    struct ConnectionInfo {
        std::weak_ptr<Session> session;
        std::chrono::steady_clock::time_point lastActivity;
    };

    std::unordered_map<socket_t, ConnectionInfo> _connections;
    std::mutex _connectionsMutex;

    // Configuration
    int _backlogSize;
    int _connectionTimeout; // milliseconds
};
#endif // HTTPSERVER_H
