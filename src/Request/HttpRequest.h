#ifndef HTTPREQUEST_H
#define HTTPREQUEST_H

#pragma once

#include <unordered_map>

#include "WarpDefs.h"
#include "Utils/Conversions.h"
#include "Utils/HeadersList.h"

struct WARP_API RequestData {
    RequestData() :
        method(Method::UNKNOWN),
        path(""),
        version("HTTP/2.0"),
        body(""),
        queryParams({}) {}

    Method method;
    std::string_view path;
    std::string_view query;
    std::string_view version;
    std::string_view body;
    std::array<std::string_view, MAX_HEADERS_SIZE> headers;

    std::unordered_map<std::string, std::string> queryParams;

    void clear() noexcept
    {
        method = Method::UNKNOWN;
        path = {};
        query = {};
        version = {};
        body = {};
        headers.fill({});
    }
};

class WARP_API HttpRequest {
public:

    explicit HttpRequest() :
        _data()
    {
        // _data.keep_alive(false);
    }

    static Method parseMethod(std::string_view m) noexcept
    {
        if (m.empty())
            return UNKNOWN;

        switch (m[0])
        {
            case 'G':
                return GET;
                break;

            case 'P':
                switch (m.size())
                {
                    case 3: return PUT; break;
                    case 4: return POST; break;
                    case 5: return PATCH; break;
                }
                break;

            case 'H':
                return HEAD;
                break;

            case 'D':
                return DELETE;
                break;

            case 'O':
                return OPTIONS;
                break;
        }

        return UNKNOWN;
    }

    Method method() const noexcept
    {
        return _data.method;
    }

    void setMethod(Method method) noexcept
    {
        _data.method = method;
    }

    const std::string_view& path() const noexcept
    {
        return _data.path;
    }

    void setPath(const std::string_view& path, const std::string_view& query)
    {
        _data.path = path;
        _data.query = query;
    }

    const std::string_view body() const noexcept
    {
        return _data.body;
    }

    void setBody(const std::string_view& buffer) noexcept
    {
        _data.body = buffer;
    }

    // void appendToBody(const std::string& buffer)
    // {
    //     _data.body.append(buffer);
    // }

    const std::array<std::string_view, MAX_HEADERS_SIZE>& headers() const noexcept
    {
        return _data.headers;
    }

    void addHeader(const HeaderType& key, const char* v, const size_t vLen) noexcept
    {
        if (key != HeaderType::COUNT)
            _data.headers[key] = std::string_view(v, vLen);
    }

    const std::string_view getHeader(const HeaderType& key) const noexcept
    {
        if (key < HeaderType::COUNT)
            return _data.headers[key];

        return {};
    }

    const std::unordered_map<std::string, std::string>& queryParams() const noexcept
    {
        return _data.queryParams;
    }

    void extractQueryParams()
    {
        std::string_view target = _data.query;
        while (!target.empty())
        {
            auto amp = target.find('&');
            std::string_view part = (amp == std::string_view::npos) ? target : target.substr(0, amp);
            target = (amp == std::string_view::npos) ? std::string_view{} : target.substr(amp + 1);

            auto eq = part.find('=');
            std::string_view k = (eq == std::string_view::npos) ? part : part.substr(0, eq);
            std::string_view v = (eq == std::string_view::npos) ? std::string_view{} : part.substr(eq + 1);

            std::string key = Conversions::urlDecode(k);
            std::string value = Conversions::urlDecode(v);
            _data.queryParams[std::move(key)] = std::move(value);
        }
    }

    // bool isChunked() const
    // {
    //     return _req.chunked();
    // }

    // bool isKeepAlive() const
    // {
    //     return _req.keep_alive();
    // }

    const std::string_view version() const noexcept
    {
        return _data.version;
    }

    void setVersion(const std::string_view& version) noexcept
    {
        _data.version = version;
    }

    const RequestData& getRequestData() const noexcept
    {
        return _data;
    }

    // void pPayload()
    // {
    //     _req.prepare_payload();
    // }

    void reset() noexcept
    {
        _data.clear();
    }

private:
    RequestData _data;
};

#endif // REQUESTMANAGER_H
