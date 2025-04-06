#ifndef BASESERVICE_H
#define BASESERVICE_H

#pragma once

#include <string>

#include "Endpoint/Endpoint.h"
#include "Managers/EndpointManager.h"

class WARP_API BaseService
{
public:
    explicit BaseService() :
        _serviceEndpointsCounter(0)
    {
        // Empty
    };
    virtual ~BaseService() = default;

    virtual void registerAllEndpoints() = 0;

    virtual void registerEndpoint(const std::string& route,
                                  const Method method,
                                  RequestHandler reqHandler)
    {
        std::shared_ptr<Endpoint> endpoint = std::make_shared<Endpoint>(route, method);
        endpoint->setHandlerCallback(reqHandler);
        EndpointManager::registerEndpoint(endpoint);
        _serviceEndpointsCounter++;
    }

    const uint counter() const
    {
        return _serviceEndpointsCounter;
    }
protected:
    uint _serviceEndpointsCounter;
};

#endif // BASESERVICE_H
