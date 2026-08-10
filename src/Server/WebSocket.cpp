#include "WebSocket.h"
#include "WebSocketContext.h"

#include <cstring>

namespace ws {

namespace {

/** XOR-unmask in place, 8 bytes at a time. */
inline void unmaskInPlace(char* p, usize len, const u8 mask[4]) noexcept
{
    u32 m32;
    std::memcpy(&m32, mask, 4);
    const u64 m64 = (static_cast<u64>(m32) << 32) | m32;

    usize i = 0;
    for (; i + 8 <= len; i += 8)
    {
        u64 v;
        std::memcpy(&v, p + i, 8);
        v ^= m64;
        std::memcpy(p + i, &v, 8);
    }
    // i is a multiple of 8, so the mask phase is still 0 here.
    for (; i < len; ++i)
        p[i] = static_cast<char>(p[i] ^ mask[i & 3]);
}

inline bool isControlOpcode(u8 opcode) noexcept { return (opcode & 0x08) != 0; }

/** RFC 6455 §7.4: which close codes a peer is allowed to put on the wire. */
inline bool isValidCloseCode(u16 code) noexcept
{
    if (code >= 3000 && code <= 4999) return true;
    switch (code)
    {
        case 1000: case 1001: case 1002: case 1003:
        case 1007: case 1008: case 1009: case 1010: case 1011:
            return true;
        default:
            return false;
    }
}

WsAction failConnection(WsState& state, ByteBuffer& writeBuf, u16 code)
{
    if (!state.closeSent)
    {
        sendClose(writeBuf, code);
        state.closeSent = true;
    }
    return WsAction::CloseAfterFlush;
}

} // namespace

std::array<u8, SHA_DIGEST_LENGTH> sha1Digest(std::string_view input)
{
    std::array<u8, SHA_DIGEST_LENGTH> out{};
    SHA1(reinterpret_cast<const u8*>(input.data()), input.size(), out.data());
    return out;
}

bool sendFrame(ByteBuffer& writeBuf, u8 opcode, std::string_view payload, bool fin)
{
    u8 hdr[10];
    usize hdrLen = 0;
    hdr[hdrLen++] = static_cast<u8>((fin ? 0x80 : 0x00) | (opcode & 0x0F));

    const usize len = payload.size();
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

    // Reserve up front: a half-written frame would desynchronise the stream.
    if (!writeBuf.ensureWritable(hdrLen + len))
        return false;

    writeBuf.append(reinterpret_cast<const char*>(hdr), hdrLen);
    if (!payload.empty())
        writeBuf.append(payload);

    return true;
}

bool sendClose(ByteBuffer& writeBuf, u16 code, std::string_view reason)
{
    char buf[WS_CONTROL_MAX_PAYLOAD];
    buf[0] = static_cast<char>((code >> 8) & 0xFF);
    buf[1] = static_cast<char>(code & 0xFF);

    const usize reasonLen = reason.size() > (WS_CONTROL_MAX_PAYLOAD - 2)
                                ? (WS_CONTROL_MAX_PAYLOAD - 2)
                                : reason.size();
    if (reasonLen)
        std::memcpy(buf + 2, reason.data(), reasonLen);

    return sendFrame(writeBuf, WS_OP_CLOSE, std::string_view(buf, 2 + reasonLen));
}

namespace {

/** Delivers a fully reassembled application message to the route. */
WsAction deliverMessage(WsState& state, WebSocketContext& ctx,
                        u8 opcode, std::string_view payload, ByteBuffer& writeBuf)
{
    if (opcode != WS_OP_TEXT && opcode != WS_OP_BINARY)
        return failConnection(state, writeBuf, WS_CLOSE_PROTOCOL_ERROR);

    if (state.route && state.route->onMessage)
        state.route->onMessage(ctx, payload, opcode == WS_OP_BINARY);

    return WsAction::Continue;
}

} // namespace

WsAction processFrames(WsState& state, WebSocketContext& ctx,
                       ByteBuffer& readBuf, ByteBuffer& writeBuf,
                       usize maxMessageSize)
{
    for (;;)
    {
        usize avail = 0;
        char* data = readBuf.mutableReadPtr(avail);
        if (avail < 2)
            return WsAction::Continue;

        const u8 b0 = static_cast<u8>(data[0]);
        const u8 b1 = static_cast<u8>(data[1]);

        const bool fin    = (b0 & 0x80) != 0;
        const u8   rsv    = b0 & 0x70;
        const u8   opcode = b0 & 0x0F;
        const bool masked = (b1 & 0x80) != 0;

        u64   payloadLen = b1 & 0x7F;
        usize offset = 2;

        // No extensions negotiated, so any reserved bit is a protocol error.
        if (rsv != 0)
            return failConnection(state, writeBuf, WS_CLOSE_PROTOCOL_ERROR);

        // Clients MUST mask every frame they send (RFC 6455 §5.1).
        if (!masked)
            return failConnection(state, writeBuf, WS_CLOSE_PROTOCOL_ERROR);

        if (payloadLen == 126)
        {
            if (avail < offset + 2) return WsAction::Continue;
            payloadLen = (static_cast<u64>(static_cast<u8>(data[offset])) << 8) |
                          static_cast<u64>(static_cast<u8>(data[offset + 1]));
            offset += 2;
            if (payloadLen < 126) // must use the shortest length encoding
                return failConnection(state, writeBuf, WS_CLOSE_PROTOCOL_ERROR);
        }
        else if (payloadLen == 127)
        {
            if (avail < offset + 8) return WsAction::Continue;
            payloadLen = 0;
            for (i32 i = 0; i < 8; ++i)
                payloadLen = (payloadLen << 8) | static_cast<u64>(static_cast<u8>(data[offset + i]));
            offset += 8;
            if (payloadLen <= 0xFFFF)
                return failConnection(state, writeBuf, WS_CLOSE_PROTOCOL_ERROR);
            if (payloadLen & (u64(1) << 63)) // MSB must be 0
                return failConnection(state, writeBuf, WS_CLOSE_PROTOCOL_ERROR);
        }

        if (isControlOpcode(opcode))
        {
            // Control frames are never fragmented and never exceed 125 bytes.
            if (!fin || payloadLen > WS_CONTROL_MAX_PAYLOAD)
                return failConnection(state, writeBuf, WS_CLOSE_PROTOCOL_ERROR);
        }
        else if (opcode != WS_OP_CONTINUATION && opcode != WS_OP_TEXT && opcode != WS_OP_BINARY)
        {
            return failConnection(state, writeBuf, WS_CLOSE_PROTOCOL_ERROR);
        }

        // Reject anything we could never buffer, before trying to grow for it.
        if (payloadLen > maxMessageSize ||
            (state.fragmenting && state.fragment.size() + payloadLen > maxMessageSize))
        {
            return failConnection(state, writeBuf, WS_CLOSE_TOO_BIG);
        }

        const usize frameLen = offset + 4 + static_cast<usize>(payloadLen);

        if (avail < frameLen)
        {
            // Make sure the buffer can eventually hold the whole frame,
            // otherwise we would spin waiting for bytes that never fit.
            if (!readBuf.ensureWritable(frameLen - avail))
                return failConnection(state, writeBuf, WS_CLOSE_TOO_BIG);
            return WsAction::Continue;
        }

        u8 mask[4];
        std::memcpy(mask, data + offset, 4);
        offset += 4;

        char* payload = data + offset;
        const usize plen = static_cast<usize>(payloadLen);
        unmaskInPlace(payload, plen, mask);

        // ---- Control frames: handled immediately, never interrupt a fragmented message
        if (isControlOpcode(opcode))
        {
            const std::string_view ctl(payload, plen);

            if (opcode == WS_OP_PING)
            {
                sendFrame(writeBuf, WS_OP_PONG, ctl);
                readBuf.advanceRead(frameLen);
                continue;
            }

            if (opcode == WS_OP_PONG)
            {
                readBuf.advanceRead(frameLen);
                continue;
            }

            // WS_OP_CLOSE
            u16 peerCode = WS_CLOSE_NORMAL;
            if (plen == 1)
                return failConnection(state, writeBuf, WS_CLOSE_PROTOCOL_ERROR);

            if (plen >= 2)
            {
                peerCode = static_cast<u16>((static_cast<u8>(payload[0]) << 8) |
                                             static_cast<u8>(payload[1]));
                if (!isValidCloseCode(peerCode))
                    return failConnection(state, writeBuf, WS_CLOSE_PROTOCOL_ERROR);
            }

            state.closeReceived = true;
            readBuf.advanceRead(frameLen);

            if (!state.closeSent)
            {
                // Echo the peer's code back, as the closing handshake requires.
                sendClose(writeBuf, peerCode);
                state.closeSent = true;
            }

            if (state.route && state.route->onClose)
                state.route->onClose(ctx);

            return WsAction::CloseAfterFlush;
        }

        // ---- Data frames
        if (opcode == WS_OP_CONTINUATION)
        {
            if (!state.fragmenting)
                return failConnection(state, writeBuf, WS_CLOSE_PROTOCOL_ERROR);

            state.fragment.append(payload, plen);
            readBuf.advanceRead(frameLen);

            if (fin)
            {
                state.fragmenting = false;
                const u8 op = state.fragmentOpcode;
                const WsAction act = deliverMessage(state, ctx, op,
                                                    std::string_view(state.fragment), writeBuf);
                state.fragment.clear();
                if (act != WsAction::Continue)
                    return act;
            }
            continue;
        }

        // TEXT or BINARY
        if (state.fragmenting)
        {
            // A new data frame while a fragmented message is open is illegal.
            return failConnection(state, writeBuf, WS_CLOSE_PROTOCOL_ERROR);
        }

        if (fin)
        {
            // Unfragmented: hand the route a view straight into the read buffer,
            // no copy and no allocation.
            const WsAction act = deliverMessage(state, ctx, opcode,
                                                std::string_view(payload, plen), writeBuf);
            readBuf.advanceRead(frameLen);
            if (act != WsAction::Continue)
                return act;
        }
        else
        {
            state.fragmenting = true;
            state.fragmentOpcode = opcode;
            state.fragment.assign(payload, plen);
            readBuf.advanceRead(frameLen);
        }
    }
}

} // namespace ws
