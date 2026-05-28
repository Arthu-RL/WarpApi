#ifndef SESSION_H
#define SESSION_H

#pragma once

#include <ink/RingBuffer.h>
#include <ink/TimerWheel.h>
#include "WarpDefs.h"
#include "Request/HttpRequest.h"
#include "Server/WebSocket.h"

/**
 * @class Session
 * @brief Pure transport layer for a single network connection.
 *
 * Manages the socket descriptor, read/write ring buffers, and I/O state.
 * Protocol dispatch is handled via a lightweight ProtocolMode tag:
 *
 *   - Http mode      → parseRequest() + handleRequest()  (zero extra overhead)
 *   - WebSocket mode → ws::processFrames() with the embedded WsState
 *
 * WebSocket framing logic lives entirely in WebSocketCodec.{h,cpp}.
 * User callbacks receive a WebSocketContext& (never a raw Session&) so the
 * internal transport API stays fully encapsulated.
 */
class WARP_API Session : public ink::TimerNode {
public:
    /** @brief Closes the underlying socket and cleans up session state on destruction. */
    ~Session();

    /** @brief Closes the underlying socket and cleans up session state. */
    void close();

    /** @brief Shuts down the socket to cancel pending network IO gracefully. */
    void shutdown();

    /** @brief Returns the raw file descriptor for this session. */
    socket_t getSocket() const noexcept;

public:
    u64 lastActivityTick = 0;

private:
    /**
     * @brief Parse the request stream, extracting headers and body
     *
     * Use integer cmp in headers parse, and, avoid allocations for the body received using string views
     */
    bool parseRequest();

    /**
     * @brief This executes the logic for each endpoint (route) called throught a request
     *
     * @note This function uses to radix tree search to find the route.
     */
    void handleRequest();

    /**
     * @brief Upgrade to websocket request if, websocket handshake headers are sent correctly according to rfc6455
     * @link https://www.rfc-editor.org/rfc/rfc6455.html
     *
     * Upgrade the connction sending the handshake from server
     */
    bool upgradeToWebSocket();

    /** @brief Encodes and queues a WebSocket frame. Called exclusively by WebSocketContext. */
    void wsFrameSend(u8 opcode, std::string_view payload, bool fin = true);

    friend class WebSocketContext;

    enum class ProtocolMode : u8 {
        Http = 0,
        WebSocket
    };

    socket_t _socket;
    HttpRequest _req;
    bool _keepAlive;
    ProtocolMode _mode = ProtocolMode::Http;
    ws::WsState _wsState;

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

    bool isReadInFlight()    const { return (_ioFlags & IO_READING)    != 0; }
    bool isWriteInFlight()   const { return (_ioFlags & IO_WRITING)    != 0; }
    bool isZcNotifInFlight() const { return (_ioFlags & IO_WAITING_ZC) != 0; }
    bool hasPendingIo()      const { return _ioFlags != IO_NONE; }

    SessionStatus& getStatus() { return _status; }

    void setStatus(SessionStatus status) { _status = status; }

    void updateIoState(IoStateFlags flag, bool active) {
        if (active)
            _ioFlags = static_cast<IoStateFlags>(_ioFlags | flag);
        else
            _ioFlags = static_cast<IoStateFlags>(_ioFlags & ~flag);
    }

    void resetIoState() { _ioFlags = IO_NONE; }

private:
    SessionStatus _status = SessionStatus::Active;
    IoStateFlags _ioFlags = IO_NONE;

    /**
     * @brief Operation scope for a session to avoid using dynamic allocs.
     * Works like a wrapper to grab the context of the session.
     */
    IoRequest _readReq{this, OperationType::Read};
    IoRequest _writeReq{this, OperationType::Write};

    usize _lockedZcBytes = 0;
#endif
};

#endif // SESSION_H
