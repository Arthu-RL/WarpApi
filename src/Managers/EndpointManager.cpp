#include "EndpointManager.h"

EndpointManager::EndpointManager()
{

};

EndpointManager::~EndpointManager()
{

}

EndpointManager* EndpointManager::getInstance()
{
    static EndpointManager instance;
    return &instance;
}

void EndpointManager::registerEndpoint(Endpoint* endpoint)
{
    auto& tree = _endpoints_map[endpoint->getMethod()];

    if (tree.get(endpoint->getRoute()) != nullptr)
        throw std::runtime_error("Endpoints with equivalent method, or, path is forbidden. Hint: "+std::to_string((u32)endpoint->getMethod())+':'+std::string(endpoint->getRoute().data()));

    tree.insert(endpoint->getRoute(), endpoint);
}

void EndpointManager::registerWebSocketEndpoint(const std::string& route, WebSocketRoute* wsRoute)
{
    auto& tree = _wsEndpoints[0];
    if (tree.get(route) != nullptr)
        throw std::runtime_error("Duplicated websocket route: " + route);

    tree.insert(route, wsRoute);
}

Endpoint** EndpointManager::getEndpoint(const Method& method, const std::string_view& route)
{
    return _endpoints_map[method].get(route);
}

WebSocketRoute** EndpointManager::getWebSocketEndpoint(const std::string_view& route)
{
    return _wsEndpoints[0].get(route);
}

u32 EndpointManager::count() const
{
    return _endpoints_map.size()+_wsEndpoints.size();
}
