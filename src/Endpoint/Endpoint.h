#ifndef ENDPOINT_H
#define ENDPOINT_H

#pragma once

#include "WarpDefs.h"

/**
 * The Endpoint class represents a single API endpoint.
 * Each endpoint is associated with a unique route and can process incoming requests using a provided ResponseManager.
 */
class WARP_API Endpoint {
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

    const Method& getMethod() const
    {
        return _method;
    }

    const std::string_view getRoute() const
    {
        return _route;
    };

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
