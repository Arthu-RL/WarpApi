#include "HttpServer.h"

#include <plog/Log.h>

#include "Session.h"

HttpServer::HttpServer(net::io_context& ioc, tcp::endpoint addr)
    : _acceptor(ioc, addr), _ioc(ioc) {
    accept();
}

HttpServer::~HttpServer()
{
    PLOG_DEBUG << "Server destroyed!";
}

void HttpServer::accept() {
    _acceptor.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
        PLOG_DEBUG << "Accepted socket: " << socket.remote_endpoint();
        if (!ec) {
            std::shared_ptr<Session> session = std::make_shared<Session>(std::move(socket));
            session->start();
        }
        accept();
    });
}

void HttpServer::run_thread_pool(uint num_threads) {
    std::vector<std::thread> threads;

    for (uint i = 0; i < num_threads; ++i) {
        threads.emplace_back([this] { _ioc.run(); });
    }

    PLOG_INFO << "Running with " << num_threads << " threads.";

    for (auto& t : threads) {
        t.join();
    }
}
