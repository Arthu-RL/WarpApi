#include "Session.h"

#include <cstring>
#include <string>

#include "WebSocket.h"
#include "WebSocketContext.h"
#include "EventLoop/EventLoop.h"
#include "Managers/EndpointManager.h"
#include "Utils/HeadersList.h"
#include "Utils/StringUtils.h"
#include "Settings/Settings.h"

namespace {

// Smallest recv() window we bother asking the buffer for.
constexpr usize kReadChunk = 4096;

// Stop pulling further pipelined requests once this much output is queued, so
// a client that pipelines thousands of requests cannot make buffer without
// bound. Reading resumes as soon as the socket drains.
constexpr usize kWriteHighWater = 256 * 1024;

// Above this size, handing the pages to the NIC beats copying them. Below it,
// zero-copy costs an extra completion and a page pin for no benefit.
constexpr usize kZeroCopyThreshold = 32 * 1024;

// Guards against a peer that opens a request and never finishes the headers.
constexpr u32 kMaxHeaderLines = 128;

constexpr std::string_view kNotFoundBody = "{\"error\":\"Endpoint not found.\"}";

/**
 * @brief Renders an allowedMethods() bitmask as an Allow header value.
 * @note Writes into caller-owned storage that must outlive the response's
 *       commit, since the response stores a view rather than copying.
 */
std::string_view formatAllow(u32 mask, char* buf, usize cap) noexcept
{
    static constexpr std::string_view kNames[] = {
        "GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"
    };

    usize len = 0;
    for (u32 m = 0; m < static_cast<u32>(Method::UNKNOWN); ++m)
    {
        if ((mask & (1u << m)) == 0)
            continue;

        const std::string_view name = kNames[m];
        if (len + name.size() + 2 >= cap)
            break;

        if (len)
        {
            buf[len++] = ',';
            buf[len++] = ' ';
        }
        std::memcpy(buf + len, name.data(), name.size());
        len += name.size();
    }

    return std::string_view(buf, len);
}

} // namespace

#ifdef USE_IOURING
Session::Session(socket_t socket) :
    _socket(socket),
    _readBuffer(Settings::getSettings().read_buffer_size, Settings::getSettings().max_request_size),
    _writeBuffer(Settings::getSettings().write_buffer_size, Settings::getSettings().max_response_size),
    _flightBuffer(Settings::getSettings().write_buffer_size, Settings::getSettings().max_response_size)
{
}
#endif

#ifdef USE_EPOLL
Session::Session(socket_t socket, socket_t assignedEpollFd) :
    _socket(socket),
    _readBuffer(Settings::getSettings().read_buffer_size, Settings::getSettings().max_request_size),
    _writeBuffer(Settings::getSettings().write_buffer_size, Settings::getSettings().max_response_size),
    _assignedEpollFd(assignedEpollFd)
{
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

bool Session::wsFrameSend(u8 opcode, std::string_view payload, bool fin)
{
    if (_mode != ProtocolMode::WebSocket || _wsState.closeSent)
        return false;

    return ws::sendFrame(_writeBuffer, opcode, payload, fin);
}

void Session::wsClose(u16 code, std::string_view reason)
{
    if (_mode != ProtocolMode::WebSocket)
    {
        _closeAfterFlush = true;
        return;
    }

    if (!_wsState.closeSent)
    {
        ws::sendClose(_writeBuffer, code, reason);
        _wsState.closeSent = true;
    }

    // Flush first: the peer must see a well-formed closing handshake.
    _closeAfterFlush = true;
}

bool Session::wsIsOpen() const noexcept
{
    return _mode == ProtocolMode::WebSocket && !_wsState.closeSent && !_wsState.closeReceived;
}

Session::ParseResult Session::parseRequest()
{
    usize avail = 0;
    const char* data = _readBuffer.readPtr(avail);

    if (avail < MIN_REQUEST_SIZE)
        return ParseResult::Incomplete;

    // A request must be fully re-initialized here. Reusing the previous
    // request's header slots across a pipelined batch made request N inherit
    // request N-1's headers, which (among other things) turned a plain GET
    // following a WebSocket handshake into a second upgrade attempt.
    _req.reset();

    const char* p = data;
    const char* const end = data + avail;

    // ---- Request line
    const char* lineEnd = StringUtils::find_crlf(p, end);
    if (!lineEnd || lineEnd + 1 >= end)
        return avail >= _readBuffer.maxCapacity() ? ParseResult::Error : ParseResult::Incomplete;
    if (lineEnd[1] != '\n')
        return ParseResult::Error;

    const char* methodEnd = p;
    while (methodEnd < lineEnd && *methodEnd != ' ') ++methodEnd;
    if (methodEnd == lineEnd) return ParseResult::Error;

    const std::string_view method(p, static_cast<usize>(methodEnd - p));

    const char* pathStart = methodEnd + 1;
    const char* pathEnd = pathStart;
    while (pathEnd < lineEnd && *pathEnd != '?' && *pathEnd != ' ') ++pathEnd;
    if (pathEnd == lineEnd) return ParseResult::Error;

    const std::string_view path(pathStart, static_cast<usize>(pathEnd - pathStart));

    const char* queryStart = pathEnd;
    const char* queryEnd = pathEnd;
    if (*pathEnd == '?')
    {
        queryStart = pathEnd + 1;
        queryEnd = queryStart;
        while (queryEnd < lineEnd && *queryEnd != ' ') ++queryEnd;
        if (queryEnd == lineEnd) return ParseResult::Error;
    }

    const std::string_view query(queryStart, static_cast<usize>(queryEnd - queryStart));

    const char* verStart = queryEnd + 1;
    if (verStart > lineEnd) return ParseResult::Error;
    const std::string_view version(verStart, static_cast<usize>(lineEnd - verStart));

    const Method parsedMethod = HttpRequest::parseMethod(method);
    if (parsedMethod == Method::UNKNOWN)
        return ParseResult::Error;

    _req.setMethod(parsedMethod);
    _req.setPath(path, query);
    _req.setVersion(version);

    // HTTP/1.0 defaults to close, HTTP/1.1 to keep-alive.
    _keepAlive = !(version.size() == 8 && version[7] == '0');

    p = lineEnd + 2;

    // Headers
    u64 contentLength = 0;
    bool hasContentLen = false;
    bool chunked = false;
    bool expectContinue = false;
    u32 headerLines = 0;

    for (;;)
    {
        if (p + 1 >= end)
            return avail >= _readBuffer.maxCapacity() ? ParseResult::Error : ParseResult::Incomplete;

        if (p[0] == '\r' && p[1] == '\n')
        {
            p += 2;
            break;
        }

        if (++headerLines > kMaxHeaderLines)
            return ParseResult::Error;

        const char* hEnd = StringUtils::find_crlf(p, end);
        if (!hEnd || hEnd + 1 >= end)
            return avail >= _readBuffer.maxCapacity() ? ParseResult::Error : ParseResult::Incomplete;
        if (hEnd[1] != '\n')
            return ParseResult::Error;

        const char* colon = p;
        while (colon < hEnd && *colon != ':') ++colon;

        if (colon == hEnd)
        {
            // No colon: not a header field. Tolerate and skip.
            p = hEnd + 2;
            continue;
        }

        const usize klen = static_cast<usize>(colon - p);

        const char* v = colon + 1;
        while (v < hEnd && (*v == ' ' || *v == '\t')) ++v;
        const char* vEnd = hEnd;
        while (vEnd > v && (vEnd[-1] == ' ' || vEnd[-1] == '\t')) --vEnd;
        const usize vlen = static_cast<usize>(vEnd - v);

        const HeaderType key = StringUtils::matchHeaderName(p, klen);
        const std::string_view value(v, vlen);

        switch (key)
        {
            case HeaderType::Connection:
                if (StringUtils::containsToken(value, CLOSE_CONN_HEADER))
                    _keepAlive = false;
                else if (StringUtils::containsToken(value, KEEP_ALIVE_HEADER))
                    _keepAlive = true;
                break;

            case HeaderType::ContentLength:
            {
                u64 parsed = 0;
                if (!StringUtils::parseDecimal(v, vlen, parsed))
                    return ParseResult::Error;
                // A second, disagreeing Content-Length is a request smuggling vector.
                if (hasContentLen && parsed != contentLength)
                    return ParseResult::Error;
                contentLength = parsed;
                hasContentLen = true;
                if (contentLength > Settings::getSettings().max_body_size)
                    return ParseResult::Error;
                break;
            }

            case HeaderType::TransferEncoding:
                if (StringUtils::containsToken(value, "chunked"))
                    chunked = true;
                break;

            case HeaderType::Expect:
                if (StringUtils::iequals_small(value, "100-continue"))
                    expectContinue = true;
                break;

            default:
                break;
        }

        _req.addHeader(key, v, vlen);
        p = hEnd + 2;
    }

    // Chunked bodies are not implemented; refusing beats mis-framing the stream.
    if (chunked)
        return ParseResult::Error;

    const usize headerSize = static_cast<usize>(p - data);

    // The interim response has to go out the moment the headers are complete,
    // *before* we start waiting on the body. A conforming client holds the body
    // back until it sees this, so emitting it after the "body incomplete" path
    // below would deadlock both sides until the keep-alive timer fired.
    // _continueSent keeps a pipelined re-parse of the same request from
    // queueing it twice.
    if (expectContinue && !_continueSent)
    {
        _continueSent = true;
        _writeBuffer.append("HTTP/1.1 100 Continue\r\n\r\n");
    }

    if (hasContentLen && contentLength > 0)
    {
        const usize total = headerSize + static_cast<usize>(contentLength);
        if (avail < total)
        {
            // Guarantee the buffer can grow to hold the rest of the body.
            if (!_readBuffer.ensureWritable(total - avail))
                return ParseResult::Error;
            return ParseResult::Incomplete;
        }

        _req.setBody(std::string_view(p, static_cast<usize>(contentLength)));
        _consumed = total;
    }
    else
    {
        _req.setBody({});
        _consumed = headerSize;
    }

    _continueSent = false; // request fully framed; arm again for the next one
    return ParseResult::Complete;
}

void Session::sendErrorAndClose(i32 status, std::string_view message)
{
    _keepAlive = false;
    _closeAfterFlush = true;

    HttpResponse& response = _res;
    response.begin(&_writeBuffer, HTTP_VERSION, false, false);
    response.setStatus(status);
    response.setContentType(TEXT_CONTENT_TYPE);
    response.setBody(message);
}

void Session::handleRequest()
{
    constexpr HeaderMask kUpgradeBits =
        headerBits(HeaderType::Upgrade, HeaderType::SecWebSocketKey);

    if (_req.method() == Method::GET && hasHeader(_req.presentHeaders(), kUpgradeBits))
    {
        upgradeToWebSocket();
        return;
    }

    const bool headOnly = (_req.method() == Method::HEAD);

    HttpResponse& response = _res;
    response.begin(&_writeBuffer, HTTP_VERSION, _keepAlive, headOnly);

    try
    {
        // HEAD must be served by whatever answers GET, minus the payload.
        const Method lookup = headOnly ? Method::GET : _req.method();
        const EndpointManager* routes = EndpointManager::getInstance();

        // Passing &_req lets a pattern route capture its `:params` into it.
        Endpoint* endpoint = routes->getEndpoint(lookup, _req.path(), &_req);

        if (endpoint != nullptr)
        {
            endpoint->exec(_req, response);
        }
        else if (const u32 allowed = routes->allowedMethods(_req.path()))
        {
            // The path exists, just not for this method: 405 with Allow is
            // required by RFC 9110, and a bare 404 here would send clients
            // hunting for a routing bug that is really a verb mismatch.
            char allowBuf[96];
            response.setStatus(StatusCode::method_not_allowed);
            response.addHeader(HeaderType::Allow, formatAllow(allowed, allowBuf, sizeof(allowBuf)));
            response.setContentType(TEXT_CONTENT_TYPE);
            response.setBody("Method not allowed for this path.");
        }
        else
        {
            response.setStatus(StatusCode::not_found);
            response.setBody(kNotFoundBody);
        }
    }
    catch (const std::exception& e)
    {
        if (!response.isCommitted())
        {
            response.setStatus(StatusCode::internal_server_error);
            response.setContentType(TEXT_CONTENT_TYPE);
            response.setBody(e.what());
        }
        _keepAlive = false;
        _closeAfterFlush = true;
    }

    // A handler that returned without producing a body would otherwise leave
    // the client waiting for a response that never comes.
    response.finalize();

    if (response.failed())
    {
        _keepAlive = false;
        _closeAfterFlush = true;
    }
}

void Session::upgradeToWebSocket()
{
    const std::string_view upgradeHeader    = _req.getHeader(HeaderType::Upgrade);
    const std::string_view connectionHeader = _req.getHeader(HeaderType::Connection);
    const std::string_view wsKey            = _req.getHeader(HeaderType::SecWebSocketKey);
    const std::string_view wsVersion        = _req.getHeader(HeaderType::SecWebSocketVersion);

    // Browsers send "Connection: keep-alive, Upgrade", so this has to be a
    // token search rather than a whole-value comparison.
    if (!StringUtils::containsToken(upgradeHeader, WEBSOCKET_UPGRADE_HEADER) ||
        !StringUtils::containsToken(connectionHeader, UPGRADE_HEADER))
    {
        sendErrorAndClose(StatusCode::bad_request, "Invalid WebSocket upgrade request.");
        return;
    }

    if (wsVersion != WS_VERSION_13_HEADER)
    {
        _keepAlive = false;
        _closeAfterFlush = true;

        HttpResponse& response = _res;
        response.begin(&_writeBuffer, HTTP_VERSION, false, false);
        response.setStatus(StatusCode::upgrade_required);
        response.setContentType(TEXT_CONTENT_TYPE);
        response.addHeader(HeaderType::SecWebSocketVersion, WS_VERSION_13_HEADER);
        response.setBody("Unsupported WebSocket version.");
        return;
    }

    // Bound the key before it is concatenated into a fixed stack buffer: an
    // unbounded Sec-WebSocket-Key used to smash the stack right here.
    if (wsKey.empty() || wsKey.size() > ws::WS_MAX_KEY_LEN)
    {
        sendErrorAndClose(StatusCode::bad_request, "Invalid Sec-WebSocket-Key.");
        return;
    }

    WebSocketRoute* wsRoute = EndpointManager::getInstance()->getWebSocketEndpoint(_req.path());
    if (!wsRoute)
    {
        sendErrorAndClose(StatusCode::not_found, "No WebSocket route at this path.");
        return;
    }

    char material[ws::WS_MAX_KEY_LEN + ws::kWsGuid.size()];
    const usize matLen = wsKey.size() + ws::kWsGuid.size();
    std::memcpy(material, wsKey.data(), wsKey.size());
    std::memcpy(material + wsKey.size(), ws::kWsGuid.data(), ws::kWsGuid.size());

    const auto digest = ws::sha1Digest(std::string_view(material, matLen));
    const std::string accept = StringUtils::base64Encode(digest.data(), digest.size());

    constexpr std::string_view hsPart1 =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: ";
    constexpr std::string_view hsPart2 = "\r\n\r\n";

    if (!_writeBuffer.ensureWritable(hsPart1.size() + accept.size() + hsPart2.size()))
    {
        _keepAlive = false;
        _closeAfterFlush = true;
        return;
    }

    _writeBuffer.append(hsPart1);
    _writeBuffer.append(accept);
    _writeBuffer.append(hsPart2);

    _mode = ProtocolMode::WebSocket;
    _wsState.reset();
    _wsState.route = wsRoute;
    _keepAlive = true;
    _req.reset();

    if (wsRoute->onOpen)
    {
        WebSocketContext ctx(*this);
        wsRoute->onOpen(ctx);
    }
}

bool Session::drainWebSocket()
{
    WebSocketContext ctx(*this);
    const ws::WsAction action = ws::processFrames(
        _wsState, ctx, _readBuffer, _writeBuffer,
        Settings::getSettings().max_body_size);

    if (action == ws::WsAction::CloseAfterFlush)
    {
        _keepAlive = false;
        _closeAfterFlush = true;
    }

    return true;
}

bool Session::drainInput()
{
    if (_mode == ProtocolMode::WebSocket)
        return drainWebSocket();

    while (!_closeAfterFlush)
    {
        // Backpressure: never let a pipelining client outrun the socket.
        if (_writeBuffer.size() >= kWriteHighWater)
            break;

        const ParseResult result = parseRequest();

        if (result == ParseResult::Incomplete)
            break;

        if (result == ParseResult::Error)
        {
            sendErrorAndClose(StatusCode::bad_request, "Malformed request.");
            return true;
        }

        handleRequest();
        _readBuffer.advanceRead(_consumed);
        _consumed = 0;

        if (_mode == ProtocolMode::WebSocket)
        {
            // Anything left in the buffer after the handshake is already framed.
            return drainWebSocket();
        }

        if (!_keepAlive)
        {
            _closeAfterFlush = true;
            break;
        }
    }

    return true;
}

void Session::recycleBuffers()
{
    // Keep a burst from permanently pinning a grown buffer on an idle keep-alive
    // connection; with a million sockets this is the difference between a few
    // hundred MB and tens of GB of resident memory.
    const auto& settings = Settings::getSettings();

    if (_readBuffer.empty())
        _readBuffer.shrinkTo(settings.read_buffer_size);
    if (_writeBuffer.empty())
        _writeBuffer.shrinkTo(settings.write_buffer_size);
}

#ifdef USE_EPOLL

bool Session::flushWrites()
{
    while (_writeBuffer.size() > 0)
    {
        usize avail = 0;
        const char* out = _writeBuffer.readPtr(avail);

        const ssize_t sent = ::send(_socket, out, avail, MSG_NOSIGNAL);

        if (sent > 0)
        {
            _writeBuffer.advanceRead(static_cast<usize>(sent));
            continue;
        }

        if (sent < 0 && errno == EINTR)
            continue;

        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return true; // socket full: finish on EPOLLOUT

        return false;
    }

    return true;
}

void Session::updateEpollInterest()
{
    const bool needWrite = _writeBuffer.size() > 0;
    if (needWrite == _writeArmed)
        return;

    _writeArmed = needWrite;

    epoll_event ev{};
    ev.data.fd = _socket;
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET | (needWrite ? EPOLLOUT : 0u);
    epoll_ctl(_assignedEpollFd, EPOLL_CTL_MOD, _socket, &ev);
}

bool Session::onReadReady()
{
    for (;;)
    {
        usize space = 0;
        char* buf = _readBuffer.prepareWrite(kReadChunk, space);

        if (!buf)
        {
            // The buffer is at its hard cap and still holds an unfinished
            // request: the peer is sending more than we agreed to accept.
            sendErrorAndClose(StatusCode::payload_too_large, "Request entity too large.");
            break;
        }

        const ssize_t bytesRead = ::recv(_socket, buf, space, 0);

        if (bytesRead > 0)
        {
            _readBuffer.advanceWrite(static_cast<usize>(bytesRead));

            if (!drainInput())
                return false;

            if (!flushWrites())
                return false;

            if (_closeAfterFlush && _writeBuffer.empty())
                return false;

            if (_writeBuffer.size() >= kWriteHighWater)
            {
                // Stop reading until the socket drains. Edge-triggered epoll
                // will not re-notify us, so remember to resume from onWriteReady.
                _readSuspended = true;
                break;
            }

            // A short read means the receive queue is empty; a new edge will
            // arrive if more data shows up.
            if (static_cast<usize>(bytesRead) < space)
                break;

            continue;
        }

        if (bytesRead == 0)
            return false; // peer closed

        if (errno == EINTR)
            continue;

        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;

        return false;
    }

    if (_closeAfterFlush && _writeBuffer.empty())
        return false;

    updateEpollInterest();
    recycleBuffers();
    return !_wantClose;
}

bool Session::onWriteReady()
{
    if (!flushWrites())
        return false;

    if (_writeBuffer.empty())
    {
        if (_closeAfterFlush)
            return false;

        if (_readSuspended)
        {
            _readSuspended = false;
            return onReadReady();
        }
    }

    updateEpollInterest();
    return !_wantClose;
}

#endif // USE_EPOLL

#ifdef USE_IOURING

void Session::flipWriteBuffers()
{
    if (_flightBuffer.empty() && _writeBuffer.size() > 0)
    {
        _flightBuffer.swap(_writeBuffer);
        _writeBuffer.reset();
    }
}

void Session::onReadReady(io_uring_sqe* sqe)
{
    usize space = 0;
    char* buf = _readBuffer.prepareWrite(kReadChunk, space);

    if (!buf)
    {
        sendErrorAndClose(StatusCode::payload_too_large, "Request entity too large.");
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data(sqe, nullptr);
        return;
    }

    io_uring_prep_recv(sqe, _socket, buf, space, 0);
    io_uring_sqe_set_data(sqe, &_readReq);
    updateIoState(IO_READING, true);
}

void Session::onWriteReady(io_uring_sqe* sqe)
{
    flipWriteBuffers();

    usize avail = 0;
    const char* out = _flightBuffer.readPtr(avail);

    if (avail == 0)
    {
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data(sqe, nullptr);
        return;
    }

    // MSG_NOSIGNAL keeps a closed peer from raising SIGPIPE.
    if (avail >= kZeroCopyThreshold)
    {
        io_uring_prep_send_zc(sqe, _socket, out, avail, MSG_NOSIGNAL, 0);
        _zcInFlight = true;
    }
    else
    {
        io_uring_prep_send(sqe, _socket, out, avail, MSG_NOSIGNAL);
        _zcInFlight = false;
    }

    io_uring_sqe_set_data(sqe, &_writeReq);
    updateIoState(IO_WRITING, true);
}

bool Session::processRead(i32 bytesRecv, io_uring* ring)
{
    if (bytesRecv <= 0)
    {
        if (bytesRecv == -EAGAIN || bytesRecv == -EINTR)
        {
            io_uring_sqe* rSqe = io_uring_get_sqe(ring);
            if (rSqe) onReadReady(rSqe);
            return true;
        }

        // Peer closed or hard error: still flush whatever is already queued.
        _keepAlive = false;
        _closeAfterFlush = true;
        return hasQueuedOutput();
    }

    _readBuffer.advanceWrite(static_cast<usize>(bytesRecv));

    if (!drainInput())
        return false;

    // Shrinking a buffer reallocates it, which would free the memory a
    // just-armed SQE still points the kernel at. recycleBuffers() must run
    // while nothing is in flight, so it has to happen here — before either
    // buffer gets a new operation queued against it below — and never after.
    recycleBuffers();

    if (hasQueuedOutput() && !isWriteInFlight() && !isZcNotifInFlight())
    {
        io_uring_sqe* wSqe = io_uring_get_sqe(ring);
        if (wSqe) onWriteReady(wSqe);
    }

    if (_closeAfterFlush && !hasQueuedOutput())
        return false;

    if (!isReadInFlight() && !_closeAfterFlush && _writeBuffer.size() < kWriteHighWater)
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
        // Zero-copy notification: the NIC is done with the pages, so the bytes
        // recorded when the send completed can finally be released.
        _flightBuffer.advanceRead(_lockedZcBytes);
        _lockedZcBytes = 0;
        _zcInFlight = false;
    }
    else
    {
        if (bytesSent < 0)
        {
            if (bytesSent != -EAGAIN && bytesSent != -EINTR)
                return false;
        }
        else if (_zcInFlight)
        {
            // Wait for the F_NOTIF completion before touching the buffer.
            _lockedZcBytes = static_cast<usize>(bytesSent);
            return true;
        }
        else
        {
            _flightBuffer.advanceRead(static_cast<usize>(bytesSent));
        }
    }

    if (hasQueuedOutput())
    {
        if (!isWriteInFlight() && !isZcNotifInFlight())
        {
            io_uring_sqe* wSqe = io_uring_get_sqe(ring);
            if (wSqe) onWriteReady(wSqe);
        }
        return true;
    }

    if (_closeAfterFlush || !_keepAlive)
        return false;

    // See the comment in processRead(): this has to run before onReadReady()
    // arms a new SQE, never after, or a shrink can free memory the kernel was
    // just handed a pointer into.
    recycleBuffers();

    // Output drained: resume reading if backpressure had paused it.
    if (!isReadInFlight())
    {
        io_uring_sqe* rSqe = io_uring_get_sqe(ring);
        if (rSqe) onReadReady(rSqe);
    }

    return true;
}

#endif // USE_IOURING
