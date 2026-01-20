#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#pragma once

#include "EventLoop/EventLoop.h"

class SessionManagerWorker;

class WARP_API HttpServer {
public:
    HttpServer();
    ~HttpServer();

    void start();
    void stop();

private:
    bool _running;

    // EventLoop for I/O multiplexing
    std::unique_ptr<EventLoop> _eventLoop;
};
#endif // HTTPSERVER_H
