#include "Core/Router.h"

#include "Endpoint/Endpoint.h"
#include "Managers/EndpointManager.h"

namespace warp {

Router::Router(EndpointManager& manager, ServiceContainer& services) :
    _manager(manager), _services(services)
{
}

Router::Router(EndpointManager& manager, ServiceContainer& services, std::string prefix) :
    _manager(manager), _services(services), _prefix(std::move(prefix))
{
}

std::string Router::resolvePath(std::string_view path) const
{
    if (_prefix.empty())
        return std::string(path);

    std::string full = _prefix;

    // Normalize the seam so group("/api") + get("/users") and
    // group("/api/") + get("users") both yield "/api/users" rather than
    // "/api//users" or "/apiusers" - route matching is exact, so a stray
    // slash would silently produce an endpoint nobody can reach.
    const bool prefixEndsWithSlash = full.back() == '/';
    const bool pathStartsWithSlash = !path.empty() && path.front() == '/';

    if (prefixEndsWithSlash && pathStartsWithSlash)
        path.remove_prefix(1);
    else if (!prefixEndsWithSlash && !pathStartsWithSlash)
        full.push_back('/');

    full.append(path);

    // group("/api", ...) with get("/") means the group's own root: "/api",
    // not "/api/". Strip the trailing slash so callers do not have to guess.
    if (full.size() > 1 && full.back() == '/')
        full.pop_back();

    return full;
}

Router& Router::route(Method method, std::string_view path, RequestHandler handler)
{
    const std::string full = resolvePath(path);

    Endpoint* endpoint = new Endpoint(full, method);
    endpoint->setHandlerCallback(std::move(handler));
    _manager.registerEndpoint(endpoint); // takes ownership; throws on a duplicate

    return *this;
}

Router& Router::webSocket(std::string_view path,
                          WebSocketOpenHandler onOpen,
                          WebSocketMessageHandler onMessage,
                          WebSocketCloseHandler onClose)
{
    const std::string full = resolvePath(path);

    auto* route = new WebSocketRoute(std::move(onOpen), std::move(onMessage), std::move(onClose));
    _manager.registerWebSocketEndpoint(full, route);

    return *this;
}

Router& Router::group(std::string_view prefix, const std::function<void(Router&)>& build)
{
    Router scoped(_manager, _services, resolvePath(prefix));
    build(scoped);
    return *this;
}

} // namespace warp
