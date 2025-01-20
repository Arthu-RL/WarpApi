#ifndef ENDPOINTMANAGER_H
#define ENDPOINTMANAGER_H

#pragma once

#include <memory>
#include <unordered_map>

#include "Endpoint.h"

class EndpointManager
{
public:
    EndpointManager() = default;
    ~EndpointManager() = default;

    static void registerEndpoint(std::shared_ptr<Endpoint> endpoint);

    static std::shared_ptr<Endpoint> getEndpoint(const std::string& endpoint_id);

    static uint count();

private:
    static std::unordered_map<std::string, std::shared_ptr<Endpoint>> _endpoints_map;
    static std::mutex _mutex;
};

#endif // ENDPOINTMANAGER_H
