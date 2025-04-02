#include "Session.h"
#include "Managers/EndpointManager.h"
#include "Response/HttpResponse.h"
#include "Utils/RouteIdentifier.h"

Session::Session(socket_t socket, EventLoop* eventLoop) :
    _socket(socket),
    _req(),
    _keepAlive(false),
    _readBuffer(16384),  // 16KB read buffer
    _writeBuffer(16384), // 16KB write buffer
    _active(true),
    _readingHeaders(true),
    _writingResponse(false),
    _eventLoop(eventLoop)
{
    INK_DEBUG << "Session Created!";
}

Session::~Session()
{
    close();
    INK_DEBUG << "Session Closed!";
}

void Session::start()
{
    // Register with event loop
    _eventLoop->addSession(shared_from_this());
    // Register interest in reading
    _eventLoop->updateSessionInterest(shared_from_this(), true, false);
}

void Session::close()
{
    if (_active)
    {
        _active = false;

        _eventLoop->removeSession(shared_from_this());

#ifdef _WIN32
        closesocket(_socket);
#else
        ::close(_socket);
#endif
    }
}

bool Session::setNonBlocking()
{
#ifdef _WIN32
    unsigned long nonBlocking = 1;
    return ioctlsocket(_socket, FIONBIO, &nonBlocking) == 0;
#else
    int flags = fcntl(_socket, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(_socket, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

std::thread::id Session::getWorkerId() const noexcept
{
    return _workerId;
}

void Session::setWorkerId(std::thread::id workerId) noexcept
{
    _workerId = workerId;
}

const bool Session::isActive() const noexcept
{
    return _active;
}

socket_t Session::getSocket() const
{
    return _socket;
}

void Session::onReadReady()
{
    if (!_active) return;
    read();
}

void Session::onWriteReady()
{
    if (!_active) return;
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
    if (!_active) return;

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

    if (bytesRead > 0) {
        _readBuffer.advanceWritePos(bytesRead);
        INK_TRACE << "To Read on socket: " << _socket << " buffer: \n" << writePtr;

        // Try to parse the request - may be partial
        if (parseRequest())
        {
            handleRequest();
        }
        else
        {
            // Need more data, continue reading
            _eventLoop->updateSessionInterest(shared_from_this(), true, false);
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
        int errorCode =
#ifdef _WIN32
            WSAGetLastError();
        if (errorCode != WSAEWOULDBLOCK) {
#else
            errno;
        if (errorCode != EAGAIN && errorCode != EWOULDBLOCK)
        {
#endif
            INK_ERROR << "Socket read error: " << errorCode;
            close();
        }
        else
        {
            // Would block, try again later
            _eventLoop->updateSessionInterest(shared_from_this(), true, false);
        }
    }
}

bool Session::parseRequest()
{
    // Minimum viable HTTP request size check
    if (_readBuffer.size() < 16)
        return false;

    size_t availableData;
    const char* data = _readBuffer.getReadBuffer(availableData);

    if (!data || availableData == 0)
        return false;

    // Search for end of headers delimiter
    const char* endOfHeaders = nullptr;
    for (size_t i = 0; i < availableData - 3; i++) {
        if (data[i] == '\r' && data[i+1] == '\n' && data[i+2] == '\r' && data[i+3] == '\n') {
            endOfHeaders = data + i + 4;
            break;
        }
    }

    if (!endOfHeaders) {
        // Headers aren't complete yet
        return false;
    }

    // Parse request line
    const char* lineEnd = strstr(data, "\r\n");
    if (!lineEnd) return false;

    std::string_view requestLine(data, lineEnd - data);
    INK_TRACE << "requestLine: " << requestLine;

    // Find first space (after method)
    size_t methodEnd = requestLine.find(' ');
    if (methodEnd == std::string_view::npos) return false;

    // Find second space (after path)
    size_t pathEnd = requestLine.find(' ', methodEnd + 1);
    if (pathEnd == std::string_view::npos) return false;

    std::string_view method = requestLine.substr(0, methodEnd);
    std::string_view path = requestLine.substr(methodEnd + 1, pathEnd - methodEnd - 1);
    std::string_view version = requestLine.substr(pathEnd + 1);

    _req.setMethod(HttpRequest::parseMethod(std::string(method)));
    _req.setPath(std::string(path));
    _req.setVersion(std::string(version));

    // Parse headers
    const char* headerStart = lineEnd + 2;
    const char* headerLineEnd = headerStart;

    while (headerLineEnd < endOfHeaders - 2) {
        headerLineEnd = strstr(headerStart, "\r\n");
        if (!headerLineEnd) break;

        std::string_view headerLine(headerStart, headerLineEnd - headerStart);

        if (headerLine.empty()) {
            break;
        }

        size_t colonPos = headerLine.find(": ");
        if (colonPos != std::string_view::npos) {
            std::string key(headerLine.substr(0, colonPos));
            std::string value(headerLine.substr(colonPos + 2));
            _req.addHeader(key, value);

            // Check for Connection header for keep-alive
            if (key == "Connection") {
                _keepAlive = (value == "keep-alive");
            }
        }

        headerStart = headerLineEnd + 2;
    }

    // Check if we have the full body based on Content-Length
    std::string contentLengthStr = _req.getHeader("Content-Length");
    if (!contentLengthStr.empty()) {
        size_t contentLength = std::stoul(contentLengthStr);
        size_t headersSize = endOfHeaders - data;

        if (availableData < headersSize + contentLength) {
            // Need more data for complete body
            return false;
        }

        // We have the full body, set it
        _req.setBody(std::string(endOfHeaders, contentLength));

        // Advance the read position past this complete request
        _readBuffer.advanceReadPos(headersSize + contentLength);
    } else {
        // No Content-Length, assume no body
        _readBuffer.advanceReadPos(endOfHeaders - data);
    }

    return true;
}

void Session::handleRequest()
{
    HttpResponse response;
    response.addHeader("Server", "WarpApi/1.0");

    // Set Connection header based on keep-alive
    if (_keepAlive) {
        response.addHeader("Connection", "keep-alive");
    } else {
        response.addHeader("Connection", "close");
    }

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

    // Serialize response to the write buffer
    std::string responseStr = response.toString();

    size_t written = _writeBuffer.write(responseStr.c_str(), responseStr.length());

    if (written < responseStr.length()) {
        INK_ERROR << "Failed to write complete response to buffer";
        _keepAlive = false;  // Force connection close on error
    }

    // Start writing the response
    _writingResponse = true;
    _eventLoop->updateSessionInterest(shared_from_this(), false, true);

    updateActivity();
}

void Session::write()
{
    if (!_active) return;

    size_t availableData;
    const char* readPtr = _writeBuffer.getReadBuffer(availableData);

    if (!readPtr || availableData == 0) {
        // No data to write
        _writingResponse = false;

        if (_keepAlive) {

            _eventLoop->updateSessionInterest(shared_from_this(), true, false);

            // Reset request state
            _req.reset();
        } else {
            // Not keep-alive, close connection
            close();
        }

        return;
    }

    int bytesSent = send(_socket, readPtr, availableData, 0);

    if (bytesSent > 0) {
        _writeBuffer.advanceReadPos(bytesSent);

        // Check if we've written everything
        if (_writeBuffer.size() > 0) {
            // Still more to write
            _eventLoop->updateSessionInterest(shared_from_this(), false, true);

        } else {
            // All data written
            _writingResponse = false;

            if (_keepAlive) {
                // Prepare for next request if keep-alive
                _eventLoop->updateSessionInterest(shared_from_this(), true, false);
                // Reset request for next use
                _req.reset();
            } else {
                // Not keep-alive, close the connection
                close();
            }
        }
    } else {
        // Error or would block
        int errorCode =
#ifdef _WIN32
            WSAGetLastError();
        if (errorCode != WSAEWOULDBLOCK) {
#else
            errno;
        if (errorCode != EAGAIN && errorCode != EWOULDBLOCK) {
#endif
            INK_ERROR << "Socket write error: " << errorCode;
            close();
        }
        else
        {
            // Would block, try again later
            _eventLoop->updateSessionInterest(shared_from_this(), false, true);
        }
    }
}
