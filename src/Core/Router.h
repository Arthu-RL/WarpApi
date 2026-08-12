#ifndef ROUTER_H
#define ROUTER_H

#include <functional>
#include <string>
#include <string_view>

#include "Core/ServiceContainer.h"
#include "WarpDefs.h"

class WARP_API EndpointManager;

namespace warp {

/**
 * @class Router
 * @brief Scoped route builder passed to route-configuration functions.
 *
 * The one way routes get declared in WarpApi. A Router is an ordinary object
 * with an ordinary lifetime: created by whoever is wiring the application,
 * passed to a plain function such as `void configureUserRoutes(Router&)`, and
 * discarded. Nothing about it is global, which is what makes a route set
 * possible to build twice in one binary — say, against a real database and
 * against a fake one for a test — and there is no interface to implement to
 * write one of those functions: a function taking `Router&` is already the
 * whole unit of composition, nothing to inherit from or instantiate.
 *
 * Its second job is structure. `group()` nests a prefix so a large API keeps
 * its shape in the source instead of repeating "/api/v1/..." on every line:
 *
 * @code
 * router.group("/api/v1", [](Router& v1) {
 *     v1.group("/users", [](Router& users) {
 *         users.get("/",     listUsers);
 *         users.post("/",    createUser);
 *         users.get("/me",   currentUser);
 *     });
 * });
 * @endcode
 *
 * @note Registers directly into EndpointManager, which is what makes a
 *       colliding path across two unrelated route-configuration functions
 *       fail loudly at startup instead of one silently overwriting the other.
 */
class WARP_API Router {
public:
    Router(EndpointManager& manager, ServiceContainer& services);

    /** @brief The application's services, for resolving dependencies. */
    ServiceContainer& services() noexcept { return _services; }

    Router& route(Method method, std::string_view path, RequestHandler handler);

    Router& get(std::string_view path, RequestHandler handler)     { return route(Method::GET, path, std::move(handler)); }
    Router& post(std::string_view path, RequestHandler handler)    { return route(Method::POST, path, std::move(handler)); }
    Router& put(std::string_view path, RequestHandler handler)     { return route(Method::PUT, path, std::move(handler)); }
    Router& patch(std::string_view path, RequestHandler handler)   { return route(Method::PATCH, path, std::move(handler)); }
    Router& del(std::string_view path, RequestHandler handler)     { return route(Method::DELETE, path, std::move(handler)); }
    Router& head(std::string_view path, RequestHandler handler)    { return route(Method::HEAD, path, std::move(handler)); }
    Router& options(std::string_view path, RequestHandler handler) { return route(Method::OPTIONS, path, std::move(handler)); }

    Router& webSocket(std::string_view path,
                      WebSocketOpenHandler onOpen,
                      WebSocketMessageHandler onMessage,
                      WebSocketCloseHandler onClose = {});

    /** @brief Runs @p build against a Router whose paths are prefixed by @p prefix. */
    Router& group(std::string_view prefix, const std::function<void(Router&)>& build);

    /** @brief The prefix currently in effect (empty at the top level). */
    std::string_view prefix() const noexcept { return _prefix; }

private:
    Router(EndpointManager& manager, ServiceContainer& services, std::string prefix);

    /** Joins the active prefix with @p path, normalizing the slash between them. */
    std::string resolvePath(std::string_view path) const;

    EndpointManager& _manager;
    ServiceContainer& _services;
    std::string _prefix;
};

} // namespace warp

#endif // ROUTER_H
