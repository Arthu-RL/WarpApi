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
    /** @brief Constructs a session with a raw socket descriptor. */
    explicit Session(socket_t socket);
    ~Session();

    /** @brief Closes the underlying socket and cleans up session state. */
    void close();

    /** @brief Returns the raw file descriptor for this session. */
    socket_t getSocket() const;

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
    bool processWrite(i32 bytesRead, io_uring* ring, bool isNotif);

    /**
     * @brief Processes data received from the kernel.
     * @return true if session remains active, false if it should be closed.
     */
    bool processRead(i32 bytesRead, io_uring* ring);

public:
    i32 pendingRequests = 0;
    SessionState state = SessionState::Active;

private:
    bool parseRequest();
    void handleRequest();

    socket_t _socket;
    HttpRequest _req;
    bool _keepAlive;

    usize _lockedZcBytes = 0;
    bool _isReadPending = false;
    bool _isWritePending = false;

    ink::RingBuffer _readBuffer;
    ink::RingBuffer _writeBuffer;

    /**
     * @brief Operation scope for a session to avoid using dynamic allocs.
     * It works like a wrapper to grab the context of the session.
     */
    IoRequest _readReq{this, OperationType::Read};
    IoRequest _writeReq{this, OperationType::Write};
};
#endif // SESSION_H
