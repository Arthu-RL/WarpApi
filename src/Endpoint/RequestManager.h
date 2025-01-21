#ifndef REQUESTMANAGER_H
#define REQUESTMANAGER_H

#pragma once

#include <boost/beast.hpp>
#include <string>

namespace beast = boost::beast;
namespace http = beast::http;

template <typename BodyType = http::string_body>
class RequestManager {
public:
    explicit RequestManager() :
        _req()
    {
        // Empty
    }

    void setBody(const typename BodyType::value_type& buffer)
    {
        _req.body() = buffer;
    }

    void setHeader(boost::beast::http::field key, const std::string& value)
    {
        _req.set(key, value);
    }

    void setHeader(const std::string& key, const std::string& value)
    {
        _req.set(key, value);
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

private:
    http::request<BodyType> _req;
};

#endif // REQUESTMANAGER_H
