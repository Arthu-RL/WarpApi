#include "HttpServer.h"

#include <plog/Log.h>

#include "Session.h"

HttpServer::HttpServer(net::io_context& ioc, tcp::endpoint addr)
    : acceptor_(ioc, addr) {
    accept();
}

HttpServer::~HttpServer()
{
    PLOG_DEBUG << "Server destroyed!";
}

void HttpServer::accept() {
    acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
        if (!ec) {
            std::make_shared<Session>(std::move(socket));
        }
        accept();
    });
}
