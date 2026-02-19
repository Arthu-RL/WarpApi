#ifndef HEADERSLIST_H
#define HEADERSLIST_H

// Common headers
#define APP_INFO_HEADER "WarpApi/1.0"
#define KEEP_ALIVE_HEADER "keep-alive"
#define CLOSE_CONN_HEADER "close"

#include "WarpDefs.h"

#define HEADER_LIST(X) \
    X(Server, "Server") \
    X(ContentType, "Content-Type") \
    X(ContentLength, "Content-Length") \
    X(Connection, "Connection") \
    X(UserAgent, "User-Agent") \
    X(Accept, "Accept") \
    X(AcceptEncoding, "Accept-Encoding") \
    X(Host, "Host") \
    X(Authorization, "Authorization") \
    X(CacheControl, "Cache-Control")

enum WARP_API HeaderType : i32
{
#define X(name, str) name,
    HEADER_LIST(X)
#undef X
    COUNT
};

constexpr std::array<std::string_view, static_cast<size_t>(HeaderType::COUNT)> HeaderStrings = {
#define X(name, str) str,
        HEADER_LIST(X)
#undef X
};

#define MAX_HEADERS_SIZE static_cast<size_t>(HeaderType::COUNT)

struct Header {
    HeaderType key;
    std::string_view value;
};

#endif // HEADERSLIST_H
