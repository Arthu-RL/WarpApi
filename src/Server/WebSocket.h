#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#pragma once

#include <openssl/sha.h>
#include <string>

#include "WarpDefs.h"
#include "Utils/ByteBuffer.h"

namespace ws {

// WebSocket Globally Unique Identifier (RFC 6455 §1.3)
constexpr std::string_view kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr usize WS_CONTROL_MAX_PAYLOAD = 125;
constexpr usize WS_MAX_KEY_LEN = 32;

enum WsOpcode : u8 {
    WS_OP_CONTINUATION = 0x0,
    WS_OP_TEXT         = 0x1,
    WS_OP_BINARY       = 0x2,
    WS_OP_CLOSE        = 0x8,
    WS_OP_PING         = 0x9,
    WS_OP_PONG         = 0xA,
};

// RFC 6455 §7.4.1
enum WsCloseCode : u16 {
    WS_CLOSE_NORMAL          = 1000,
    WS_CLOSE_GOING_AWAY      = 1001,
    WS_CLOSE_PROTOCOL_ERROR  = 1002,
    WS_CLOSE_UNSUPPORTED     = 1003,
    WS_CLOSE_INVALID_PAYLOAD = 1007,
    WS_CLOSE_POLICY          = 1008,
    WS_CLOSE_TOO_BIG         = 1009,
    WS_CLOSE_INTERNAL_ERROR  = 1011,
};

/** Outcome of a frame-processing pass, decided by the codec and acted on by Session. */
enum class WsAction : u8 {
    Continue,        ///< Keep the connection open, wait for more bytes.
    CloseAfterFlush  ///< A close frame is queued: send what is buffered, then close.
};

std::array<u8, SHA_DIGEST_LENGTH> sha1Digest(std::string_view input);

/**
 * @struct WsState
 * @brief Per-connection WebSocket state, embedded inline in Session.
 *
 * Only touched when Session::_mode == WebSocket, so the HTTP fast path never
 * pays for it. @c fragment stays empty (and therefore allocation-free) unless
 * a peer actually fragments a message.
 */
struct WsState {
    const WebSocketRoute* route = nullptr;
    std::string fragment;     ///< Accumulated payload of an in-progress fragmented message.
    u8   fragmentOpcode = 0;  ///< Opcode of the first frame of that message.
    bool fragmenting = false;
    bool closeSent = false;
    bool closeReceived = false;

    void reset() noexcept
    {
        route = nullptr;
        fragment.clear();
        fragmentOpcode = 0;
        fragmenting = false;
        closeSent = false;
        closeReceived = false;
    }
};

/**
 * @brief Encodes and appends a WebSocket frame to @p writeBuf.
 *
 * Server-to-client frames are never masked (RFC 6455 §5.1).
 */
bool sendFrame(ByteBuffer& writeBuf, u8 opcode, std::string_view payload, bool fin = true);

/** @brief Queues a close frame carrying @p code and an optional @p reason. */
bool sendClose(ByteBuffer& writeBuf, u16 code, std::string_view reason = {});

/**
 * @brief Consumes every complete frame available in @p readBuf.
 *
 * Handles fragmentation, control frames, masking and all the framing-level
 * protocol rules; message payloads are delivered to the route callbacks
 * through @p ctx. Any protocol violation queues the appropriate close frame
 * and returns WsAction::CloseAfterFlush so the peer receives a well-formed
 * close before the socket goes away.
 *
 * @param maxMessageSize Hard cap on a single (possibly reassembled) message.
 */
WsAction processFrames(WsState& state, WebSocketContext& ctx,
                       ByteBuffer& readBuf, ByteBuffer& writeBuf,
                       usize maxMessageSize);

} // namespace ws

#endif // WEBSOCKET_H
