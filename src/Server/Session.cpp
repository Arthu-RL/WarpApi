#include "Session.h"

Session::Session(tcp::socket socket) : socket_(std::move(socket))
{
    read();
}

Session::~Session()
{
    PLOG_DEBUG << "Session Closed!";
}

void Session::read() {
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

void Session::write() {
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

http::response<http::string_body> Session::handle_request(const http::request<http::string_body>& req) {
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
