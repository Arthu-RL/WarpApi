#include <plog/Log.h>
#include <plog/Initializers/ConsoleInitializer.h>
#include <plog/Formatters/TxtFormatter.h>
// #include <plog/Formatters/MessageOnlyFormatter.h>
#include <plog/Appenders/ColorConsoleAppender.h>

#include <memory.h>
#include <thread>
#include <queue>
#include <condition_variable>
// #include <unordered_map>

#include <boost/beast.hpp>
#include <boost/asio.hpp>

#include "Server/HttpServer.h"
#include "Utils/JsonLoader.h"

#include "Endpoint/EndpointManager.h"
#include "Services/GeneralServices.h"

#ifdef NDEBUG
const plog::Severity plogSeverity = plog::info;
#else
const plog::Severity plogSeverity = plog::debug;
#endif


int main(int argc, char** argv)
{
    static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init(plogSeverity, &consoleAppender);

    nlohmann::json appConfig = JsonLoader::loadJsonFromFile("./config.json");

    if (appConfig.empty()) std::exit(EXIT_FAILURE);

    try {
        net::io_context ioc;

        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&](boost::system::error_code, int) {
            ioc.stop();
        });

        std::string ip = appConfig["ip"].get<std::string>();
        uint16_t port = appConfig["port"].get<uint16_t>();

        net::ip::address address = net::ip::make_address(ip);

        tcp::endpoint addr(address, port);
        HttpServer server(ioc, addr);

        PLOG_INFO << "Server listening on " << ip+':'+std::to_string(port);

        EndpointManager endpointManager;

        // Endpoints
        GeneralServices generalServices;

        // logs
        PLOG_INFO << "Endpoints counter: " << endpointManager.count();

        // Threads for Request handle pool
        uint maxThreads = std::min(std::thread::hardware_concurrency(), appConfig["max_threads"].get<uint>());

        server.run_thread_pool(maxThreads);

    } catch (const std::exception& e)
    {
        PLOG_ERROR << "Server error: " << e.what();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
