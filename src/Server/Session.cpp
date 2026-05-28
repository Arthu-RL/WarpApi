#include "Session.h"

#include <cstring>
#include <string>

#include <ink/LastWish.h>
#include <ink/utils.h>

#include "WebSocket.h"
#include "WebSocketContext.h"
#include "EventLoop/EventLoop.h"
#include "Managers/EndpointManager.h"
#include "Response/HttpResponse.h"
#include "Utils/HeadersList.h"
#include "Utils/StringUtils.h"
#include "Settings/Settings.h"

#ifdef USE_IOURING
Session::Session(socket_t socket) :
    _socket(socket),
    _req(),
    _keepAlive(false),
    _readBuffer(Settings::getSettings().max_request_size),
    _writeBuffer(Settings::getSettings().max_response_size)
{
    // Empty
}
#endif

#ifdef USE_EPOLL
Session::Session(socket_t socket, socket_t assignedEpollFd) :
    _socket(socket),
    _req(),
    _keepAlive(false),
    _readBuffer(Settings::getSettings().max_request_size),
    _writeBuffer(Settings::getSettings().max_response_size),
    _assignedEpollFd(assignedEpollFd)
{
    // Empty
}
#endif

Session::~Session()
{
    close();
}

void Session::close()
{
    if (_socket == SOCKET_ERROR_VALUE) return;

    ::close(_socket);
    _socket = SOCKET_ERROR_VALUE;
}

void Session::shutdown()
{
    if (_socket > SOCKET_ERROR_VALUE)
        ::shutdown(_socket, SHUT_RDWR);
}

socket_t Session::getSocket() const noexcept
{
    return _socket;
}

void Session::wsFrameSend(u8 opcode, std::string_view payload, bool fin)
{
    ws::sendFrame(_writeBuffer, opcode, payload, fin);
}

#ifdef USE_EPOLL
socket_t Session::getAssignedEpollFd() const noexcept
{
    return _assignedEpollFd;
}

bool Session::onReadReady()
{
    int read = false;
    while (1)
    {
        size_t availableSpace;
        char* buf = _readBuffer.getWriteBuffer(availableSpace);
        if (availableSpace == 0)
        {
            close();
            return true;
        }

        ssize_t bytesRead = recv(_socket, buf, availableSpace, 0);
        if (bytesRead > 0)
        {
            read = true;
            _readBuffer.advanceWritePos(bytesRead);

            if (_mode == ProtocolMode::Http)
            {
                while (parseRequest())
                    handleRequest();
            }
            else
            {
                WebSocketContext ctx(*this);
                if (!ws::processFrames(_wsState, ctx, _readBuffer, _writeBuffer))
                {
                    _keepAlive = false;
                    close();
                    return true;
                }
            }

            // Flush writes immediately without waiting for EPOLLOUT
            if (_writeBuffer.size() > 0)
            {
                onWriteReady();

                // If onWriteReady hit EAGAIN, stop reading to avoid memory bloat
                if (_writeBuffer.size() > 0)
                    break;
            }
        }
        else if (bytesRead == 0)
        {
            close();
            return true;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            close();
            return true;
        }
    }

    return read;
}

bool Session::onWriteReady()
{
    if (_writeBuffer.size() == 0)
        return false;

    bool wrote = false;

    while (1)
    {
        size_t available;
        const char* readBuf = _writeBuffer.getReadBuffer(available);

        if (available == 0) break;

        ssize_t bytesSent = send(_socket, readBuf, available, 0);

        if (bytesSent > 0)
        {
            wrote = true;
            _writeBuffer.advanceReadPos(bytesSent);
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            close();
            return true;
        }
    }

    if (_writeBuffer.size() == 0)
        onWriteComplete();

    return wrote;
}

void Session::onWriteComplete()
{
    if (_keepAlive)
    {
        if (_mode == ProtocolMode::Http)
            _req.reset();
    }
    else
    {
        close();
    }
}
#endif

#ifdef USE_IOURING
void Session::onReadReady(io_uring_sqe* sqe)
{
    size_t availableSpace;
    char* buf = _readBuffer.getWriteBuffer(availableSpace);

    if (availableSpace == 0)
    {
        this->close();
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data(sqe, nullptr);
        return;
    }

    io_uring_prep_recv(sqe, _socket, buf, availableSpace, 0);
    io_uring_sqe_set_data(sqe, &_readReq);
    updateIoState(IO_READING, true);
}

void Session::onWriteReady(io_uring_sqe* sqe)
{
    size_t availableSpace;
    const char* readBuf = _writeBuffer.getReadBuffer(availableSpace);
    if (availableSpace == 0)
    {
        this->close();
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data(sqe, nullptr);
        return;
    }

    // MSG_NOSIGNAL prevents EPIPE from killing the process
    // if the client closed the connection.
    io_uring_prep_send_zc(sqe, _socket, readBuf, availableSpace, MSG_NOSIGNAL, 0);
    io_uring_sqe_set_data(sqe, &_writeReq);
    updateIoState(IO_WRITING, true);
}

bool Session::processRead(i32 bytesRecv, io_uring* ring)
{
    updateIoState(IO_READING, false);

    if (bytesRecv <= 0)
    {
        if (_writeBuffer.size() == 0) {
            this->close();
            return false;
        }
        _keepAlive = false;
        setStatus(SessionStatus::Closing);
        return true;
    }

    _readBuffer.advanceWritePos(bytesRecv);

    if (_mode == ProtocolMode::Http)
    {
        while (parseRequest())
            handleRequest();
    }
    else
    {
        WebSocketContext ctx(*this);
        if (!ws::processFrames(_wsState, ctx, _readBuffer, _writeBuffer))
        {
            setStatus(SessionStatus::Closing);
            _keepAlive = false;
        }
    }

    if (_writeBuffer.size() > 0 && !isWriteInFlight())
    {
        io_uring_sqe* wSqe = io_uring_get_sqe(ring);
        if (wSqe) onWriteReady(wSqe);
    }

    if (!isReadInFlight() && _status == SessionStatus::Active)
    {
        io_uring_sqe* rSqe = io_uring_get_sqe(ring);
        if (rSqe) onReadReady(rSqe);
    }

    return true;
}

bool Session::processWrite(i32 bytesSent, bool is_notif, io_uring* ring)
{
    if (is_notif)
    {
        // Zero-Copy Notification (CQE 2): NIC is done with memory, advance buffer now
        updateIoState(IO_WAITING_ZC, false);

        _writeBuffer.advanceReadPos(_lockedZcBytes);
        _lockedZcBytes = 0;

        if (_writeBuffer.size() > 0)
        {
            io_uring_sqe* wSqe = io_uring_get_sqe(ring);
            if (wSqe) onWriteReady(wSqe);
            return true;
        }

        if (!_keepAlive)
        {
            this->close();
            return false;
        }

        return true;
    }

    // Send Result (CQE 1)
    updateIoState(IO_WRITING, false);

    if (bytesSent < 0)
    {
        this->close();
        return false;
    }

    // Record how many bytes the kernel accepted but DO NOT advance the buffer yet.
    // The memory is still pinned by the NIC until the F_NOTIF CQE arrives.
    _lockedZcBytes = bytesSent;

    return true;
}
#endif

bool Session::parseRequest()
{
    size_t avail;
    const char* data = _readBuffer.getReadBuffer(avail);
    if (__builtin_expect(!data || avail < MIN_REQUEST_SIZE, 0))
        return false;

    const char* p = data;
    const char* end = data + avail;

    // REQUEST LINE
    const char* lineEnd = StringUtils::find_crlf(p, end);
    if (__builtin_expect(!lineEnd || lineEnd + 1 >= end || lineEnd[1] != '\n', 0))
        return false;

    // METHOD
    const char* methodEnd = p;
    while (methodEnd < lineEnd && *methodEnd != ' ') ++methodEnd;
    if (__builtin_expect(methodEnd == lineEnd, 0)) return false;

    std::string_view method(p, methodEnd - p);

    // PATH
    const char* pathStart = methodEnd + 1;
    const char* pathEnd = pathStart;
    while (pathEnd < lineEnd && *pathEnd != '?' && *pathEnd != ' ') ++pathEnd;
    if (__builtin_expect(pathEnd == lineEnd, 0)) return false;

    std::string_view path(pathStart, pathEnd - pathStart);

    const char* queryStart = nullptr;
    const char* queryEnd = nullptr;
    if (*pathEnd == '?')
    {
        queryStart = pathEnd + 1;
        queryEnd = queryStart;
        while (queryEnd < lineEnd && *queryEnd != ' ') ++queryEnd;
        if (__builtin_expect(queryEnd == lineEnd, 0)) return false;
    }
    else
    {
        queryStart = pathEnd;
        queryEnd = pathEnd;
    }

    std::string_view query(queryStart, queryEnd - queryStart);

    // VERSION
    const char* verStart = queryEnd + 1;
    std::string_view version(verStart, lineEnd - verStart);

    _req.setMethod(HttpRequest::parseMethod(method));
    _req.setPath(path, query);
    _req.setVersion(version);

    p = lineEnd + 2; // skip CRLF

    // HEADERS
    size_t contentLength = 0;
    bool hasContentLen = false;
    _keepAlive = true; // HTTP/1.1 default

    while (__builtin_expect(p < end, 1))
    {
        if (__builtin_expect(p + 1 < end && p[0] == '\r' && p[1] == '\n', 0))
        {
            p += 2;
            break;
        }

        const char* hEnd = StringUtils::find_crlf(p, end);
        if (__builtin_expect(!hEnd || hEnd + 1 >= end || hEnd[1] != '\n', 0))
            return false;

        const char* colon = p;
        while (colon < hEnd && *colon != ':') ++colon;

        if (colon == hEnd)
        {
            p = hEnd + 2;
            continue;
        }

        size_t klen = colon - p;

        const char* v = colon + 1;
        if (v < hEnd && *v == ' ') ++v;
        size_t vlen = hEnd - v;

        HeaderType key = HeaderType::None;

        switch (klen)
        {
            case 10: // Connection
                if (StringUtils::iequals_small(std::string_view(v, vlen), CLOSE_CONN_HEADER))
                    _keepAlive = false;
                else if (StringUtils::iequals_small(std::string_view(v, vlen), KEEP_ALIVE_HEADER))
                    _keepAlive = true;
                key = HeaderType::Connection;
                break;
            case 14: // Content-Length
                contentLength = StringUtils::fast_atoi(v, vlen);
                hasContentLen = true;
                if (contentLength > Settings::getSettings().max_body_size)
                    return false;
                key = HeaderType::ContentLength;
                break;
            case 7:  // Upgrade
                key = HeaderType::Upgrade;
                break;
            case 17: // Sec-WebSocket-Key
                key = HeaderType::SecWebSocketKey;
                break;
            case 21: // Sec-WebSocket-Version
                key = HeaderType::SecWebSocketVersion;
                break;
        }

        if (key != HeaderType::None)
        {
            _req.presentHeaders() |= key;
        }

        _req.addHeader(key, v, vlen);
        p = hEnd + 2;
    }

    // BODY
    if (hasContentLen && contentLength > 0)
    {
        size_t headerSize = p - data;
        if (avail < headerSize + contentLength)
            return false;

        _req.setBody(std::string_view(p, contentLength));
        _readBuffer.advanceReadPos(headerSize + contentLength);
    }
    else
    {
        _readBuffer.advanceReadPos(p - data);
    }

    return true;
}

bool Session::upgradeToWebSocket()
{
    WebSocketRoute* wsRoute = EndpointManager::getInstance()->getWebSocketEndpoint(_req.path());
    if (!wsRoute)
        return false;

    auto sendBadUpgradeResponse = [&]() {
        HttpResponse response;
        response.setStatus(StatusCode::bad_request);
        response.setVersion(HTTP_VERSION);
        response.addHeader(HeaderType::Server, APP_INFO_HEADER);
        response.addHeader(HeaderType::Connection, CLOSE_CONN_HEADER);
        response.initBody(&_writeBuffer);
        response.setBody("Invalid WebSocket upgrade request.");
        _keepAlive = false;
#ifdef USE_IOURING
        setStatus(SessionStatus::Closing);
#endif
    };

    std::string_view upgradeHeader = _req.getHeader(HeaderType::Upgrade);
    std::string_view connectionHeader = _req.getHeader(HeaderType::Connection);
    std::string_view wsKey = _req.getHeader(HeaderType::SecWebSocketKey);
    std::string_view wsVersion = _req.getHeader(HeaderType::SecWebSocketVersion);

    bool validUpgrade =
        _req.method() == Method::GET &&
        StringUtils::iequals_small(upgradeHeader, WEBSOCKET_UPGRADE_HEADER) &&
        StringUtils::iequals_small(connectionHeader, UPGRADE_HEADER) &&
        !wsKey.empty() &&
        wsVersion == WS_VERSION_13_HEADER;

    if (!validUpgrade)
    {
        sendBadUpgradeResponse();
        return true;
    }

    char material[64];
    size_t matLen = wsKey.size() + ws::kWsGuid.size();
    std::memcpy(material, wsKey.data(), wsKey.size());
    std::memcpy(material + wsKey.size(), ws::kWsGuid.data(), ws::kWsGuid.size());

    auto digest = ws::sha1Digest(std::string_view(material, matLen));
    std::string accept = StringUtils::base64Encode(digest.data(), digest.size());

    constexpr std::string_view hsPart1 = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ";
    constexpr std::string_view hsPart2 = "\r\n\r\n";

    HttpResponse::writeAll(_writeBuffer, hsPart1.data(), hsPart1.size());
    HttpResponse::writeAll(_writeBuffer, accept.data(), accept.size());
    HttpResponse::writeAll(_writeBuffer, hsPart2.data(), hsPart2.size());

    _mode = ProtocolMode::WebSocket;
    _wsState.route = wsRoute;
    _wsState.closeSent = false;
    _keepAlive = true;
    _req.reset();

    if (_wsState.route->onOpen)
    {
        WebSocketContext ctx(*this);
        _wsState.route->onOpen(ctx);
    }

    return true;
}

void Session::handleRequest()
{
    HeaderType& headers = _req.presentHeaders();

    if (hasHeader(headers, HeaderType::Upgrade | HeaderType::SecWebSocketKey | HeaderType::SecWebSocketVersion))
    {
        upgradeToWebSocket();
        return;
    }

    HttpResponse response;
    response.setVersion(HTTP_VERSION);
    response.addHeader(HeaderType::Server, APP_INFO_HEADER);
    response.initBody(&_writeBuffer);

    if (_keepAlive)
    {
        response.addHeader(HeaderType::Connection, KEEP_ALIVE_HEADER);
    }
    else
    {
        response.addHeader(HeaderType::Connection, CLOSE_CONN_HEADER);
#ifdef USE_IOURING
        setStatus(SessionStatus::Closing);
#endif
    }

    try
    {
        Endpoint* endpoint = EndpointManager::getInstance()->getEndpoint(_req.method(), _req.path());

        if (endpoint != nullptr)
        {
            endpoint->exec(_req, response);
        }
        else
        {
            response.setStatus(StatusCode::not_found);
            response.setBody("Endpoint not found.");
        }
    }
    catch (const std::exception& e)
    {
        response.setStatus(StatusCode::internal_server_error);
        if (_keepAlive)
        {
            response.addHeader(HeaderType::Connection, CLOSE_CONN_HEADER);
#ifdef USE_IOURING
            setStatus(SessionStatus::Closing);
#endif
            _keepAlive = false;
        }
        response.setBody("Internal Server error: " + std::string(e.what()));
    }
}
