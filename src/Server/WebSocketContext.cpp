#include "WebSocketContext.h"
#include "Session.h"

bool WebSocketContext::sendText(std::string_view payload)
{
    return _session.wsFrameSend(ws::WS_OP_TEXT, payload);
}

bool WebSocketContext::sendBinary(std::string_view payload)
{
    return _session.wsFrameSend(ws::WS_OP_BINARY, payload);
}

bool WebSocketContext::sendPing(std::string_view payload)
{
    if (payload.size() > ws::WS_CONTROL_MAX_PAYLOAD)
        payload = payload.substr(0, ws::WS_CONTROL_MAX_PAYLOAD);

    return _session.wsFrameSend(ws::WS_OP_PING, payload);
}

void WebSocketContext::close(u16 code, std::string_view reason)
{
    _session.wsClose(code, reason);
}

bool WebSocketContext::isOpen() const noexcept
{
    return _session.wsIsOpen();
}

usize WebSocketContext::pendingBytes() const noexcept
{
    return _session.pendingWriteBytes();
}
