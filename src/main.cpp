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

#ifdef NDEBUG
const plog::Severity plogSeverity = plog::info;
#else
const plog::Severity plogSeverity = plog::debug;
#endif


int main(int argc, char** argv)
{
    static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init(plogSeverity, &consoleAppender);

    nlohmann::json appConfig = JsonLoader::loadJsonFromFile("../../config.json");

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

        uint maxThreads = appConfig["max_threads"].get<uint>();
        uint tcount = std::min(std::thread::hardware_concurrency(), maxThreads);
        std::vector<std::thread> threads;
        for (uint i = 0; i < tcount; ++i) {
            threads.emplace_back([&ioc] { ioc.run(); });
        }

        PLOG_INFO << "Server listening on " << ip+':'+std::to_string(port);
        PLOG_INFO << "Running with " << tcount << " threads.";

        for (auto& t : threads) {
            t.join();
        }
    } catch (const std::exception& e)
    {
        PLOG_ERROR << "Server error: " << e.what();
    }

    return 0;
}
