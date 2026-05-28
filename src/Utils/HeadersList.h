#ifndef HEADERSLIST_H
#define HEADERSLIST_H

// Common headers
#define APP_INFO_HEADER "WarpApi/1.0"
#define KEEP_ALIVE_HEADER "keep-alive"
#define CLOSE_CONN_HEADER "close"
#define UPGRADE_HEADER "Upgrade"
#define WEBSOCKET_UPGRADE_HEADER "websocket"
#define WS_VERSION_13_HEADER "13"

#include "WarpDefs.h"
#include <array>
#include <string_view>

#define HEADER_LIST(X) \
    X(Server, "Server", 0) \
    X(ContentType, "Content-Type", 1) \
    X(ContentLength, "Content-Length", 2) \
    X(Connection, "Connection", 3) \
    X(UserAgent, "User-Agent", 4) \
    X(Accept, "Accept", 5) \
    X(AcceptEncoding, "Accept-Encoding", 6) \
    X(Host, "Host", 7) \
    X(Authorization, "Authorization", 8) \
    X(CacheControl, "Cache-Control", 9) \
    X(Upgrade, "Upgrade", 10) \
    X(SecWebSocketKey, "Sec-WebSocket-Key", 11) \
    X(SecWebSocketVersion, "Sec-WebSocket-Version", 12) \
    X(SecWebSocketAccept, "Sec-WebSocket-Accept", 13)

enum WARP_API HeaderType : i32
{
    None = 0,

#define X(name, str, bit) name = (1 << bit),
    HEADER_LIST(X)
#undef X
};

constexpr std::array<std::string_view, 14> HeaderStrings = {
#define X(name, str, bit) str,
    HEADER_LIST(X)
#undef X
};

#define MAX_HEADERS_SIZE HeaderStrings.size()

struct Header
{
    HeaderType key;
    std::string_view value;
};

inline constexpr HeaderType operator|(HeaderType lhs, HeaderType rhs)
{
    return static_cast<HeaderType>(
        static_cast<i32>(lhs) |
        static_cast<i32>(rhs)
        );
}

inline constexpr HeaderType operator&(HeaderType lhs, HeaderType rhs)
{
    return static_cast<HeaderType>(
        static_cast<i32>(lhs) &
        static_cast<i32>(rhs)
        );
}

inline HeaderType& operator|=(HeaderType& lhs, HeaderType rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

inline bool hasHeader(HeaderType flags, HeaderType required)
{
    return (flags & required) == required;
}

#endif // HEADERSLIST_H
