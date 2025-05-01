#include "Session.h"
#include "Managers/EndpointManager.h"
#include "Response/HttpResponse.h"
#include "Utils/RouteIdentifier.h"
#include "Settings/Settings.h"

Session::Session(socket_t socket, EventLoop* eventLoop) :
    _socket(socket),
    _req(),
    _keepAlive(false),
    _readBuffer(Settings::getSettings().max_request_size),
    _writeBuffer(Settings::getSettings().max_response_size),
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
        // INK_TRACE << "To Read on socket: " << _socket << " buffer: \n" << writePtr;

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
            if (key == "Connection")
                connectionValue = value;
            else if (key == "Content-Length")
                contentLengthValue = value;
        }
        headerStart = headerEnd + 2;
    }

    // Set connection state
    _keepAlive = (connectionValue == "keep-alive");

    // Process the body if Content-Length is present
    if (!contentLengthValue.empty())
    {
        // Use fast string-to-int conversion avoiding exceptions from std::stoul
        size_t contentLength = 0;
        for (char c : contentLengthValue)
        {
            if (c >= '0' && c <= '9') {
                contentLength = contentLength * 10 + (c - '0');
            }
            else {
                return false;
            }
        }

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
    HttpResponse response;
    response.addHeader("Server", "WarpApi/1.0");

    // Set Connection header based on keep-alive
    if (_keepAlive)
        response.addHeader("Connection", "keep-alive");
    else
        response.addHeader("Connection", "close");

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

    // Start writing the response
    _writingResponse = true;
    _eventLoop->updateSessionInterest(shared_from_this(), false, true);

    size_t written = _writeBuffer.write(responseStr.c_str(), responseStr.length());

    if (written < responseStr.length())
    {
        INK_ERROR << "Failed to write complete response to buffer";
        _keepAlive = false;  // Force connection close on error
    }

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

        if (_keepAlive)
        {
            _eventLoop->updateSessionInterest(shared_from_this(), true, false);

            // Reset request state
            _req.reset();
        }
        // Not keep-alive, close connection
        else
        {
            close();
        }

        return;
    }

    int bytesSent = send(_socket, readPtr, availableData, 0);

    if (bytesSent > 0)
    {
        _writeBuffer.advanceReadPos(bytesSent);

        // Check if we've written everything
        if (_writeBuffer.size() > 0) {
            // Still more to write
            _eventLoop->updateSessionInterest(shared_from_this(), false, true);
        }
        else
        {
            // All data written
            _writingResponse = false;

            if (_keepAlive) {
                // Prepare for next request if keep-alive
                _eventLoop->updateSessionInterest(shared_from_this(), true, false);
                // Reset request for next use
                _req.reset();
            }
            else
            {
                // Not keep-alive, close the connection
                close();
            }
        }
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
