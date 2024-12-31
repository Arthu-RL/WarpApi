#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#pragma once

#include <plog/Log.h>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <iostream>
#include <thread>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpServer {
public:
    HttpServer(net::io_context& ioc, tcp::endpoint addr);
    ~HttpServer();

private:
    void accept();

    class Session : public std::enable_shared_from_this<Session> {
    public:
        Session(tcp::socket socket) : socket_(std::move(socket)) {}
        ~Session() { PLOG_DEBUG << "Session Closed!"; }

        void start() {
            read();
        }

    private:
        void read() {
            auto self = shared_from_this();
            http::async_read(socket_, buffer_, req_, [self](beast::error_code ec, std::size_t) {
                if (!ec) {
                    self->write();
                } else
                {
                    PLOG_ERROR << "Error: " << ec.message();
                }
            });
        }

        void write() {
            auto self = shared_from_this();
            auto res = std::make_shared<http::response<http::string_body>>(handle_request(req_));
            http::async_write(socket_, *res, [self, res](beast::error_code ec, std::size_t) {
                if (!ec) {
                    self->socket_.shutdown(tcp::socket::shutdown_send);
                } else
                {
                    PLOG_ERROR << "Error: " << ec.message();
                }
            });
        }

        http::response<http::string_body> handle_request(const http::request<http::string_body>& req) {
            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::content_type, "text/plain");

            if (req.method() == http::verb::get) {
                if (req.target() == "/") {
                    res.body() = "Welcome to the home page!";
                } else if (req.target() == "/hello") {
                    res.body() = "Hello, World!";
                } else {
                    res.result(http::status::not_found);
                    res.body() = "404 Not Found";
                }
            } else {
                res.result(http::status::method_not_allowed);
                res.body() = "405 Method Not Allowed";
            }

            res.prepare_payload();
            return res;
        }

        tcp::socket socket_;
        beast::flat_buffer buffer_;
        http::request<http::string_body> req_;
    };

    tcp::acceptor acceptor_;
};

#endif // HTTPSERVER_H
