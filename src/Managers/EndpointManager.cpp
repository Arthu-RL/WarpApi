#include "EndpointManager.h"

#include <stdexcept>

EndpointManager::EndpointManager() = default;

EndpointManager::~EndpointManager()
{
    for (Endpoint* e : _ownedEndpoints)
        delete e;
    for (WebSocketRoute* r : _ownedWsRoutes)
        delete r;
}

EndpointManager* EndpointManager::getInstance()
{
    static EndpointManager instance;
    return &instance;
}

void EndpointManager::registerEndpoint(Endpoint* endpoint)
{
    if (_frozen)
    {
        delete endpoint;
        throw std::runtime_error("Endpoints cannot be registered after the registry is frozen.");
    }

    if (!_endpoints_map[endpoint->getMethod()].insert(endpoint->getRoute(), endpoint))
    {
        const std::string route(endpoint->getRoute());
        const auto method = static_cast<u32>(endpoint->getMethod());
        delete endpoint;
        throw std::runtime_error(
            "Endpoints with equivalent method and path are forbidden. Hint: " +
            std::to_string(method) + ':' + route);
    }

    _ownedEndpoints.push_back(endpoint);
}

void EndpointManager::registerWebSocketEndpoint(const std::string& route, WebSocketRoute* wsRoute)
{
    if (_frozen)
    {
        delete wsRoute;
        throw std::runtime_error("WebSocket routes cannot be registered after the registry is frozen.");
    }

    if (!_wsEndpoints.insert(route, wsRoute))
    {
        delete wsRoute;
        throw std::runtime_error("Duplicated websocket route: " + route);
    }

    _ownedWsRoutes.push_back(wsRoute);
}

void EndpointManager::freeze()
{
    if (_frozen)
        return;

    for (auto& table : _endpoints_map)
        table.freeze();
    _wsEndpoints.freeze();

    _frozen = true;
}

u32 EndpointManager::count() const
{
    usize total = _wsEndpoints.size();
    for (const auto& table : _endpoints_map)
        total += table.size();

    return static_cast<u32>(total);
}
