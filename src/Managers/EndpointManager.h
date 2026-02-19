#ifndef ENDPOINTMANAGER_H
#define ENDPOINTMANAGER_H

#pragma once

#include <memory>
#include <ink/InkixTree.h>

#include "Endpoint/Endpoint.h"

using EndpointTable = std::array<ink::InkixTree<std::shared_ptr<Endpoint>>, Method::UNKNOWN + 1>;

class WARP_API EndpointManager
{
public:
    EndpointManager();
    ~EndpointManager();

    static EndpointManager* getInstance();

    void registerEndpoint(std::shared_ptr<Endpoint> endpoint);

    Endpoint* getEndpoint(const Method& method, const std::string_view& route);

    uint count();

private:
    EndpointTable _endpoints_map;
};

#endif // ENDPOINTMANAGER_H
