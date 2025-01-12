#include "Session.h"

#include "Utils/JsonLoader.h"

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

    std::shared_ptr<Session> self = shared_from_this();
    http::async_read(_socket, _buffer, _req, [self](beast::error_code ec, std::size_t) {
        if (!ec) {
            self->write();
        }
        else if (ec == http::error::end_of_stream) {
            PLOG_DEBUG << "Client closed connection (end of stream)";
            self->_socket.shutdown(tcp::socket::shutdown_send, ec);
        }
        else
        {
            PLOG_WARNING << ec.message();
        }
    });
}

void Session::write() {
    PLOG_DEBUG << "Session Writing!";

    std::shared_ptr<Session> self = shared_from_this();

    auto res = std::make_shared<http::response<http::string_body>>(build_response());

    http::async_write(_socket, *res, [self, res](beast::error_code ec, std::size_t bytes_transferred) {
        if (!ec) {
            PLOG_DEBUG << "Response sent: " << bytes_transferred << " bytes";

            if (self->_req.keep_alive())
                self->read();
            else
                self->_socket.shutdown(tcp::socket::shutdown_send, ec);
        } else
        {
            PLOG_WARNING << ec.message();
        }
    });
}

http::response<http::string_body> Session::build_response() {
    http::response<http::string_body> res{http::status::ok, _req.version()};
    res.set(http::field::content_type, "application/json");
    res.set(http::field::connection, "keep-alive");
    res.set(http::field::cache_control, "no-cache, no-store, must-revalidate");
    res.set(http::field::strict_transport_security, "max-age=31536000; includeSubDomains");
    res.set(http::field::server, "MyCustomServer/1.0");
    res.set(http::field::access_control_allow_origin, "*");
    res.set("X-Content-Type-Options", "nosniff");
    res.set("X-Frame-Options", "DENY");
    res.set("X-XSS-Protection", "1; mode=block");
    res.set("Referrer-Policy", "no-referrer");
    res.set("Feature-Policy", "geolocation 'self'; camera 'none'");

    if (_req.method() == http::verb::get) {
        if (_req.target() == "/") {
            nlohmann::json array = JsonLoader::array();
            nlohmann::json obj = JsonLoader::object();
            obj["message"] = "Welcome to home page!";
            nlohmann::json meta_obj = JsonLoader::meta_info();
            array.push_back(obj);
            array.push_back(meta_obj);
            res.body() = JsonLoader::jsonToIndentedString(array);
        } else if (_req.target() == "/hello") {
            nlohmann::json obj = JsonLoader::object();
            obj["message"] = "Hello, World!";
            res.body() = JsonLoader::jsonToIndentedString(obj);
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

