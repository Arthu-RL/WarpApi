#ifndef REQUESTMANAGER_H
#define REQUESTMANAGER_H

#pragma once

#include <boost/beast.hpp>
#include <string>
#include <unordered_map>

#include "Utils/Conversions.h"

namespace beast = boost::beast;
namespace http = beast::http;

template <typename BodyType = http::string_body>
class RequestManager {
public:
    explicit RequestManager() :
        _req(), _queryParams({})
    {
        _req.keep_alive(false);
    }

    void setBody(const typename BodyType::value_type& buffer)
    {
        _req.body() = buffer;
    }

    const std::string& getRequestBody()
    {
        return _req.body();
    }

    void setHeader(boost::beast::http::field key, const std::string& value)
    {
        _req.set(key, value);
    }

    void setHeader(const std::string& key, const std::string& value)
    {
        _req.set(key, value);
    }

    void setQueryParams(const std::string& key, const std::string& value)
    {
        std::string target = requestPath();
        const char delimiter = (target.find('?') == std::string::npos) ? '?' : '&';
        target += delimiter + Conversions::urlEncode(key) + "=" + Conversions::urlEncode(value);
        _req.target(target);
    }

    void extractQueryParams()
    {
        std::string target = requestPath();

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
                _queryParams[key] = value;
            }
            else
            {
                value.clear();
            }
        }

        target = target.substr(0, queryStart);
        _req.target(target);
    }

    const std::unordered_map<std::string, std::string>& getQueryParams()
    {
        return _queryParams;
    }

    bool isChunked() const
    {
        return _req.chunked();
    }
    bool isKeepAlive() const
    {
        return _req.keep_alive();
    }

    const uint getVersion() const
    {
        return _req.version();
    }

    const std::string requestPath() const
    {
        return _req.target().to_string();
    }

    const http::verb requestMethod() const
    {
        return _req.method();
    }

    http::request<BodyType>& getRequest()
    {
        return _req;
    }

    void pPayload()
    {
        _req.prepare_payload();
    }

    void reset()
    {
        _req.clear();
        _req.body().clear();
        _queryParams.clear();
    }

private:
    http::request<BodyType> _req;

    std::unordered_map<std::string, std::string> _queryParams;
};

#endif // REQUESTMANAGER_H
