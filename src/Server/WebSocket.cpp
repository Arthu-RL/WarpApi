#include "WebSocket.h"
#include "WebSocketContext.h"
#include "Response/HttpResponse.h"  // for writeAll
#include "Settings/Settings.h"

namespace ws {

std::array<u8, SHA_DIGEST_LENGTH> sha1Digest(std::string_view input)
{
    std::array<u8, SHA_DIGEST_LENGTH> out;
    SHA1(reinterpret_cast<const u8*>(input.data()), input.size(), out.data());
    return out;
}

void sendFrame(ink::RingBuffer& writeBuf, u8 opcode, std::string_view payload, bool fin)
{
    u8 hdr[14];
    usize hdrLen = 0;
    hdr[hdrLen++] = static_cast<u8>((fin ? 0x80 : 0x00) | (opcode & 0x0F));

    usize len = payload.size();
    if (len <= 125)
    {
        hdr[hdrLen++] = static_cast<u8>(len);
    }
    else if (len <= 0xFFFF)
    {
        hdr[hdrLen++] = 126;
        hdr[hdrLen++] = static_cast<u8>((len >> 8) & 0xFF);
        hdr[hdrLen++] = static_cast<u8>(len & 0xFF);
    }
    else
    {
        hdr[hdrLen++] = 127;
        for (i32 i = 7; i >= 0; --i)
            hdr[hdrLen++] = static_cast<u8>((static_cast<u64>(len) >> (i * 8)) & 0xFF);
    }

    HttpResponse::writeAll(writeBuf, reinterpret_cast<const char*>(hdr), hdrLen);
    if (!payload.empty())
    {
        HttpResponse::writeAll(writeBuf, payload.data(), payload.size());
    }
}

static bool dispatchFrame(WsState& state, WebSocketContext& ctx,
                          u8 opcode, bool fin,
                          const char* payload, usize payloadLen,
                          ink::RingBuffer& writeBuf)
{
    if (!fin)
    {
        // Fragmented frames are not supported — close with protocol error 1002
        char closePayload[2] = {0x03, static_cast<char>(0xEA)};
        sendFrame(writeBuf, WS_OP_CLOSE, std::string_view(closePayload, 2));
        state.closeSent = true;
        return false;
    }

    switch (opcode)
    {
    case WS_OP_TEXT:
    case WS_OP_BINARY:
        if (state.route && state.route->onMessage)
            state.route->onMessage(ctx, std::string_view(payload, payloadLen));
        return true;

    case WS_OP_PING:
        sendFrame(writeBuf, WS_OP_PONG, std::string_view(payload, payloadLen));
        return true;

    case WS_OP_PONG:
        return true;

    case WS_OP_CLOSE:
        if (!state.closeSent)
        {
            usize echoLen = payloadLen > WS_CONTROL_MAX_PAYLOAD ? WS_CONTROL_MAX_PAYLOAD : payloadLen;
            sendFrame(writeBuf, WS_OP_CLOSE, std::string_view(payload, echoLen));
            state.closeSent = true;
        }
        if (state.route && state.route->onClose)
            state.route->onClose(ctx);
        return false;

    case WS_OP_CONTINUATION:
    default:
    {
        char closePayload[2] = {0x03, static_cast<char>(0xEA)};
        sendFrame(writeBuf, WS_OP_CLOSE, std::string_view(closePayload, 2));
        state.closeSent = true;
        return false;
    }
    }
}

bool processFrames(WsState& state, WebSocketContext& ctx,
                   ink::RingBuffer& readBuf, ink::RingBuffer& writeBuf)
{
    while (true)
    {
        size_t avail = 0;
        const char* data = readBuf.getReadBuffer(avail);
        if (!data || avail < 2)
            return true;

        const u8 b0 = static_cast<u8>(data[0]);
        const u8 b1 = static_cast<u8>(data[1]);
        const bool fin    = (b0 & 0x80) != 0;
        const u8   opcode = b0 & 0x0F;
        const bool masked = (b1 & 0x80) != 0;
        u64        payloadLen = b1 & 0x7F;
        usize      offset = 2;

        // Clients MUST mask all frames (RFC 6455 §5.1)
        if (!masked)
            return false;

        if (payloadLen == 126)
        {
            if (avail < offset + 2) return true;
            payloadLen = (static_cast<u64>(static_cast<u8>(data[offset])) << 8) |
                         static_cast<u64>(static_cast<u8>(data[offset + 1]));
            offset += 2;
        }
        else if (payloadLen == 127)
        {
            if (avail < offset + 8) return true;
            payloadLen = 0;
            for (i32 i = 0; i < 8; ++i)
                payloadLen = (payloadLen << 8) | static_cast<u64>(static_cast<u8>(data[offset + i]));
            offset += 8;
        }

        // Control frames must not exceed 125 bytes (RFC 6455 §5.5)
        if ((opcode & 0x08) && payloadLen > WS_CONTROL_MAX_PAYLOAD)
            return false;

        if (payloadLen > Settings::getSettings().max_body_size)
            return false;

        // Need 4-byte mask key
        if (avail < offset + 4) return true;
        const u8* mask = reinterpret_cast<const u8*>(data + offset);
        offset += 4;

        // Need full payload
        if (avail < offset + payloadLen) return true;

        std::string payload;
        payload.resize(payloadLen);
        for (u64 i = 0; i < payloadLen; ++i)
            payload[static_cast<usize>(i)] = data[offset + static_cast<usize>(i)] ^ mask[i % 4];

        readBuf.advanceReadPos(offset + static_cast<usize>(payloadLen));

        if (!dispatchFrame(state, ctx, opcode, fin, payload.data(), payload.size(), writeBuf))
            return false;
    }
}

} // namespace ws
