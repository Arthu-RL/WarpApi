#include "Session.h"

Session::Session(tcp::socket socket) : _socket(std::move(socket))
{
    PLOG_DEBUG << "Session Created!";
}

Session::~Session()
{
    PLOG_DEBUG << "Session Closed!";
}

void Session::start() {
    read();
}

void Session::read() {
    PLOG_DEBUG << "Session Reading!";

    auto self = shared_from_this();
    http::async_read(_socket, _buffer, _req, [self](beast::error_code ec, std::size_t) {
        if (!ec) {
            self->write();
        } else
        {
            PLOG_ERROR << "Error: " << ec.message();
        }
    });
}

void Session::write() {
    PLOG_DEBUG << "Session Writing!";

    auto self = shared_from_this();

    auto res = std::make_shared<http::response<http::string_body>>(
        [self]() -> http::response<http::string_body> {
            http::response<http::string_body> res{http::status::ok, self->_req.version()};
            res.set(http::field::content_type, "text/plain");

            if (self->_req.method() == http::verb::get) {
                if (self->_req.target() == "/") {
                    res.body() = "Welcome to the home page!";
                } else if (self->_req.target() == "/hello") {
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
        }());

    http::async_write(_socket, *res, [self, res](beast::error_code ec, std::size_t) {
        if (!ec) {
            self->_socket.shutdown(tcp::socket::shutdown_send);
        } else
        {
            PLOG_ERROR << "Error: " << ec.message();
        }
    });
}

