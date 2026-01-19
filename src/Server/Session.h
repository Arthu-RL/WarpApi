#ifndef SESSION_H
#define SESSION_H

#pragma once

#include <ink/RingBuffer.h>

#include "WarpDefs.h"
#include "Request/HttpRequest.h"

class WARP_API Session : public std::enable_shared_from_this<Session> {
public:
    Session(socket_t socket, EventLoop* eventLoop = nullptr);
    ~Session();

    void start();
    void close();
    void setSocketOptimizations();

    // Methods needed for EventLoop integration
    socket_t getSocket() const;
    void onReadReady();
    void onWriteReady();

    void updateActivity();
    bool isIdle(std::chrono::milliseconds timeout) const noexcept;

    socket_t getAssignedEpollFd() const noexcept;
    void setAssignedEpollFd(socket_t fd) noexcept;

private:
    void read();
    void write();
    bool parseRequest();
    void handleRequest();
    void onWriteComplete();

    std::atomic<socket_t> _socket;
    HttpRequest _req;
    bool _keepAlive;

    std::atomic<std::chrono::steady_clock::time_point> _lastActivity;

    // Use RingBuffer for efficient I/O
    ink::RingBuffer _readBuffer;
    ink::RingBuffer _writeBuffer;

    // Request/response state tracking
    bool _writingResponse;

    // Reference to the event loop for async I/O
    EventLoop* _eventLoop;

    socket_t _assignedEpollFd = -1;
};

#endif // SESSION_H
