#ifndef SESSION_H
#define SESSION_H

#pragma once

#include <ink/RingBuffer.h>
#include <ink/TimerWheel.h>
#include "WarpDefs.h"
#include "Request/HttpRequest.h"

/**
 * @class Session
 * @brief Represents an active network connection optimized for io_uring.
 * * This class manages per-connection buffers and state. It is designed to be
 * stored in a fixed-memory ObjectPool to leverage io_uring's high-performance features.
 */
class WARP_API Session : public ink::TimerNode {
public:
    /** @brief Closes the underlying socket and cleans up session state on destruction. */
    ~Session();

    /** @brief Closes the underlying socket and cleans up session state. */
    void close();

    /** @brief Shut down the socket to cancel pending network IO gracefully. */
    void shutdown();

    /** @brief Returns the raw file descriptor for this session. */
    socket_t getSocket() const noexcept;

public:
    u64 lastActivityTick = 0;

private:
    bool parseRequest();
    void handleRequest();

    socket_t _socket;
    HttpRequest _req;
    bool _keepAlive;

    ink::RingBuffer _readBuffer;
    ink::RingBuffer _writeBuffer;

#ifdef USE_EPOLL
public:
    /** @brief Constructs a session with a raw socket descriptor. */
    explicit Session(socket_t socket, socket_t assignedEpollFd);

    socket_t getAssignedEpollFd() const noexcept;

    // Called by the Worker Thread Loop
    bool onReadReady();
    bool onWriteReady();

private:
    void onWriteComplete();
    socket_t _assignedEpollFd;
#endif

#ifdef USE_IOURING
public:
    /** @brief Constructs a session with a raw socket descriptor. */
    explicit Session(socket_t socket);

    /**
     * @brief Prepares an SQE for a non-blocking receive operation.
     * @param sqe Pointer to a submission queue entry obtained from the ring.
     */
    void onReadReady(io_uring_sqe* sqe);

    /**
     * @brief Prepares an SQE for a zero-copy send operation.
     * @param sqe Pointer to a submission queue entry obtained from the ring.
     */
    void onWriteReady(io_uring_sqe* sqe);

    /**
     * @brief Finalizes a write operation after CQE completion.
     * @return true if session remains active, false if it should be closed.
     */
    bool processWrite(i32 bytesSent, bool is_notif, io_uring* ring);

    /**
     * @brief Processes data received from the kernel.
     * @return true if session remains active, false if it should be closed.
     */
    bool processRead(i32 bytesRecv, io_uring* ring);

    bool isReadInFlight() const { return (_ioFlags & IO_READING) != 0; }
    bool isWriteInFlight() const { return (_ioFlags & IO_WRITING) != 0; }
    bool isZcNotifInFlight() const { return (_ioFlags & IO_WAITING_ZC) != 0; }
    bool hasPendingIo() const { return _ioFlags != IO_NONE; }

    SessionStatus& getStatus() {
        return _status;
    }

    // Lifecycle Status Setter
    void setStatus(SessionStatus status) {
        _status = status;
    }

    // Usage: updateIoState(IO_READING, true)  -> Adds flag
    // Usage: updateIoState(IO_READING, false) -> Removes flag
    void updateIoState(IoStateFlags flag, bool active) {
        if (active)
            _ioFlags = static_cast<IoStateFlags>(_ioFlags | flag);
        else
            _ioFlags = static_cast<IoStateFlags>(_ioFlags & ~flag);
    }

    // Utility for quick clearing (e.g., on hard close)
    void resetIoState() {
        _ioFlags = IO_NONE;
    }
private:

    SessionStatus _status = SessionStatus::Active;
    IoStateFlags  _ioFlags = IO_NONE;

    /**
     * @brief Operation scope for a session to avoid using dynamic allocs.
     * It works like a wrapper to grab the context of the session.
     */
    IoRequest _readReq{this, OperationType::Read};
    IoRequest _writeReq{this, OperationType::Write};

    usize _lockedZcBytes = 0;
#endif
};

#endif // SESSION_H
