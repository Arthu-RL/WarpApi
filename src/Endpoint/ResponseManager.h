#ifndef RESPONSEMANAGER_H
#define RESPONSEMANAGER_H

#pragma once

#include <boost/beast.hpp>
#include <string>

namespace beast = boost::beast;
namespace http = beast::http;

template <typename BodyType = http::string_body>
class ResponseManager {
public:
    explicit ResponseManager(int version) :
        _res(http::status::ok, version)
    {
        applyDefaultHeaders();
    }

    void setBody(const typename BodyType::value_type& buffer)
    {
        _res.body() = buffer;
    }
    void setStatus(const http::status& status)
    {
        _res.result(status);
    }

    void setHeader(boost::beast::http::field key, const std::string& value)
    {
        _res.set(key, value);
    }

    void setHeader(const std::string& key, const std::string& value)
    {
        _res.set(key, value);
    }

    bool isChunked() const
    {
        return _res.chunked();
    }
    bool isKeepAlive() const
    {
        return _res.keep_alive();
    }

    http::response<BodyType>& getResponse()
    {
        return _res;
    }

    void pPayload()
    {
        _res.prepare_payload();
    }

private:
    void applyDefaultHeaders()
    {
        _res.set(http::field::content_type, "application/json");
        _res.set(http::field::server, "CustomServer/1.0");
        _res.set(http::field::cache_control, "no-cache, no-store, must-revalidate");
        _res.set(http::field::strict_transport_security, "max-age=31536000; includeSubDomains");
        _res.set(http::field::access_control_allow_origin, "*");
        _res.set("X-Content-Type-Options", "nosniff");
        _res.set("X-Frame-Options", "DENY");
        _res.set("X-XSS-Protection", "1; mode=block");
        _res.set("Referrer-Policy", "no-referrer");
        _res.set("Feature-Policy", "geolocation 'self'; camera 'none'");
    }

    http::response<BodyType> _res;
};

#endif // RESPONSEMANAGER_H
