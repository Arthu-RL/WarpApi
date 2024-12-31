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

private:
    void read();

    void write();

    http::response<http::string_body> handle_request(const http::request<http::string_body>& req);

    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
};

#endif // SESSION_H
