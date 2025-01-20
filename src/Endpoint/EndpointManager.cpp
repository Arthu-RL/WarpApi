#include "EndpointManager.h"
#include <plog/Log.h>

std::unordered_map<std::string, std::shared_ptr<Endpoint>> EndpointManager::_endpoints_map;
std::mutex EndpointManager::_mutex;

void EndpointManager::registerEndpoint(std::shared_ptr<Endpoint> endpoint)
{
    std::lock_guard<std::mutex> lock(_mutex);

    const std::string endpoint_id = endpoint->id();

    if (_endpoints_map.find(endpoint_id) != _endpoints_map.end())
        throw std::runtime_error("Endpoints with equivalent ids (path:method) are forbidden. Hint: "+endpoint_id);

    _endpoints_map[endpoint_id] = endpoint;
}

std::shared_ptr<Endpoint> EndpointManager::getEndpoint(const std::string& endpoint_id)
{
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _endpoints_map.find(endpoint_id);
    if (it != _endpoints_map.end())
        return it->second;

    return nullptr;
}

uint EndpointManager::count()
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _endpoints_map.size();
}
