#ifndef SESSION_H
#define SESSION_H

#pragma once

#include <ink/RingBuffer.h>

#include "WarpDefs.h"
#include "Request/HttpRequest.h"
#include "EventLoop/EventLoop.h"

class WARP_API Session : public std::enable_shared_from_this<Session> {
public:
    Session(socket_t socket, EventLoop* eventLoop = nullptr);
    ~Session();

    void start();
    void close();
    const bool isActive() const noexcept;
    bool setSocketOptimizations();

    // Methods needed for EventLoop integration
    socket_t getSocket() const;
    void onReadReady();
    void onWriteReady();

    void updateActivity();
    bool isIdle(std::chrono::milliseconds timeout) const noexcept;

    std::thread::id getWorkerId() const noexcept;
    void setWorkerId(std::thread::id workerId) noexcept;

private:
    void read();
    void write();
    bool parseRequest();
    void handleRequest();

    socket_t _socket;
    HttpRequest _req;
    bool _keepAlive;

    std::atomic<bool> _active;
    std::atomic<std::chrono::steady_clock::time_point> _lastActivity;

    // Use RingBuffer for efficient I/O
    ink::RingBuffer _readBuffer;
    ink::RingBuffer _writeBuffer;

    // Request/response state tracking
    bool _readingHeaders;
    bool _writingResponse;

    // Reference to the event loop for async I/O
    EventLoop* _eventLoop;

    std::thread::id _workerId;
};

#endif // SESSION_H
