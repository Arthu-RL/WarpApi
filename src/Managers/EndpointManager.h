#ifndef ENDPOINTMANAGER_H
#define ENDPOINTMANAGER_H

#pragma once

#include <vector>

#include "Endpoint/Endpoint.h"
#include "Managers/RouteTable.h"

using EndpointTable = std::array<RouteTable<Endpoint*>, Method::UNKNOWN + 1>;

/**
 * @class EndpointManager
 * @brief Route registry: one frozen hash index per HTTP method.
 *
 * @note Registration happens at startup, before any worker thread exists, and
 *       is sealed by freeze(). Afterwards every table is strictly read-only,
 *       which is what makes concurrent lock-free lookups from every worker
 *       safe without so much as an atomic.
 */
class WARP_API EndpointManager
{
public:
    EndpointManager();
    ~EndpointManager();

    static EndpointManager* getInstance();

    /** @note Takes ownership of @p route. Must be called before freeze(). */
    void registerEndpoint(Endpoint* route);
    /** @note Takes ownership of @p wsRoute. Must be called before freeze(). */
    void registerWebSocketEndpoint(const std::string& route, WebSocketRoute* wsRoute);

    /** Seals the registry and builds the probe arrays. Call once, at startup. */
    void freeze();

    Endpoint* getEndpoint(Method method, std::string_view route) const noexcept
    {
        if (static_cast<usize>(method) >= _endpoints_map.size()) [[unlikely]]
            return nullptr;

        return _endpoints_map[method].lookup(route);
    }

    WebSocketRoute* getWebSocketEndpoint(std::string_view route) const noexcept
    {
        return _wsEndpoints.lookup(route);
    }

    /** @brief Number of registered routes (HTTP + WebSocket). */
    u32 count() const;

private:
    EndpointTable _endpoints_map;
    RouteTable<WebSocketRoute*> _wsEndpoints;

    std::vector<Endpoint*> _ownedEndpoints;
    std::vector<WebSocketRoute*> _ownedWsRoutes;

    bool _frozen = false;
};

#endif // ENDPOINTMANAGER_H
