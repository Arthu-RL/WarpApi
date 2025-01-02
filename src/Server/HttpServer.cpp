#include "HttpServer.h"

#include <plog/Log.h>

#include "Session.h"

HttpServer::HttpServer(net::io_context& ioc, tcp::endpoint addr)
    : _acceptor(ioc, addr) {
    accept();
}

HttpServer::~HttpServer()
{
    PLOG_DEBUG << "Server destroyed!";
}

void HttpServer::accept() {
    _acceptor.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
        if (!ec) {
            auto session = std::make_shared<Session>(std::move(socket));
            session->start();
        }
        accept();
    });
}
