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

#ifdef NDEBUG
const bool enableValidationLayers = false;
const plog::Severity plogSeverity = plog::info;
#else
const bool enableValidationLayers = true;
const plog::Severity plogSeverity = plog::debug;
#endif


int main(int argc, char** argv)
{
    static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init(plogSeverity, &consoleAppender);

    try {
        net::io_context ioc;

        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&](boost::system::error_code, int) {
            ioc.stop();
        });

        HttpServer server(ioc, tcp::endpoint(net::ip::make_address("127.0.0.1"), 8080));

        uint tcounter = 0;
        std::vector<std::thread> threads;
        for (std::size_t i = 0; i < 4; ++i) {
            threads.emplace_back([&ioc] { ioc.run(); });
            tcounter++;
        }

        PLOG_INFO << "Server initialized with " << tcounter << " threads.";

        for (auto& t : threads) {
            t.join();
        }
    } catch (const std::exception& e)
    {
        PLOG_ERROR << "Server error: " << e.what();
    }


    return 0;
}
