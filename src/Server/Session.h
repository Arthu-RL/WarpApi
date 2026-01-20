#ifndef SESSION_H
#define SESSION_H

#pragma once

#include <ink/RingBuffer.h>
#include <memory>
#include "WarpDefs.h"
#include "Request/HttpRequest.h"

// Forward decl
struct epoll_event;

class WARP_API Session : public std::enable_shared_from_this<Session> {
public:
    // Remove EventLoop* from constructor
    explicit Session(socket_t socket, socket_t assignedEpollFd);
    ~Session();

    void close();

    // Direct IO Interest Management (No EventLoop pointer needed)
    void updateIoInterest(SessionInterest interest) noexcept;

    // Getters/Setters
    socket_t getSocket() const;

    // Called by the Worker Thread Loop
    void onReadReady();
    void onWriteReady();

    void updateActivity();
    bool isIdle(std::chrono::milliseconds timeout) const noexcept;

    socket_t getAssignedEpollFd() const noexcept;

private:
    void read();
    void write();
    bool parseRequest();
    void handleRequest();
    void onWriteComplete();

    socket_t _socket;
    socket_t _assignedEpollFd;

    HttpRequest _req;
    bool _keepAlive;
    bool _writingResponse;

    std::chrono::steady_clock::time_point _lastActivity;

    ink::RingBuffer _readBuffer;
    ink::RingBuffer _writeBuffer;
};

#endif // SESSION_H
