#include "HttpServer.h"

#include "Settings/Settings.h"

HttpServer::HttpServer() :
    _running(false),
    _eventLoop(std::make_unique<EventLoop>())
{
    INK_ASSERT_MSG(Settings::isValid(), "Settings not initialized!");
    INK_ASSERT_MSG(_eventLoop != nullptr, "EventLoop is NULL!");
}

HttpServer::~HttpServer()
{
    stop();

#ifdef _WIN32
    WSACleanup();
#endif

    INK_DEBUG << "Server destroyed!";
}

void HttpServer::start()
{
    if (_running) {
        return;
    }

    _eventLoop->start();

    _running = true;
}

void HttpServer::stop()
{
    if (!_running) {
        return;
    }

    _running = false;

    // Stop event loop
    if (_eventLoop)
    {
        _eventLoop->stop();
    }

    INK_INFO << "Server stopped";
}
