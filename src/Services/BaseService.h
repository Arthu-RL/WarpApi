#ifndef BASESERVICE_H
#define BASESERVICE_H

#pragma once

#include <string>

#include "Endpoint/Endpoint.h"
#include "Managers/EndpointManager.h"

class WARP_API BaseService
{
public:
    explicit BaseService() = default;
    virtual ~BaseService() = default;

    virtual void registerAllEndpoints() = 0;

    virtual void registerEndpoint(const std::string& route,
                                  const Method method,
                                  RequestHandler reqHandler)
    {
        Endpoint* endpoint = new Endpoint(route, method);
        endpoint->setHandlerCallback(reqHandler);
        EndpointManager::getInstance()->registerEndpoint(endpoint);
    }

    virtual void registerWebSocketEndpoint(const std::string& route,
                                           WebSocketRoute wsRoute)
    {
        WebSocketRoute* ws = new WebSocketRoute(std::move(wsRoute));
        EndpointManager::getInstance()->registerWebSocketEndpoint(route, ws);
    }
};

#endif // BASESERVICE_H
