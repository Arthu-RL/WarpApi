#include "Session.h"

#include <plog/Log.h>

#include "../Endpoint/ResponseManager.h"
#include "../Endpoint/EndpointManager.h"
#include "Utils/RouteIdentifier.h"

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
    http::async_read(_socket, _buffer, _req.getRequest(), [self](beast::error_code ec, std::size_t) {
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

            if (self->_req.isKeepAlive())
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
    ResponseManager<http::string_body> res(_req.getVersion());

    _req.extractQueryParams();
    const std::string endpoint_id = RouteIdentifier::generateIdentifier(_req.requestPath(), _req.requestMethod());
    auto endpoint = EndpointManager::getEndpoint(endpoint_id);

    if (endpoint != nullptr)
    {
        try {
            endpoint->exec(_req, res);
        } catch (const std::exception& e) {
            res.setStatus(http::status::internal_server_error);
            res.setBody("Internal Server error: " + std::string(e.what()));
        }
    }
    else {
        res.setStatus(http::status::not_found);
        res.setBody("Endpoint not found.");
    }

    _req.reset();

    res.pPayload();
    return res.getResponse();
}

