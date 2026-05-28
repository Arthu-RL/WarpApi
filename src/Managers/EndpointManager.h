#ifndef ENDPOINTMANAGER_H
#define ENDPOINTMANAGER_H

#pragma once

#include <ink/InkixTree.h>

#include "Endpoint/Endpoint.h"

using EndpointTable = std::array<ink::InkixTree<Endpoint*>, Method::UNKNOWN + 1>;
using WebSocketEndpointTable = std::array<ink::InkixTree<WebSocketRoute*>, 1>;

class WARP_API EndpointManager
{
public:
    EndpointManager();
    ~EndpointManager();

    static EndpointManager* getInstance();

    void registerEndpoint(Endpoint* route);
    void registerWebSocketEndpoint(const std::string& route, WebSocketRoute* wsRoute);

    Endpoint** getEndpoint(const Method& method, const std::string_view& route);
    WebSocketRoute** getWebSocketEndpoint(const std::string_view& route);

    u32 count() const;

private:
    EndpointTable _endpoints_map;
    WebSocketEndpointTable _wsEndpoints;
};

#endif // ENDPOINTMANAGER_H
