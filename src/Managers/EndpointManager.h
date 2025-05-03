#ifndef ENDPOINTMANAGER_H
#define ENDPOINTMANAGER_H

#pragma once

#include <memory>

#include "Endpoint/Endpoint.h"

typedef std::unordered_map<std::string, std::shared_ptr<Endpoint>> EndpointTable;

class WARP_API EndpointManager
{
public:
    EndpointManager() = default;
    ~EndpointManager() = default;

    static void registerEndpoint(std::shared_ptr<Endpoint> endpoint);

    static std::shared_ptr<Endpoint> getEndpoint(const std::string& endpoint_id);

    static uint count();

private:
    static EndpointTable _endpoints_map;
};

#endif // ENDPOINTMANAGER_H
