#include "WarpDefs.h"
#include "Server/HttpServer.h"
#include "Managers/EndpointManager.h"
#include "Services/GeneralServices.h"
#include "Settings/Settings.h"
#include <csignal>

#ifdef NDEBUG
const ink::LogLevel logSeverity = ink::LogLevel::INFO;
#else
const ink::LogLevel logSeverity = ink::LogLevel::TRACE;
#endif

// Global server pointer for signal handling
HttpServer* g_server = nullptr;

void signalHandler(int signal) {
    INK_INFO << "Received signal " << signal << ", shutting down...";
    if (g_server) {
        g_server->stop();
    }
    exit(signal);
}

void increase_fd_limit(uint64_t limit) {
    struct rlimit rl;
    rl.rlim_cur = limit; // Soft limit
    rl.rlim_max = limit; // Hard limit
    if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
        perror("setrlimit failed");
    }
}

int main(int /*argc*/, char** /*argv*/)
{
    increase_fd_limit(1000000);

    // Initialize logger
    INK_CORE_LOGGER->setName("WarpAPI");
    ink::LogManager::getInstance().setGlobalLevel(logSeverity);

    INK_INFO << "Starting WarpAPI server...";

    // Load configuration
    ink::EnhancedJson appConfig = ink::EnhancedJson::loadFromFile("./config.json");
    if (appConfig.empty()) {
        INK_ERROR << "Failed to load config.json";
        std::exit(EXIT_FAILURE);
    }
    SettingsData settings = Settings(appConfig).getSettings();

    INK_INFO << "WarpAPI settings loaded.";

    try {
        // Initialize endpoint manager
        EndpointManager* endpointManager = EndpointManager::getInstance();

        // Register services/endpoints
        GeneralServices generalServices;

        INK_INFO << "Registered endpoints: " << endpointManager->count();

        // Create and configure the server
        HttpServer server;
        g_server = &server;

        // Set up signal handlers for graceful shutdown
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        // Start the server
        server.start();
    }
    catch (const std::exception& e)
    {
        INK_ERROR << "Server error: " << e.what();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
