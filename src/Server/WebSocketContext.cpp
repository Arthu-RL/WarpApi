#include "WebSocketContext.h"
#include "Session.h"

void WebSocketContext::sendText(std::string_view payload)
{
    _session.wsFrameSend(ws::WS_OP_TEXT, payload);
}

void WebSocketContext::sendBinary(std::string_view payload)
{
    _session.wsFrameSend(ws::WS_OP_BINARY, payload);
}

void WebSocketContext::close(u16 code, std::string_view reason)
{
    // Close payload: 2-byte big-endian status code followed by optional reason
    char buf[125 + 2];
    buf[0] = static_cast<char>((code >> 8) & 0xFF);
    buf[1] = static_cast<char>(code & 0xFF);

    usize reasonLen = reason.size() > 123 ? 123 : reason.size();
    if (reasonLen > 0)
        std::memcpy(buf + 2, reason.data(), reasonLen);

    _session.wsFrameSend(ws::WS_OP_CLOSE, std::string_view(buf, 2 + reasonLen));
}
