#ifndef HTTPREQUEST_H
#define HTTPREQUEST_H

#pragma once

#include <unordered_map>

#include "WarpDefs.h"
#include "Utils/Conversions.h"

struct WARP_API RequestData {
    RequestData() :
        method(Method::UNKNOWN),
        path(""),
        version("HTTP/2.0"),
        headers({}),
        body(""),
        queryParams({}) {}

    Method method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    std::unordered_map<std::string, std::string> queryParams;

    void clear()
    {
        method = Method::UNKNOWN;
        path.clear();
        version.clear();
        body.clear();

        headers.clear();
        queryParams.clear();
    }
};

class WARP_API HttpRequest {
public:


    explicit HttpRequest() :
        _data()
    {
        // _data.keep_alive(false);
    }

    static Method parseMethod(const std::string_view& method)
    {
        if (method == "GET") return GET;
        if (method == "POST") return POST;
        if (method == "PUT") return PUT;
        if (method == "DELETE") return DELETE;
        if (method == "HEAD") return HEAD;
        if (method == "OPTIONS") return OPTIONS;
        return UNKNOWN;
    }

    const Method method() const noexcept
    {
        return _data.method;
    }

    void setMethod(Method method)
    {
        _data.method = method;
    }

    const std::string& path() const noexcept
    {
        return _data.path;
    }

    void setPath(const std::string_view& path)
    {
        _data.path = std::string(path);
    }

    const std::string& body() const noexcept
    {
        return _data.body;
    }

    void setBody(const std::string_view& buffer)
    {
        _data.body = std::string(buffer);
    }

    // void appendToBody(const std::string& buffer)
    // {
    //     _data.body.append(buffer);
    // }

    const std::unordered_map<std::string, std::string>& headers() const noexcept
    {
        return _data.headers;
    }

    void addHeader(const std::string& key, const std::string& value)
    {
        _data.headers[key] = value;
    }

    const bool hasHeader(const std::string& key) const noexcept
    {
        return _data.headers.find(key) != _data.headers.end();
    }

    const std::string& getHeader(const std::string& key, const std::string& dvalue = "") const noexcept
    {
        auto it = _data.headers.find(key);
        if (it != _data.headers.end())
        {
            return it->second;
        }
        return dvalue;
    }

    const std::unordered_map<std::string, std::string>& queryParams() const noexcept
    {
        return _data.queryParams;
    }

    void addQueryParams(const std::string& key, const std::string& value)
    {
        std::string target = _data.path;
        const char delimiter = (target.find('?') == std::string::npos) ? '?' : '&';
        target += delimiter + Conversions::urlEncode(key) + "=" + Conversions::urlEncode(value);
        _data.path = target;
    }

    void extractQueryParams()
    {
        std::string target = _data.path;

        size_t queryStart = target.find('?');
        if (queryStart == std::string::npos)
            return;

        std::string query = target.substr(queryStart+1);

        std::string key, value;
        std::istringstream queryStream(query);
        while (std::getline(queryStream, key, '&'))
        {
            std::size_t equalPos = key.find('=');
            if (equalPos != std::string::npos)
            {
                value = Conversions::urlDecode(key.substr(equalPos + 1));
                key = Conversions::urlDecode(key.substr(0, equalPos));
                _data.queryParams[key] = value;
            }
            else
            {
                value.clear();
            }
        }

        target = target.substr(0, queryStart);
        _data.path = target;
    }

    // bool isChunked() const
    // {
    //     return _req.chunked();
    // }

    // bool isKeepAlive() const
    // {
    //     return _req.keep_alive();
    // }

    const std::string version() const noexcept
    {
        return _data.version;
    }

    void setVersion(const std::string_view& version)
    {
        _data.version = std::string(version);
    }

    const RequestData& getRequestData() const noexcept
    {
        return _data;
    }

    // void pPayload()
    // {
    //     _req.prepare_payload();
    // }

    void reset()
    {
        _data.clear();
    }

private:
    RequestData _data;
};

#endif // REQUESTMANAGER_H
