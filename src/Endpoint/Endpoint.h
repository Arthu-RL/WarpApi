#ifndef ENDPOINT_H
#define ENDPOINT_H

#pragma once

#include <string>
#include <functional>

#include "ResponseManager.h"
#include "Utils/RouteIdentifier.h"

typedef std::function<void(ResponseManager<http::string_body>&)> RequestHandlerCallback;

/**
 * The Endpoint class represents a single API endpoint.
 * Each endpoint is associated with a unique route and can process incoming requests using a provided ResponseManager.
 */
class Endpoint {
public:
    explicit Endpoint(const std::string& route, http::verb method) :
        _route(route), _method(method)
    {
        // Empty
    }
    ~Endpoint() = default;

    void setHandlerCallback(RequestHandlerCallback handlerCallback)
    {
        _handlerCallBack = handlerCallback;
    }

    const std::string id() const
    {
        return RouteIdentifier::generateIdentifier(_route, _method);
    }

    const std::string& getRoute() const
    {
        return _route;
    };

    const bool isValid() const
    {
        return _route != "";
    }

    void exec(ResponseManager<http::string_body>& responseManager)
    {
        _handlerCallBack(responseManager);
    }

protected:
    std::string _route;
    http::verb _method;

    RequestHandlerCallback _handlerCallBack;;
};


#endif // ENDPOINT_H
