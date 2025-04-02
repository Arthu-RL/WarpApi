#ifndef HTTPRESPONSE_H
#define HTTPRESPONSE_H

#pragma once

#include "WarpDefs.h"

struct WARP_API HttpResponseData {
    HttpResponseData() :
        status(StatusCode::ok),
        version("HTTP/2.0"),
        headers({}),
        body("") {}

    int status;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

class WARP_API HttpResponse
{
public:
    HttpResponse() : _data() {}

    void setStatus(int status) { _data.status = status; }
    void setVersion(const std::string& version) { _data.version = version; }
    void addHeader(const std::string& key, const std::string& value) {
        _data.headers[key] = value;
    }
    void setBody(const std::string& body) {
        _data.body = body;
        addHeader("Content-Length", std::to_string(_data.body.length()));
    }

    std::string toString() const {
        std::stringstream ss;
        ss << _data.version << " " << _data.status << " " << statusText(_data.status) << "\r\n";

        for (const auto& header : _data.headers) {
            ss << header.first << ": " << header.second << "\r\n";
        }

        ss << "\r\n" << _data.body;
        return ss.str();
    }

private:
    HttpResponseData _data;

    std::string statusText(int status) const {
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
