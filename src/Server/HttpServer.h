#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#pragma once

#include "EventLoop/EventLoop.h"

class SessionManagerWorker;

class WARP_API HttpServer {
public:
    HttpServer(uint16_t port,
               size_t connection_timeout_ms = 60000,
               size_t backlog_size = SOMAXCONN);
    ~HttpServer();

    void start();
    void stop();

    // Configuration methods
    void setBacklogSize(int size);

private:
    void acceptLoop();


    uint16_t _port;
    socket_t _serverSocket;

    std::atomic<bool> _running;

    // EventLoop for I/O multiplexing
    std::unique_ptr<EventLoop> _eventLoop;

    // Configuration
    size_t _backlogSize;
    size_t _connectionTimeout; // milliseconds
};
#endif // HTTPSERVER_H
