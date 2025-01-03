#ifndef SESSION_H
#define SESSION_H

#pragma once

#include <plog/Log.h>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket);
    ~Session();

    void start();

private:
    void read();

    void write();

    http::response<http::string_body> build_response();

    tcp::socket _socket;
    beast::flat_buffer _buffer;
    http::request<http::string_body> _req;
};

#endif // SESSION_H
