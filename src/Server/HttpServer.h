#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#pragma once

#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <iostream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpServer {
public:
    HttpServer(net::io_context& ioc, tcp::endpoint addr);
    ~HttpServer();

    void run_thread_pool(uint num_threads);

private:
    void accept();

    tcp::acceptor _acceptor;
    net::io_context& _ioc;
};
#endif // HTTPSERVER_H
