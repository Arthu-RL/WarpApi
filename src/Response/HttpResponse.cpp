#include "HttpResponse.h"

#include <ctime>

namespace {

// One cache per worker thread: no sharing, no atomics, no false sharing.
struct DateCache {
    time_t second = 0;
    char date[30] = {};
    char okPrefix[192] = {};
    usize okPrefixLen = 0;
    char dateHeader[64] = {};
    usize dateHeaderLen = 0;

    void refresh(time_t now)
    {
        second = now;
        StringUtils::formatHttpDate(date, now);

        // "HTTP/1.1 200 OK\r\nServer: ...\r\nDate: ...\r\nContent-Type: ...\r\n"
        char* p = okPrefix;
        auto put = [&p](std::string_view sv) {
            std::memcpy(p, sv.data(), sv.size());
            p += sv.size();
        };

        put(HTTP_VERSION " 200 OK\r\n");
        put(HeaderStrings[HeaderType::Server]);
        put(": " APP_INFO_HEADER "\r\n");
        put(HeaderStrings[HeaderType::Date]);
        put(": ");
        put(std::string_view(date, 29));
        put("\r\n");
        put(HeaderStrings[HeaderType::ContentType]);
        put(": " DEFAULT_CONTENT_TYPE "\r\n");
        okPrefixLen = static_cast<usize>(p - okPrefix);

        char* d = dateHeader;
        auto putD = [&d](std::string_view sv) {
            std::memcpy(d, sv.data(), sv.size());
            d += sv.size();
        };
        putD(HeaderStrings[HeaderType::Date]);
        putD(": ");
        putD(std::string_view(date, 29));
        putD("\r\n");
        dateHeaderLen = static_cast<usize>(d - dateHeader);
    }

    void ensureFresh()
    {
        const time_t now = ::time(nullptr);
        if (now != second)
            refresh(now);
    }
};

thread_local DateCache t_dateCache;

constexpr std::string_view kKeepAliveLine = "Connection: keep-alive\r\n";
constexpr std::string_view kCloseLine = "Connection: close\r\n";
constexpr std::string_view kContentLengthPrefix = "Content-Length: ";

} // namespace

std::string_view ResponsePrelude::commonOk() noexcept
{
    t_dateCache.ensureFresh();
    return std::string_view(t_dateCache.okPrefix, t_dateCache.okPrefixLen);
}

std::string_view ResponsePrelude::dateLine() noexcept
{
    t_dateCache.ensureFresh();
    return std::string_view(t_dateCache.dateHeader, t_dateCache.dateHeaderLen);
}

std::string_view HttpResponse::getStatusString(i32 status) noexcept
{
    static constexpr auto statusMap = [] {
        std::array<std::string_view, 512> arr{};
        arr[100] = "100 Continue";
        arr[101] = "101 Switching Protocols";
        arr[102] = "102 Processing";
        arr[200] = "200 OK";
        arr[201] = "201 Created";
        arr[202] = "202 Accepted";
        arr[203] = "203 Non-Authoritative Information";
        arr[204] = "204 No Content";
        arr[205] = "205 Reset Content";
        arr[206] = "206 Partial Content";
        arr[300] = "300 Multiple Choices";
        arr[301] = "301 Moved Permanently";
        arr[302] = "302 Found";
        arr[303] = "303 See Other";
        arr[304] = "304 Not Modified";
        arr[305] = "305 Use Proxy";
        arr[307] = "307 Temporary Redirect";
        arr[308] = "308 Permanent Redirect";
        arr[400] = "400 Bad Request";
        arr[401] = "401 Unauthorized";
        arr[402] = "402 Payment Required";
        arr[403] = "403 Forbidden";
        arr[404] = "404 Not Found";
        arr[405] = "405 Method Not Allowed";
        arr[406] = "406 Not Acceptable";
        arr[407] = "407 Proxy Authentication Required";
        arr[408] = "408 Request Timeout";
        arr[409] = "409 Conflict";
        arr[410] = "410 Gone";
        arr[411] = "411 Length Required";
        arr[412] = "412 Precondition Failed";
        arr[413] = "413 Payload Too Large";
        arr[414] = "414 URI Too Long";
        arr[415] = "415 Unsupported Media Type";
        arr[416] = "416 Range Not Satisfiable";
        arr[417] = "417 Expectation Failed";
        arr[426] = "426 Upgrade Required";
        arr[429] = "429 Too Many Requests";
        arr[431] = "431 Request Header Fields Too Large";
        arr[500] = "500 Internal Server Error";
        arr[501] = "501 Not Implemented";
        arr[502] = "502 Bad Gateway";
        arr[503] = "503 Service Unavailable";
        arr[504] = "504 Gateway Timeout";
        arr[505] = "505 HTTP Version Not Supported";
        return arr;
    }();

    if (status >= 0 && status < static_cast<i32>(statusMap.size()) && !statusMap[status].empty())
        return statusMap[status];

    return "500 Internal Server Error";
}

void HttpResponse::emitCustomHeaders()
{
    ByteBuffer& out = *_data.body;

    for (u32 i = 0; i < _data.header_count; ++i)
    {
        const HeaderType key = _data.active_headers[i];

        // Content-Length and Connection are emitted by us, from authoritative
        // state, so a handler can never desynchronise the framing.
        if (key == HeaderType::ContentLength || key == HeaderType::Connection)
            continue;

        out.append(HeaderStrings[key]);
        out.append(": ", 2);
        out.append(_data.headers[key]);
        out.append("\r\n", 2);
    }
}

bool HttpResponse::setBody(std::string_view body)
{
    if (_committed || _data.body == nullptr)
        return false;

    _committed = true;

    ByteBuffer& out = *_data.body;

    char numBuf[24];
    const std::string_view lenStr = StringUtils::fast_itoa(numBuf, sizeof(numBuf), body.size());

    // Reserve the exact worst case once, so the append chain below cannot fail
    // halfway and leave a truncated message framed on the wire.
    usize headerBytes = 0;
    for (u32 i = 0; i < _data.header_count; ++i)
    {
        const HeaderType key = _data.active_headers[i];
        if (key == HeaderType::ContentLength || key == HeaderType::Connection)
            continue;
        headerBytes += HeaderStrings[key].size() + _data.headers[key].size() + 4;
    }

    const usize needed = 256 + headerBytes + lenStr.size() + kKeepAliveLine.size() +
                         (_headOnly ? 0 : body.size());

    if (!out.ensureWritable(needed))
    {
        _failed = true;
        return false;
    }

    const bool plainOk = (_data.status == StatusCode::ok) &&
                         ((_data.present & headerBit(HeaderType::ContentType)) == 0) &&
                         ((_data.present & headerBit(HeaderType::Server)) == 0) &&
                         ((_data.present & headerBit(HeaderType::Date)) == 0);

    if (plainOk)
    {
        // Fast path: one blit for status line + Server + Date + Content-Type.
        out.append(ResponsePrelude::commonOk());
    }
    else
    {
        out.append(_data.version.empty() ? std::string_view(HTTP_VERSION) : _data.version);
        out.appendByte(' ');
        out.append(getStatusString(_data.status));
        out.append("\r\n", 2);

        if ((_data.present & headerBit(HeaderType::Server)) == 0)
        {
            out.append(HeaderStrings[HeaderType::Server]);
            out.append(": " APP_INFO_HEADER "\r\n");
        }
        if ((_data.present & headerBit(HeaderType::Date)) == 0)
            out.append(ResponsePrelude::dateLine());
        if ((_data.present & headerBit(HeaderType::ContentType)) == 0)
        {
            out.append(HeaderStrings[HeaderType::ContentType]);
            out.append(": " DEFAULT_CONTENT_TYPE "\r\n");
        }
    }

    emitCustomHeaders();

    out.append(_keepAlive ? kKeepAliveLine : kCloseLine);

    out.append(kContentLengthPrefix);
    out.append(lenStr);
    out.append("\r\n\r\n", 4);

    if (!_headOnly && !body.empty())
        out.append(body);

    return true;
}
