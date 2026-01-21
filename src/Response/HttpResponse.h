#ifndef HTTPRESPONSE_H
#define HTTPRESPONSE_H

#pragma once

#include <charconv>
#include <array>
#include <ink/RingBuffer.h>

#include "WarpDefs.h"

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

    int status;
    std::string_view version;
    std::array<Header, MAX_HEADERS_SIZE> headers;
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
    void addHeader(const std::string& key, const std::string& value) {
        if (_data.header_count >= _data.headers.size()) throw std::out_of_range("Too many headers");
        _data.headers[_data.header_count++] = Header{key, value};
    }
    void initBody(ink::RingBuffer* writeBufferPtr) { _data.body = writeBufferPtr; }
    void setBody(const std::string_view body)
    {
        addHeader("Content-Length", std::to_string(body.length()));

        ink::RingBuffer& out = *_data.body;

        auto write = [&](std::string_view sv) {
            return writeAll(out, sv.data(), sv.size());
        };

        char statusBuf[4];
        auto [ptr, ec] = std::to_chars(statusBuf, statusBuf + sizeof(statusBuf), _data.status);
        size_t statusLen = ptr - statusBuf;

        // Status line
        write(_data.version);
        write(" ");
        write({statusBuf, statusLen});
        write(" ");
        write(statusText(_data.status));
        write("\r\n");

        // Headers
        for (u32 i = 0; i < _data.header_count; ++i) {
            const Header& h = _data.headers[i];
            write(h.key);
            write(": ");
            write(h.value);
            write("\r\n");
        }

        // Separator + body
        write("\r\n");
        write(body);
    }

private:
    HttpResponseData _data;

    std::string_view statusText(int status) const {
        static const std::array<const char*, 506> statusMap = []{
            std::array<const char*, 506> arr = {};
            arr[100] = "Continue";
            arr[101] = "Switching Protocols";
            arr[102] = "Processing";
            arr[200] = "OK";
            arr[201] = "Created";
            arr[202] = "Accepted";
            arr[203] = "Non-Authoritative Information";
            arr[204] = "No Content";
            arr[205] = "Reset Content";
            arr[206] = "Partial Content";
            arr[300] = "Multiple Choices";
            arr[301] = "Moved Permanently";
            arr[302] = "Found";
            arr[303] = "See Other";
            arr[304] = "Not Modified";
            arr[305] = "Use Proxy";
            arr[307] = "Temporary Redirect";
            arr[308] = "Permanent Redirect";
            arr[400] = "Bad Request";
            arr[401] = "Unauthorized";
            arr[402] = "Payment Required";
            arr[403] = "Forbidden";
            arr[404] = "Not Found";
            arr[405] = "Method Not Allowed";
            arr[406] = "Not Acceptable";
            arr[407] = "Proxy Authentication Required";
            arr[408] = "Request Timeout";
            arr[409] = "Conflict";
            arr[410] = "Gone";
            arr[411] = "Length Required";
            arr[412] = "Precondition Failed";
            arr[413] = "Payload Too Large";
            arr[414] = "URI Too Long";
            arr[415] = "Unsupported Media Type";
            arr[416] = "Range Not Satisfiable";
            arr[417] = "Expectation Failed";
            arr[429] = "Too Many Requests";
            arr[500] = "Internal Server Error";
            arr[501] = "Not Implemented";
            arr[502] = "Bad Gateway";
            arr[503] = "Service Unavailable";
            arr[504] = "Gateway Timeout";
            arr[505] = "HTTP Version Not Supported";
            return arr;
        }();

        if (status >= 0 && status < static_cast<int>(statusMap.size()) && statusMap[status] != nullptr) {
            return statusMap[status];
        }

        return "Unknown";
    }
};

#endif // HTTPRESPONSE_H
