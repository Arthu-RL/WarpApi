#include "EndpointManager.h"

EndpointManager::EndpointManager() : _endpoints_map({})
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

void EndpointManager::registerEndpoint(std::shared_ptr<Endpoint> endpoint)
{
    auto& tree = _endpoints_map[endpoint->getMethod()];

    if (tree.get(endpoint->getRoute()) != nullptr)
        throw std::runtime_error("Endpoints with equivalent method, or, path is forbidden. Hint: "+std::to_string((u32)endpoint->getMethod())+':'+std::string(endpoint->getRoute().data()));

    tree.insert(endpoint->getRoute(), endpoint);
}

Endpoint* EndpointManager::getEndpoint(const Method& method, const std::string_view& route)
{
    return _endpoints_map[method].get(route)->get();
}

uint EndpointManager::count()
{
    return _endpoints_map.size();
}
