#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#pragma once

#include "WarpDefs.h"
#include "EventLoop/EventLoop.h"

class WARP_API HttpServer {
public:
    HttpServer(uint16_t port,
               size_t numThreads = 2,
               size_t connection_timeout_ms = 60000,
               size_t backlog_size = SOMAXCONN);
    ~HttpServer();

    void start();
    void stop();

    // Configuration methods
    void setBacklogSize(int size);
    void setConnectionTimeout(int milliseconds);

private:
    void acceptLoop();
    void cleanupIdleConnections();

    uint16_t _port;
    socket_t _serverSocket;
    // std::thread _serverThread;
    std::thread _cleanupThread;
    std::atomic<bool> _running;
    ink::ThreadPool _threadPool;

    // EventLoop for I/O multiplexing
    std::unique_ptr<EventLoop> _eventLoop;

    SessionTable _connections;

    // Configuration
    size_t _backlogSize;
    size_t _connectionTimeout; // milliseconds
};
#endif // HTTPSERVER_H
