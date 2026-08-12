#ifndef SERVICECONTAINER_H
#define SERVICECONTAINER_H

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Core/TypeId.h"
#include "WarpDefs.h"

namespace warp {

/**
 * @class ServiceContainer
 * @brief Owns the application's long-lived services and hands them to routes.
 *
 * Two operations, because that is all a route ever needs: build a service
 * once at startup, then resolve a reference to it while wiring routes.
 *
 * @code
 * ServiceContainer services;
 * auto& cfg  = services.add<Config>("postgres://...");
 * auto& repo = services.add<Repo>(services.get<Config>());  // direct DI:
 *                                    // Config resolved as a constructor
 *                                    // argument, no factory indirection
 * @endcode
 *
 * That single pattern — pass `get<T>()` straight in as a constructor
 * argument that covers dependency injection
 * concept: `services.get<Config>()` fully evaluates (throwing immediately if
 * Config is missing) before `add<Repo>` is even entered, so ordering mistakes
 * still fail at startup with a named error, not silently or later.
 *
 * @par Resolution happens at startup, never per request
 * `get<T>()` is meant to be called while routes are being *configured*. The
 * reference it returns is captured into the handler's closure, so serving a
 * request costs one indirect call and zero lookups — the container is not on
 * the hot path at all and its lookup being a linear scan is irrelevant. Do
 * not call `get<T>()` from inside a handler body; capture the reference in
 * the enclosing configuration function instead.
 *
 * @par Construction is eager and ordered, which makes cycles impossible
 * A service is fully constructed when `add<T>()` returns, and can only be
 * built from services added before it (see the direct-injection example
 * above). There is therefore no lazy-initialization race and no dependency
 * cycle to detect: a cycle cannot be expressed.
 *
 * @warning Services are shared by every worker thread. WarpApi's workers are
 *          shared-nothing and never synchronize with each other, so anything
 *          you put in here must be immutable after startup or internally
 *          thread-safe. A service holding a bare `std::vector` that handlers
 *          mutate is a data race, not a slow path.
 *
 * @note Non-copyable and non-movable on purpose: handlers hold references
 *       into it for the process's lifetime, so it must never relocate.
 */
class WARP_API ServiceContainer {
public:
    ServiceContainer() = default;

    ~ServiceContainer()
    {
        // Reverse registration order, so a service is always destroyed before
        // anything it was allowed to depend on.
        for (auto it = _entries.rbegin(); it != _entries.rend(); ++it)
            it->deleter(it->instance);
    }

    ServiceContainer(const ServiceContainer&) = delete;
    ServiceContainer& operator=(const ServiceContainer&) = delete;
    ServiceContainer(ServiceContainer&&) = delete;
    ServiceContainer& operator=(ServiceContainer&&) = delete;

    /**
     * @brief Constructs a `T(std::forward<Args>(args)...)` and takes ownership.
     *
     * Also how you register an already-built value: `add<T>(std::move(t))`
     * forwards into `T`'s move constructor, so there is no separate "adopt"
     * method for that case.
     */
    template <typename T, typename... Args>
    T& add(Args&&... args)
    {
        if (tryGet<T>() != nullptr)
            throw std::runtime_error(errorText("Service registered twice: ", typeName<T>()));

        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = owned.release();

        Entry e;
        e.key = typeKey<T>();
        e.instance = static_cast<void*>(raw);
        e.deleter = [](void* p) { delete static_cast<T*>(p); };
        _entries.push_back(e);

        return *raw;
    }

    /**
     * @brief Resolves a service, or throws naming the missing type.
     * @note Startup-time API — see the class note on resolution timing. For
     *       an optional dependency, call this inside a try/catch at startup;
     *       the exception cost is irrelevant off the hot path.
     */
    template <typename T>
    T& get() const
    {
        if (T* found = tryGet<T>())
            return *found;

        throw std::runtime_error(errorText(
            "Service not registered (register it before the route that needs it): ",
            typeName<T>()));
    }

    usize size() const noexcept { return _entries.size(); }

private:
    template <typename T>
    T* tryGet() const noexcept
    {
        const void* key = typeKey<T>();
        for (const auto& e : _entries)
        {
            if (e.key == key)
                return static_cast<T*>(e.instance);
        }
        return nullptr;
    }

    static std::string errorText(std::string_view prefix, std::string_view type)
    {
        std::string s(prefix);
        s.append(type);
        return s;
    }

    struct Entry {
        const void* key;
        void* instance;
        void (*deleter)(void*); ///< Type-erased delete; captured while T is still known.
    };

    std::vector<Entry> _entries;
};

} // namespace warp

#endif // SERVICECONTAINER_H
