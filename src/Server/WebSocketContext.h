#ifndef WEBSOCKET_CONTEXT_H
#define WEBSOCKET_CONTEXT_H

#pragma once

#include "WarpDefs.h"

class Session;

/**
 * @class WebSocketContext
 * @brief The public API surface for WebSocket route callbacks.
 *
 * Constructed on the stack each time a callback fires (open / message / close).
 * Holds only a reference to the underlying Session so there is zero heap cost.
 * Users should never store this object beyond the lifetime of their callback.
 *
 * Example usage in a service:
 * @code
 *   wsRoute.onMessage = [](WebSocketContext& ctx, std::string_view payload) {
 *       ctx.sendText(payload); // echo
 *   };
 * @endcode
 */
class WARP_API WebSocketContext {
public:
    explicit WebSocketContext(Session& session) noexcept : _session(session) {}

    WebSocketContext(const WebSocketContext&) = delete;
    WebSocketContext& operator=(const WebSocketContext&) = delete;

    /** @brief Send a UTF-8 text frame to the client. */
    void sendText(std::string_view payload);

    /** @brief Send a binary frame to the client. */
    void sendBinary(std::string_view payload);

    /**
     * @brief Initiate a graceful close handshake.
     * @param code  WebSocket status code (default 1000 = normal closure).
     * @param reason Optional close reason string (max 123 bytes).
     */
    void close(u16 code = 1000, std::string_view reason = {});

private:
    Session& _session;
};

#endif // WEBSOCKET_CONTEXT_H
