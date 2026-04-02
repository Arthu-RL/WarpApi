#ifndef HTTPRESPONSE_H
#define HTTPRESPONSE_H

#pragma once

#include <array>
#include <ink/RingBuffer.h>

#include "Utils/StringUtils.h"
#include "Utils/HeadersList.h"

inline bool writeAll(ink::RingBuffer& rb, const char* data, size_t len)
{
    size_t written = 0;
    while (written < len) {
        size_t n = rb.write(data + written, len - written);
        if (n == 0)
            return false;
        written += n;
    }
    return true;
}

struct WARP_API HttpResponseData {
    HttpResponseData() :
        status(StatusCode::ok),
        version(""),
        headers({}),
        body(nullptr) {}

    i32 status;
    std::string_view version;
    std::array<std::string_view, MAX_HEADERS_SIZE> headers;
    // Tracks which headers are active
    std::array<HeaderType, MAX_HEADERS_SIZE> active_headers;
    u32 header_count = 0;

    ink::RingBuffer* body;
};

class WARP_API HttpResponse
{
public:
    HttpResponse() : _data() {}

    int getStatus() { return _data.status; }
    void setStatus(int status) { _data.status = status; }
    void setVersion(const std::string_view version) { _data.version = version; }
    void addHeader(const HeaderType key, const std::string_view& value) {
        if (_data.headers[key].empty())
        {
            _data.active_headers[_data.header_count++] = key;
        }

        _data.headers[key] = value;
    }
    void initBody(ink::RingBuffer* writeBufferPtr) { _data.body = writeBufferPtr; }
    void setBody(const std::string_view body)
    {
        char numBuf[24];
        ink::RingBuffer& out = *_data.body;

        auto write = [&](std::string_view sv)
        {
            return writeAll(out, sv.data(), sv.size());
        };

        // Status line
        write(_data.version);
        write(" ");
        write(getStatusString(_data.status));
        write("\r\n");

        // Headers
        for (u32 i = 0; i < _data.header_count; ++i)
        {
            HeaderType key = _data.active_headers[i];

            // Skip ContentLength so we never accidentally print it twice
            if (key == HeaderType::ContentLength) continue;

            // No need to check if empty anymore, we KNOW it's populated
            write(HeaderStrings[key]);
            write(": ");
            write(_data.headers[key]);
            write("\r\n");
        }

        // Write ContentLength explicitily
        write(HeaderStrings[HeaderType::ContentLength]);
        write(": ");
        write(StringUtils::fast_itoa(numBuf, sizeof(numBuf), body.length()));
        // headers sep
        write("\r\n\r\n");

        // body
        write(body);
    }

private:
    HttpResponseData _data;

    std::string_view getStatusString(int status) const {
        static const std::array<std::string_view, 506> statusMap = []{
            std::array<std::string_view, 506> arr = {};
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
            arr[429] = "429 Too Many Requests";
            arr[500] = "500 Internal Server Error";
            arr[501] = "501 Not Implemented";
            arr[502] = "502 Bad Gateway";
            arr[503] = "503 Service Unavailable";
            arr[504] = "504 Gateway Timeout";
            arr[505] = "505 HTTP Version Not Supported";
            return arr;
        }();

        // Using std::string_view allows us to just check .empty()
        if (status >= 0 && status < static_cast<int>(statusMap.size()) && !statusMap[status].empty()) {
            return statusMap[status];
        }

        return "500 Internal Server Error"; // fallback
    }
};

#endif // HTTPRESPONSE_H
