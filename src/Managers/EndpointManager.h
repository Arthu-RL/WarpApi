#ifndef ENDPOINTMANAGER_H
#define ENDPOINTMANAGER_H

#pragma once

#include <vector>

#include "Endpoint/Endpoint.h"
#include "Managers/PatternRoutes.h"
#include "Managers/RouteTable.h"

using EndpointTable = std::array<RouteTable<Endpoint*>, Method::UNKNOWN + 1>;
using PatternTable = std::array<warp::PatternRoutes<Endpoint*>, Method::UNKNOWN + 1>;

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

    /**
     * @brief Resolves a route, filling @p req 's path params on a pattern hit.
     *
     * Exact routes are probed first and answer in O(1); only a miss falls
     * through to pattern matching, so adding `:id` routes costs the exact
     * paths nothing.
     */
    Endpoint* getEndpoint(Method method, std::string_view route,
                          HttpRequest* req = nullptr) const noexcept
    {
        if (static_cast<usize>(method) >= _endpoints_map.size()) [[unlikely]]
            return nullptr;

        if (Endpoint* exact = _endpoints_map[method].lookup(route))
            return exact;

        return _patterns[method].match(route, req);
    }

    /**
     * @brief Methods that would match @p route, as a bitmask of (1 << Method).
     *
     * Only consulted when a lookup already missed, so the loop over methods
     * is off the hot path. Lets the caller answer 405 with a correct Allow
     * header instead of a misleading 404.
     */
    u32 allowedMethods(std::string_view route) const noexcept
    {
        u32 mask = 0;
        for (u32 m = 0; m < static_cast<u32>(Method::UNKNOWN); ++m)
        {
            if (_endpoints_map[m].lookup(route) || _patterns[m].match(route, nullptr))
                mask |= (1u << m);
        }
        return mask;
    }

    WebSocketRoute* getWebSocketEndpoint(std::string_view route) const noexcept
    {
        return _wsEndpoints.lookup(route);
    }

    /** @brief Number of registered routes (HTTP + WebSocket). */
    u32 count() const;

private:
    EndpointTable _endpoints_map;
    PatternTable _patterns;
    RouteTable<WebSocketRoute*> _wsEndpoints;

    std::vector<Endpoint*> _ownedEndpoints;
    std::vector<WebSocketRoute*> _ownedWsRoutes;

    bool _frozen = false;
};

#endif // ENDPOINTMANAGER_H
