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

int main(int argc, char** argv)
{
    // Initialize logger
    INK_CORE_LOGGER;
    ink::LogManager::getInstance().setGlobalLevel(logSeverity);
    INK_INFO << "Starting WarpAPI server...";

    // Load configuration
    ink::EnhancedJson appConfig = ink::EnhancedJson::loadFromFile("./config.json");
    if (appConfig.empty()) {
        INK_ERROR << "Failed to load config.json";
        std::exit(EXIT_FAILURE);
    }

    SettingsData settings = Settings(appConfig).getSettings();
    INK_ASSERT_MSG(Settings::isValid(), "Settings not initialized!");

    INK_INFO << "WarpAPI settings loaded.";

    try {
        // Initialize endpoint manager
        EndpointManager endpointManager;

        // Register services/endpoints
        GeneralServices generalServices;

        // Create and configure the server
        HttpServer server(settings.port, settings.max_auxiliar_threads, settings.connection_timeout_ms, settings.backlog_size);
        g_server = &server;

        // Configure server parameters
        server.setBacklogSize(settings.backlog_size);
        server.setConnectionTimeout(settings.connection_timeout_ms);

        // Set up signal handlers for graceful shutdown
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        // Start the server
        server.start();

        // Log server status
        INK_INFO << "Server started successfully on " << settings.ip << ":" << settings.port;
        INK_INFO << "Thread pool size: " << settings.max_threads;
        INK_INFO << "Connection backlog: " << settings.backlog_size;
        INK_INFO << "Connection timeout: " << settings.connection_timeout_ms << "ms";
        INK_INFO << "Registered endpoints: " << endpointManager.count();
        INK_INFO << "Press Ctrl+C to stop the server";

        // Keep main thread alive
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    catch (const std::exception& e)
    {
        INK_ERROR << "Server error: " << e.what();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
