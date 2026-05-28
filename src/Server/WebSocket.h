#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <openssl/sha.h>
#include "WarpDefs.h"

namespace ws {

// WebSocket Globally Unique Identifier
constexpr std::string_view kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr usize WS_CONTROL_MAX_PAYLOAD = 125;

enum WsMessageTye : u8 {
    WS_OP_CONTINUATION = 0x0,
    WS_OP_TEXT = 0x1,
    WS_OP_BINARY = 0x2,
    WS_OP_CLOSE = 0x8,
    WS_OP_PING = 0x9,
    WS_OP_PONG = 0xA,
};

std::array<u8, SHA_DIGEST_LENGTH> sha1Digest(std::string_view input);

/**
 * @struct WsState
 * @brief Lightweight per-connection WebSocket state embedded inside Session.
 *
 * Kept as a POD struct so it can live inline in Session's memory without
 * any heap allocation. Only accessed when Session::_mode == WebSocket,
 * so the HTTP fast path never touches it.
 */
struct WsState {
    const WebSocketRoute* route = nullptr;
    bool closeSent = false;

    void reset() noexcept {
        route = nullptr;
        closeSent = false;
    }
};

/**
 * @brief Encodes and writes a WebSocket frame into the write buffer.
 * @param writeBuf Destination ring buffer (Session's write buffer).
 * @param opcode   WebSocket opcode (WS_OP_TEXT, WS_OP_BINARY, etc.).
 * @param payload  Frame payload.
 * @param fin      Whether this is the final fragment (true for all non-fragmented frames).
 */
void sendFrame(ink::RingBuffer& writeBuf, u8 opcode, std::string_view payload, bool fin = true);

/**
 * @brief Drains and processes all complete WebSocket frames from the read buffer.
 *
 * Dispatches each frame to the appropriate route callback via @p ctx.
 * Returns false when the connection must be closed (invalid frame, close frame received, etc.).
 *
 * @param state   Per-connection WebSocket state (route pointer, close flag).
 * @param ctx     Context object passed to user callbacks.
 * @param readBuf Source ring buffer containing raw bytes from the network.
 * @param writeBuf Destination ring buffer for outbound frames (pongs, close echoes).
 * @return true to keep the connection alive, false to close it.
 */
bool processFrames(WsState& state, WebSocketContext& ctx,
                   ink::RingBuffer& readBuf, ink::RingBuffer& writeBuf);

}

#endif // WEBSOCKET_H
