#include "WarpDefs.h"
#include "Core/Router.h"
#include "Core/ServiceContainer.h"
#include "Server/HttpServer.h"
#include "Managers/EndpointManager.h"
#include "Services/AppServices.h"
#include "Services/DiagnosticsRoutes.h"
#include "Services/GeneralServices.h"
#include "Settings/Settings.h"
#include <csignal>

#ifdef NDEBUG
const ink::LogLevel logSeverity = ink::LogLevel::INFO;
#else
const ink::LogLevel logSeverity = ink::LogLevel::TRACE;
#endif

std::atomic<bool> g_shutdown_requested{false};

void signalHandler(int /*signal*/) {
    g_shutdown_requested.store(true, std::memory_order_relaxed);
}

/**
 * @brief Raises the descriptor limit as far as the hard limit allows.
 *
 * Asking for a soft limit above the hard limit fails outright unless the
 * process holds CAP_SYS_RESOURCE, so clamp instead of losing the raise
 * entirely on an unprivileged run.
 */
static void increase_fd_limit(rlim_t desired)
{
    struct rlimit rl{};
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
        INK_WARN << "getrlimit(RLIMIT_NOFILE) failed: " << strerror(errno);
        return;
    }

    const rlim_t hard = rl.rlim_max;
    const rlim_t target = (hard == RLIM_INFINITY) ? desired : std::min(desired, hard);

    if (rl.rlim_cur >= target)
        return;

    struct rlimit want{};
    want.rlim_cur = target;
    want.rlim_max = rl.rlim_max;

    if (setrlimit(RLIMIT_NOFILE, &want) != 0) {
        INK_WARN << "Could not raise RLIMIT_NOFILE to " << target << ": " << strerror(errno);
        return;
    }

    INK_INFO << "Open file descriptor limit raised to " << target;
}

#ifdef USE_IOURING
/**
 * @brief Raises RLIMIT_MEMLOCK as far as the hard limit allows.
 *
 * Every io_uring instance mlock()s its SQ/CQ rings, charged against this
 * limit. Containers default to 8MB, which is enough for only a handful of
 * worker threads; leaving it there silently blackholes whichever
 * SO_REUSEPORT shards fail to come up. Same clamp-to-hard-limit strategy as
 * the fd limit above, since we may not hold CAP_SYS_RESOURCE.
 */
static void increase_memlock_limit(rlim_t desired)
{
    struct rlimit rl{};
    if (getrlimit(RLIMIT_MEMLOCK, &rl) != 0) {
        INK_WARN << "getrlimit(RLIMIT_MEMLOCK) failed: " << strerror(errno);
        return;
    }

    const rlim_t hard = rl.rlim_max;
    const rlim_t target = (hard == RLIM_INFINITY) ? desired : std::min(desired, hard);

    if (rl.rlim_cur >= target)
        return;

    struct rlimit want{};
    want.rlim_cur = target;
    want.rlim_max = rl.rlim_max;

    if (setrlimit(RLIMIT_MEMLOCK, &want) != 0) {
        INK_WARN << "Could not raise RLIMIT_MEMLOCK to " << target
                 << " bytes; some io_uring shards may fail to start: " << strerror(errno);
        return;
    }

    INK_INFO << "Locked memory limit raised to " << target << " bytes";
}
#endif

int main(int /*argc*/, char** /*argv*/)
{
    increase_fd_limit(1000000);
#ifdef USE_IOURING
    increase_memlock_limit(256u * 1024 * 1024);
#endif

    // A peer that closes mid-write must not take the process down with it.
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Initialize logger
    INK_CORE_LOGGER->setName("WarpAPI");
    ink::LogManager::getInstance().setGlobalLevel(logSeverity);

    INK_INFO << "Starting WarpAPI server...";

    // Load configuration
    ink::EnhancedJson appConfig = ink::EnhancedJson::loadFromFile("./config.json");
    if (appConfig.empty()) {
        INK_ERROR << "Failed to load config.json";
        return EXIT_FAILURE;
    }

    Settings settingsLoader(appConfig);
    if (!Settings::isValid()) {
        INK_ERROR << "Invalid configuration; refusing to start.";
        return EXIT_FAILURE;
    }

    INK_INFO << "WarpAPI settings loaded.";

    try
    {
        EndpointManager* endpointManager = EndpointManager::getInstance();

        // Constructed in dependency order; add<T>() can only resolve services
        // added above it (see ServiceContainer.h)
        warp::ServiceContainer services;
        services.add<BuildInfo>("WarpApi", "0.1.0");
        services.add<RequestCounter>();

        // Routes: plain functions taking a Router&, called here in order.
        // Explicit and ordered — every route in the program is visible from
        // this one call list, and one that needs a service resolves it from
        // `services` above (see DiagnosticsRoutes.cpp).
        warp::Router router(*endpointManager, services);
        configureGeneralRoutes(router);
        configureDiagnosticsRoutes(router);

        // Everything must be registered before the workers start: freeze()
        // builds the lookup index and makes the registry read-only, which is
        // precisely what lets every worker query it without synchronization.
        endpointManager->freeze();

        INK_INFO << "Registered endpoints: " << endpointManager->count()
                 << " | services: " << services.size();

        HttpServer server;
        server.start();

        while (!g_shutdown_requested.load(std::memory_order_relaxed))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        INK_INFO << "Shutdown signal detected. Stopping server gracefully...";
        server.stop();
        INK_INFO << "Server stopped. Bye!";
    }
    catch (const std::exception& e)
    {
        INK_ERROR << "Server error: " << e.what();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
