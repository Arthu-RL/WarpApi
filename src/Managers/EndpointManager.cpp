#include "EndpointManager.h"

EndpointTable EndpointManager::_endpoints_map;

void EndpointManager::registerEndpoint(std::shared_ptr<Endpoint> endpoint)
{
    const std::string endpoint_id = endpoint->id();

    if (_endpoints_map.find(endpoint_id) != _endpoints_map.end())
        throw std::runtime_error("Endpoints with equivalent ids (path:method) are forbidden. Hint: "+endpoint_id);

    _endpoints_map[endpoint_id] = endpoint;
}

std::shared_ptr<Endpoint> EndpointManager::getEndpoint(const std::string& endpoint_id)
{
    const auto& it = _endpoints_map.find(endpoint_id);
    if (it != _endpoints_map.end())
        return it->second;

    return nullptr;
}

uint EndpointManager::count()
{
    return _endpoints_map.size();
}
