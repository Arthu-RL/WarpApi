#ifndef HEADERSLIST_H
#define HEADERSLIST_H

#pragma once

#include "WarpDefs.h"
#include <array>
#include <string_view>

// Common header values
#define APP_INFO_HEADER "WarpApi/0.1"
#define KEEP_ALIVE_HEADER "keep-alive"
#define CLOSE_CONN_HEADER "close"
#define UPGRADE_HEADER "Upgrade"
#define WEBSOCKET_UPGRADE_HEADER "websocket"
#define WS_VERSION_13_HEADER "13"
#define DEFAULT_CONTENT_TYPE "application/json"
#define TEXT_CONTENT_TYPE "text/plain"

/**
 * The single source of truth for every header WarpApi knows about.
 *
 * X(enumerator, wire name)
 *
 * The enumerator is a dense *ordinal* used to index the per-message header
 * slot arrays. Presence is tracked separately in a HeaderMask bitmask
 * (see headerBit()); mixing the two up silently corrupts memory, so the two
 * concepts are deliberately kept in different types.
 */
#define HEADER_LIST(X)                                    \
    X(Server,                 "Server")                   \
    X(Date,                   "Date")                     \
    X(ContentType,            "Content-Type")             \
    X(ContentLength,          "Content-Length")           \
    X(Connection,             "Connection")               \
    X(UserAgent,              "User-Agent")               \
    X(Accept,                 "Accept")                   \
    X(AcceptEncoding,         "Accept-Encoding")          \
    X(Host,                   "Host")                     \
    X(Authorization,          "Authorization")            \
    X(CacheControl,           "Cache-Control")            \
    X(TransferEncoding,       "Transfer-Encoding")        \
    X(Expect,                 "Expect")                   \
    X(Allow,                  "Allow")                    \
    X(Upgrade,                "Upgrade")                  \
    X(SecWebSocketKey,        "Sec-WebSocket-Key")        \
    X(SecWebSocketVersion,    "Sec-WebSocket-Version")    \
    X(SecWebSocketProtocol,   "Sec-WebSocket-Protocol")   \
    X(SecWebSocketAccept,     "Sec-WebSocket-Accept")

enum WARP_API HeaderType : u32
{
#define X(name, str) name,
    HEADER_LIST(X)
#undef X
    HeaderCount,
    // Returned by the parser for any header we do not track.
    HeaderUnknown = HeaderCount
};

#define MAX_HEADERS_SIZE (static_cast<usize>(HeaderType::HeaderCount))

inline constexpr std::array<std::string_view, MAX_HEADERS_SIZE> HeaderStrings = {
#define X(name, str) std::string_view(str),
    HEADER_LIST(X)
#undef X
};

/** Bitmask of the headers present in a message: one bit per HeaderType ordinal. */
using HeaderMask = u32;

static_assert(HeaderType::HeaderCount <= 32,
              "HeaderMask is a u32; adding more than 32 headers needs a wider mask type");

inline constexpr HeaderMask headerBit(HeaderType h) noexcept
{
    return (h < HeaderType::HeaderCount) ? (HeaderMask{1} << static_cast<u32>(h)) : HeaderMask{0};
}

inline constexpr HeaderMask headerBits() noexcept { return 0; }

template <typename... Rest>
inline constexpr HeaderMask headerBits(HeaderType first, Rest... rest) noexcept
{
    return headerBit(first) | headerBits(rest...);
}

inline constexpr bool hasHeader(HeaderMask flags, HeaderMask required) noexcept
{
    return (flags & required) == required;
}

#endif // HEADERSLIST_H
