#include "Session.h"

#include <ink/LastWish.h>
#include <ink/utils.h>

#include "EventLoop/EventLoop.h"
#include "Managers/EndpointManager.h"
#include "Managers/SessionManagerWorker.h"
#include "Response/HttpResponse.h"
#include "Utils/RouteIdentifier.h"
#include "Settings/Settings.h"

Session::Session(socket_t socket, EventLoop* eventLoop) :
    _socket(socket),
    _req(),
    _keepAlive(false),
    _readBuffer(Settings::getSettings().max_request_size),
    _writeBuffer(Settings::getSettings().max_response_size),
    _writingResponse(false),
    _eventLoop(eventLoop)
{
    // Empty
}

Session::~Session()
{
    close();
}

void Session::start()
{
    // Do some socket optimizations
    setSocketOptimizations();
    // Register with event loop
    _eventLoop->addSession(shared_from_this());
}

void Session::close()
{
    socket_t fd = _socket.exchange(SOCKET_ERROR_VALUE);
    if (fd == SOCKET_ERROR_VALUE) return;
    // if (SessionManagerWorker::getInstance()->getSessionTableSize() > 4096)
    // {
    //     SessionManagerWorker::getInstance()->wake();
    // }
    ::close(fd);
}

void Session::setSocketOptimizations()
{
    // // Set TCP_NODELAY for client socket too
    // int opt = 1;
    // setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&opt), sizeof(opt));

    // Set socket to non-blocking mode
    int flags = fcntl(_socket, F_GETFL, 0);
    fcntl(_socket, F_SETFL, flags | O_NONBLOCK);
}

socket_t Session::getAssignedEpollFd() const noexcept
{
    return _assignedEpollFd;
}

void Session::setAssignedEpollFd(socket_t fd) noexcept
{
    _assignedEpollFd = fd;
}

socket_t Session::getSocket() const
{
    return _socket;
}

void Session::onReadReady()
{
    read();
}

void Session::onWriteReady()
{
    write();
}

void Session::updateActivity()
{
    _lastActivity.store(std::chrono::steady_clock::now());
}

bool Session::isIdle(std::chrono::milliseconds timeout) const noexcept
{
    auto now = std::chrono::steady_clock::now();
    auto last = _lastActivity.load();
    return (now - last) > timeout;
}

void Session::read()
{
    if (_writingResponse) return;

    size_t availableSpace;
    char* writePtr = _readBuffer.getWriteBuffer(availableSpace);

    if (!writePtr || availableSpace == 0)
    {
        // Buffer is full - expand or handle overflow
        INK_ERROR << "Read buffer full, closing connection";
        close();
        return;
    }

    int bytesRead = recv(_socket, writePtr, availableSpace, 0);

    if (bytesRead > 0)
    {
        _readBuffer.advanceWritePos(bytesRead);
        // INK_TRACE << "To Read on socket: " << _socket << " buffer: \n" << writePtr;

        // Try to parse the request - may be partial
        while (parseRequest())
        {
            handleRequest();

            if (_writeBuffer.size() > 0)
            {
                _eventLoop->updateSessionInterest(shared_from_this(), SessionInterest::ON_WRITE);
                return;
            }
        }

        if (!_writingResponse)
        {
            _eventLoop->updateSessionInterest(shared_from_this(), SessionInterest::ON_READ);
        }
    }
    else if (bytesRead == 0)
    {
        // Connection closed by peer
        INK_DEBUG << "Connection closed by peer";
        close();
    }
    else
    {
        // Error or would block
        int errorCode = errno;
        if (errorCode != EAGAIN && errorCode != EWOULDBLOCK &&
            errorCode != EBADF && errorCode != ECONNRESET)
        {
            INK_ERROR << "Socket read error: " << errorCode << " (" << strerror(errorCode) << ")";
        }

        close();
    }
}

bool Session::parseRequest()
{
    size_t availableData;
    const char* data = _readBuffer.getReadBuffer(availableData);
    if (!data || availableData < MIN_REQUEST_SIZE) return false;

    std::string_view buffer(data, availableData);

    // Find end of headers: "\r\n\r\n"
    size_t headerEndPos = buffer.find("\r\n\r\n");
    if (headerEndPos == std::string_view::npos) return false;
    const char* endOfHeaders = data + headerEndPos + 4;

    // Find end of request line: first "\r\n"
    size_t lineEndPos = buffer.find("\r\n");
    if (lineEndPos == std::string_view::npos) return false;
    const char* lineEnd = data + lineEndPos;

    std::string_view requestLine(data, lineEnd - data);

    // Find first space (after method)
    size_t methodEnd = requestLine.find(' ');
    if (methodEnd == std::string_view::npos) return false;

    // Find second space (after path)
    size_t pathEnd = requestLine.find(' ', methodEnd + 1);
    if (pathEnd == std::string_view::npos) return false;

    std::string_view method = requestLine.substr(0, methodEnd);
    std::string_view path = requestLine.substr(methodEnd + 1, pathEnd - methodEnd - 1);
    std::string_view version = requestLine.substr(pathEnd + 1);

    _req.setMethod(HttpRequest::parseMethod(method));
    _req.setPath(path);
    _req.setVersion(version);

    // Parse headers in a single pass
    const char* headerStart = lineEnd + 2;
    const char* headerEnd = headerStart;
    std::string_view connectionValue;
    std::string_view contentLengthValue;

    while (headerEnd < endOfHeaders - 2)
    {
        headerEnd = nullptr;
        for (const char* p = headerStart; p < endOfHeaders - 1; p++)
        {
            if (*p == '\r' && *(p+1) == '\n')
            {
                headerEnd = p;
                break;
            }
        }
        if (!headerEnd)
            break;

        std::string_view headerLine(headerStart, headerEnd - headerStart);
        if (headerLine.empty())
            break;

        size_t colonPos = headerLine.find(": ");
        if (colonPos != std::string_view::npos)
        {
            std::string_view key = headerLine.substr(0, colonPos);
            std::string_view value = headerLine.substr(colonPos + 2);

            // Add header to request
            _req.addHeader(std::string(key), std::string(value));

            // Track important headers by direct comparison (avoiding extra lookups)
            if (Conversions::iequals(key, "Connection"))
                connectionValue = value;
            else if (Conversions::iequals(key, "Content-Length"))
                contentLengthValue = value;
        }
        headerStart = headerEnd + 2;
    }

    // Set connection state
    _keepAlive = Conversions::iequals(connectionValue, "keep-alive");

    // Process the body if Content-Length is present
    if (!contentLengthValue.empty())
    {
        // Use fast string-to-int conversion avoiding exceptions from std::stoul
        size_t contentLength = ink::utils::string_int(contentLengthValue);

        // guard against overflow for security
        if (contentLength > Settings::getSettings().max_body_size)
            return false;

        size_t headersSize = endOfHeaders - data;
        // There is no BODY
        if (availableData < headersSize + contentLength)
            return false;

        std::string_view bodyView(endOfHeaders, contentLength);

        // Set body without additional copies
        _req.setBody(bodyView);
        _readBuffer.advanceReadPos(headersSize + contentLength);
    } else {
        // No Content-Length, assume no body
        _readBuffer.advanceReadPos(endOfHeaders - data);
    }

    return true;
}

void Session::handleRequest()
{
    auto start = std::chrono::high_resolution_clock::now();

    HttpResponse response;
    response.setVersion("HTTP/1.1");
    response.addHeader("Server", "WarpApi/1.0");

    try {
        _req.extractQueryParams();
        const std::string endpoint_id = RouteIdentifier::generateIdentifier(_req.path(), _req.method());
        auto endpoint = EndpointManager::getEndpoint(endpoint_id);

        if (endpoint != nullptr) {
            endpoint->exec(_req, response);
        } else {
            response.setStatus(StatusCode::not_found);
            response.setBody("Endpoint not found.");
        }
    } catch (const std::exception& e) {
        response.setStatus(StatusCode::internal_server_error);
        response.setBody("Internal Server error: " + std::string(e.what()));
    }

    // Set Connection header based on keep-alive
    if (_keepAlive)
    {
        response.addHeader("Connection", "keep-alive");
        // response.addHeader("Keep-Alive", "timeout=5, max=100");
        updateActivity();
    }
    else
    {
        response.addHeader("Connection", "close");
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    // Serialize response to the write buffer
    std::string responseStr = response.toString();
    _writeBuffer.write(responseStr.c_str(), responseStr.length());

    INK_INFO << ">> " << (int)response.getStatus() << " "
             << (int)_req.method() << " "
             << _req.path() << " "
             << std::fixed << std::setprecision(4) << elapsed.count() << 's';

    _writingResponse = true;
}

void Session::write()
{
    size_t availableData;
    const char* readPtr = _writeBuffer.getReadBuffer(availableData);

    if (!readPtr || availableData == 0) {
        onWriteComplete();
        return;
    }

    int bytesSent = send(_socket, readPtr, availableData, 0);
    // INK_TRACE << "Wrote " << bytesSent << " response: \n" << readPtr;

    if (bytesSent > 0)
    {
        _writeBuffer.advanceReadPos(bytesSent);

        // Check if we've written everything
        if (_writeBuffer.size() > 0) {
            // Still more to write
            _eventLoop->updateSessionInterest(shared_from_this(), SessionInterest::ON_WRITE);
        }
        else
        {
            // All data sent
            onWriteComplete();
        }
    }
    else
    {
        // Error or would block
        int errorCode = errno;
        if (errorCode == EAGAIN && errorCode == EWOULDBLOCK)
        {
            // Socket buffer full. Keep waiting for ON_WRITE.
            _eventLoop->updateSessionInterest(shared_from_this(), SessionInterest::ON_WRITE);
        }
        else
        {
            INK_ERROR << "Write Error: " << errno;
            close();
        }
    }
}

void Session::onWriteComplete()
{
    _writingResponse = false;

    if (_keepAlive)
    {
        _req.reset();

        size_t availableRead;
        _readBuffer.getReadBuffer(availableRead);

        if (availableRead > 0)
        {
            // data is available, parse it immediately
            // don't need to wait for EPOLLIN
            read();
        }
        else
        {
            // No data, wait for EPOLLIN
            _eventLoop->updateSessionInterest(shared_from_this(), SessionInterest::ON_READ);
        }
    }
    else
    {
        close();
    }
}
