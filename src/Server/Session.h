#ifndef SESSION_H
#define SESSION_H

#pragma once

#include <ink/TimerWheel.h>

#include "WarpDefs.h"
#include "Request/HttpRequest.h"
#include "Response/HttpResponse.h"
#include "Server/WebSocket.h"
#include "Utils/ByteBuffer.h"

/**
 * @class Session
 * @brief Pure transport layer for a single network connection.
 *
 * Manages the socket descriptor, read/write buffers, and I/O state.
 * Protocol dispatch is handled via a lightweight ProtocolMode tag:
 *
 *   - Http mode      → parseRequest() + handleRequest()  (zero extra overhead)
 *   - WebSocket mode → ws::processFrames() with the embedded WsState
 *
 * WebSocket framing logic lives entirely in WebSocket.{h,cpp}. User callbacks
 * receive a WebSocketContext& (never a raw Session&) so the internal transport
 * API stays fully encapsulated.
 *
 * @note A Session never closes its own descriptor. It raises `wantsClose()` and
 *       the owning EventLoop performs the epoll de-registration, ::close and
 *       pool release in one place. Self-closing was the source of a
 *       use-after-free: the loop kept a dead Session in its fd table while the
 *       kernel handed the same fd number to the next connection.
 */
class WARP_API Session : public ink::TimerNode {
public:
    ~Session();

    /** @brief Marks the connection for teardown by the owning event loop. */
    void requestClose() noexcept { _wantClose = true; }

    /** @brief True once the connection is finished and may be reclaimed. */
    bool wantsClose() const noexcept { return _wantClose; }

    /** @brief Shuts down the socket to cancel pending network IO gracefully. */
    void shutdown();

    /** @brief Closes the underlying socket. Called by the event loop only. */
    void close();

    socket_t getSocket() const noexcept { return _socket; }

    usize pendingWriteBytes() const noexcept { return _writeBuffer.size(); }

    // WebSocket transport hooks, used by WebSocketContext
    bool wsFrameSend(u8 opcode, std::string_view payload, bool fin = true);
    void wsClose(u16 code, std::string_view reason);
    bool wsIsOpen() const noexcept;

public:
    u64 lastActivityTick = 0;

private:
    enum class ParseResult : u8 {
        Complete,    ///< A full request was extracted.
        Incomplete,  ///< Need more bytes from the peer.
        Error        ///< Malformed or oversized; the connection must be failed.
    };

    /**
     * @brief Extracts one request from the read buffer.
     *
     * Uses length-bucketed, case-insensitive header matching and keeps every
     * field as a string_view into the read buffer, so a request costs no
     * allocation at all.
     */
    ParseResult parseRequest();

    /** @brief Routes one parsed request through EndpointManager and writes the response. */
    void handleRequest();

    /** @brief Drains the read buffer, honouring pipelining and write backpressure. */
    bool drainInput();

    /** @brief Feeds buffered bytes through the WebSocket codec. */
    bool drainWebSocket();

    /** @brief Returns grown buffers to their configured size once drained. */
    void recycleBuffers();

    /**
     * @brief Performs the RFC 6455 opening handshake.
     * @link https://www.rfc-editor.org/rfc/rfc6455.html
     */
    void upgradeToWebSocket();

    /** @brief Emits a minimal error response and marks the connection for close. */
    void sendErrorAndClose(i32 status, std::string_view message);

    friend class WebSocketContext;

    enum class ProtocolMode : u8 {
        Http = 0,
        WebSocket
    };

    socket_t _socket;
    HttpRequest _req;

    /**
     * Reused across every request on this connection. Constructing it per
     * request re-zeroed its ~360 bytes of header slot arrays each time; as a
     * member that cost is paid once per connection instead, and begin() only
     * has to reset a handful of scalars because the slot arrays are guarded by
     * a presence mask exactly like the request's.
     */
    HttpResponse _res;

    bool _keepAlive = true;
    bool _wantClose = false;
    bool _closeAfterFlush = false;
    bool _continueSent = false;
    ProtocolMode _mode = ProtocolMode::Http;
    ws::WsState _wsState;

    ByteBuffer _readBuffer;
    ByteBuffer _writeBuffer;

    /// Bytes of the current request consumed once its handler has run.
    usize _consumed = 0;

#ifdef USE_EPOLL
public:
    explicit Session(socket_t socket, socket_t assignedEpollFd);

    socket_t getAssignedEpollFd() const noexcept { return _assignedEpollFd; }

    /** @return false when the session is finished and must be released. */
    bool onReadReady();
    bool onWriteReady();

    /** @brief True when a short write left data queued and EPOLLOUT is needed. */
    bool writePending() const noexcept { return _writeBuffer.size() > 0; }

private:
    /** @brief Pushes queued bytes to the socket until EAGAIN. */
    bool flushWrites();

    /**
     * @brief Registers or drops EPOLLOUT interest.
     *
     * Keeping EPOLLOUT armed permanently costs a spurious wakeup on every
     * connection; it is only worth a EPOLL_CTL_MOD on the rare short write.
     */
    void updateEpollInterest();

    socket_t _assignedEpollFd;
    bool _writeArmed = false;
    /// Set when backpressure stopped an edge-triggered read mid-stream.
    bool _readSuspended = false;
#endif

#ifdef USE_IOURING
public:
    explicit Session(socket_t socket);

    /** @brief Prepares an SQE for a non-blocking receive. */
    void onReadReady(io_uring_sqe* sqe);

    /**
     * @brief Prepares an SQE for the queued outbound bytes.
     *
     * Outbound data is double-buffered: handlers always append to _writeBuffer
     * while the kernel owns _flightBuffer. Without the flip, appending to a
     * buffer that a submitted SQE still points at could reallocate it out from
     * under the kernel.
     */
    void onWriteReady(io_uring_sqe* sqe);

    /** @return false if the session should be closed. */
    bool processWrite(i32 bytesSent, bool is_notif, io_uring* ring);
    bool processRead(i32 bytesRecv, io_uring* ring);

    bool isReadInFlight()    const { return (_ioFlags & IO_READING)    != 0; }
    bool isWriteInFlight()   const { return (_ioFlags & IO_WRITING)    != 0; }
    bool isZcNotifInFlight() const { return (_ioFlags & IO_WAITING_ZC) != 0; }
    bool hasPendingIo()      const { return _ioFlags != IO_NONE; }

    SessionStatus getStatus() const { return _status; }
    void setStatus(SessionStatus status) { _status = status; }

    void updateIoState(IoStateFlags flag, bool active)
    {
        if (active)
            _ioFlags = static_cast<IoStateFlags>(_ioFlags | flag);
        else
            _ioFlags = static_cast<IoStateFlags>(_ioFlags & ~flag);
    }

    void resetIoState() { _ioFlags = IO_NONE; }

    bool hasQueuedOutput() const { return _flightBuffer.size() > 0 || _writeBuffer.size() > 0; }

private:
    /** @brief Moves queued bytes into the kernel-owned buffer when it is free. */
    void flipWriteBuffers();

    SessionStatus _status = SessionStatus::Active;
    IoStateFlags _ioFlags = IO_NONE;

    /**
     * @brief Operation scope for a session to avoid using dynamic allocs.
     * Works like a wrapper to grab the context of the session.
     */
    IoRequest _readReq{this, OperationType::Read};
    IoRequest _writeReq{this, OperationType::Write};

    ByteBuffer _flightBuffer;
    usize _lockedZcBytes = 0;
    bool _zcInFlight = false;
#endif
};

#endif // SESSION_H
