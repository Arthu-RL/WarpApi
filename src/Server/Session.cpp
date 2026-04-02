#include "Session.h"

#include <ink/LastWish.h>
#include <ink/utils.h>

#include "EventLoop/EventLoop.h"
#include "Managers/EndpointManager.h"
#include "Response/HttpResponse.h"
#include "Utils/StringUtils.h"
#include "Settings/Settings.h"

Session::Session(socket_t socket) :
    _socket(socket),
    _req(),
    _keepAlive(false),
    _readBuffer(Settings::getSettings().max_request_size),
    _writeBuffer(Settings::getSettings().max_response_size)
{
    // Empty
}

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

socket_t Session::getSocket() const
{
    return _socket;
}

void Session::onReadReady(io_uring_sqe* sqe)
{
    size_t availableSpace;
    char* buf = _readBuffer.getWriteBuffer(availableSpace);

    if (availableSpace == 0)
    {
        this->close();
        // disarm the sqe
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data(sqe, nullptr);
        return;
    }

    // receive the request
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
        // disarm the sqe
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data(sqe, nullptr);
        return;
    }

    // MSG_NOSIGNAL prevents EPIPE from killing the process
    // if the client closed the connection.
    io_uring_prep_send_zc(sqe, _socket, readBuf, availableSpace, MSG_NOSIGNAL, 0);

    // Tag the SQE so we know which session sent the data
    io_uring_sqe_set_data(sqe, &_writeReq);

    updateIoState(IO_WRITING, true);
}

bool Session::processRead(i32 bytesRead, io_uring* ring)
{
    updateIoState(IO_READING, false);

    if (bytesRead <= 0)
    {
        // If the client sent a FIN (0) or there was an error (< 0)
        // We only close immediately if our write buffer is empty.
        if (_writeBuffer.size() == 0) {
            this->close();
            return false;
        }

        _keepAlive = false;
        setStatus(SessionStatus::Closing);
        return true;
    }

    _readBuffer.advanceWritePos(bytesRead);

    while (parseRequest())
    {
        INK_LOG << "request parsed";
        handleRequest();
    }

    INK_LOG << "after request handle";

    if (_writeBuffer.size() > 0 && !isWriteInFlight())
    {
        io_uring_sqe* wSqe = io_uring_get_sqe(ring);
        if (wSqe)
        {
            onWriteReady(wSqe);
        }
    }

    // Re-arm Read if not reading already
    if (!isReadInFlight() && _status == SessionStatus::Active)
    {
        io_uring_sqe* rSqe = io_uring_get_sqe(ring);
        if (rSqe) onReadReady(rSqe);
    }

    return true;
}

bool Session::processWrite(i32 bytesRead, bool is_notif, io_uring* ring)
{
    if (is_notif)
    {
        // Zero-Copy Notification (CQE 2)
        // The NIC is finally done with the memory. We can safely advance our buffer.
        updateIoState(IO_WAITING_ZC, false);

        _writeBuffer.advanceReadPos(_lockedZcBytes);
        _lockedZcBytes = 0;

        // If we had a partial send, re-arm the rest of the buffer now
        if (_writeBuffer.size() > 0)
        {
            io_uring_sqe* wSqe = io_uring_get_sqe(ring);
            if (wSqe) onWriteReady(wSqe);
            return true;
        }

        // Handle Keep-Alive logic only after the buffer is fully flushed and unlocked
        if (!_keepAlive)
        {
            this->close();
            return false;
        }

        return true;
    }

    // Send Result (CQE 1)
    updateIoState(IO_WRITING, false);

    if (bytesRead < 0)
    {
        this->close();
        return false;
    }

    // Record how many bytes the kernel accepted, but DO NOT advance
    // the buffer yet. The memory is still pinned by the NIC!
    _lockedZcBytes = bytesRead;

    return true;
}

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
        // End of headers
        if (__builtin_expect(p + 1 < end && p[0] == '\r' && p[1] == '\n', 0))
        {
            p += 2;
            break;
        }

        const char* hEnd = StringUtils::find_crlf(p, end);
        if (__builtin_expect(!hEnd || hEnd + 1 >= end || hEnd[1] != '\n', 0))
        {
            return false;
        }

        const char* colon = p;
        while (colon < hEnd && *colon != ':')
        {
            ++colon;
        }

        if (colon == hEnd)
        {
            p = hEnd + 2;
            continue;
        }

        // const char* k = p;
        size_t klen = colon - p;

        const char* v = colon + 1;
        if (v < hEnd && *v == ' ')
        {
            ++v;
        }
        size_t vlen = hEnd - v;

        HeaderType key = HeaderType::COUNT;

        switch (klen)
        {
            case 10: // Connection
                if (vlen == 10)
                {
                    _keepAlive = true;
                }
                else
                {
                    _keepAlive = false;
                }
                key = HeaderType::Connection;
                break;
            case 14: // Content-Length
                contentLength = StringUtils::fast_atoi(v, vlen);
                hasContentLen = true;
                if (contentLength > Settings::getSettings().max_body_size)
                {
                    return false;
                }
                key = HeaderType::ContentLength;
                break;
        }

        _req.addHeader(key, v, vlen);
        p = hEnd + 2;
    }

    // BODY
    if (hasContentLen && contentLength > 0)
    {
        size_t headerSize = p - data;
        if (avail < headerSize + contentLength)
        {
            return false;
        }

        _req.setBody(std::string_view(p, contentLength));
        _readBuffer.advanceReadPos(headerSize + contentLength);
    }
    else
    {
        _readBuffer.advanceReadPos(p - data);
    }

    return true;
}


void Session::handleRequest()
{
    // auto start = std::chrono::high_resolution_clock::now();

    HttpResponse response;
    response.setVersion(HTTP_VERSION);
    response.addHeader(HeaderType::Server, APP_INFO_HEADER);
    response.initBody(&_writeBuffer);

    // Set Connection header based on keep-alive
    if (_keepAlive)
    {
        response.addHeader(HeaderType::Connection, KEEP_ALIVE_HEADER);
    }
    else
    {
        response.addHeader(HeaderType::Connection, CLOSE_CONN_HEADER);
        // If no keep-alive, mark the status as Closing
        // so processRead/Write know to shut down after the buffer is flushed.
        setStatus(SessionStatus::Closing);
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
            setStatus(SessionStatus::Closing);
            _keepAlive = false;
        }
        response.setBody("Internal Server error: " + std::string(e.what()));
    }

    // auto end = std::chrono::high_resolution_clock::now();
    // std::chrono::duration<double> elapsed = end - start;

    // INK_INFO << ">> " << (int)response.getStatus() << " "
    //          << (int)_req.method() << " "
    //          << _req.path() << " "
    //          << std::fixed << std::setprecision(4) << elapsed.count() << 's';
}
