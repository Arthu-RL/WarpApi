#ifndef ENDPOINTMANAGER_H
#define ENDPOINTMANAGER_H

#pragma once

#include <memory>
#include <tbb/concurrent_unordered_map.h>

#include "Endpoint/Endpoint.h"

class EndpointManager
{
public:
    EndpointManager() = default;
    ~EndpointManager() = default;

    static void registerEndpoint(std::shared_ptr<Endpoint> endpoint);

    static std::shared_ptr<Endpoint> getEndpoint(const std::string& endpoint_id);

    static uint count();

private:
    static tbb::concurrent_unordered_map<std::string, std::shared_ptr<Endpoint>> _endpoints_map;
};

#endif // ENDPOINTMANAGER_H
