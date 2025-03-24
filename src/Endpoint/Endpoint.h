#ifndef ENDPOINT_H
#define ENDPOINT_H

#pragma once

#include "WarpDefs.h"
#include "Utils/RouteIdentifier.h"

/**
 * The Endpoint class represents a single API endpoint.
 * Each endpoint is associated with a unique route and can process incoming requests using a provided ResponseManager.
 */
class Endpoint {
public:
    explicit Endpoint(const std::string& route, const Method method) :
        _route(route), _method(method)
    {
        // Empty
    }
    ~Endpoint() = default;

    void setHandlerCallback(RequestHandler handlerCallback)
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

    void exec(HttpRequest& req, HttpResponse& responseManager)
    {
        _handlerCallBack(req, responseManager);
    }

protected:
    std::string _route;
    Method _method;

    RequestHandler _handlerCallBack;
};


#endif // ENDPOINT_H
