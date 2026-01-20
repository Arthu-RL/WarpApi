#include "Session.h"

#include <ink/LastWish.h>
#include <ink/utils.h>
#include <emmintrin.h>

#include "EventLoop/EventLoop.h"
#include "Managers/EndpointManager.h"
#include "Response/HttpResponse.h"
#include "Utils/RouteIdentifier.h"
#include "Settings/Settings.h"

constexpr uint32_t fnv1a_hash(const char* str, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        char c = str[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

constexpr uint32_t HASH_CONNECTION = fnv1a_hash("connection", 10);
constexpr uint32_t HASH_CONTENT_LENGTH = fnv1a_hash("content-length", 14);
constexpr uint32_t HASH_HOST = fnv1a_hash("host", 4);
constexpr uint32_t HASH_USER_AGENT = fnv1a_hash("user-agent", 10);

inline uint32_t hashHeaderName(const char* str, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        char c = str[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

inline const char* find_crlf(const char* data, const char* end) {
    const char* p = data;

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86_FP)
    if (end - p >= 16) {
        const __m128i cr = _mm_set1_epi8('\r');

        while (end - p >= 16) {
            __m128i chunk = _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(p));

            __m128i cmp = _mm_cmpeq_epi8(chunk, cr);
            int mask = _mm_movemask_epi8(cmp);

            if (mask) {
                return p + __builtin_ctz(mask);
            }
            p += 16;
        }
    }
#endif

    // Scalar tail
    while (p < end) {
        if (*p == '\r')
            return p;
        ++p;
    }

    return nullptr;
}

inline bool is_crlf(const char* p, const char* end) {
    return (p + 1 < end) && (p[0] == '\r') && (p[1] == '\n');
}

inline bool is_header_end(const char* p, const char* end) {
    return (p + 3 < end) &&
           (p[0] == '\r') && (p[1] == '\n') &&
           (p[2] == '\r') && (p[3] == '\n');
}

inline bool iequals_small(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;

    const char* pa = a.data();
    const char* pb = b.data();
    size_t len = a.size();

    // Unroll loop for common cases
    switch (len) {
    case 10: // "keep-alive"
        if ((pa[0] | 32) != (pb[0] | 32)) return false;
        if ((pa[1] | 32) != (pb[1] | 32)) return false;
        if ((pa[2] | 32) != (pb[2] | 32)) return false;
        if ((pa[3] | 32) != (pb[3] | 32)) return false;
        if ((pa[4] | 32) != (pb[4] | 32)) return false;
        if ((pa[5] | 32) != (pb[5] | 32)) return false;
        if ((pa[6] | 32) != (pb[6] | 32)) return false;
        if ((pa[7] | 32) != (pb[7] | 32)) return false;
        if ((pa[8] | 32) != (pb[8] | 32)) return false;
        if ((pa[9] | 32) != (pb[9] | 32)) return false;
        return true;

    case 5: // "close"
        if ((pa[0] | 32) != (pb[0] | 32)) return false;
        if ((pa[1] | 32) != (pb[1] | 32)) return false;
        if ((pa[2] | 32) != (pb[2] | 32)) return false;
        if ((pa[3] | 32) != (pb[3] | 32)) return false;
        if ((pa[4] | 32) != (pb[4] | 32)) return false;
        return true;

    default:
        for (size_t i = 0; i < len; ++i) {
            if ((pa[i] | 32) != (pb[i] | 32)) return false;
        }
        return true;
    }
}

inline size_t fast_atoi(const char* str, size_t len) {
    size_t result = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = str[i];
        if (c < '0' || c > '9') break;
        result = result * 10 + (c - '0');
    }
    return result;
}

Session::Session(socket_t socket, socket_t assignedEpollFd) :
    _socket(socket),
    _assignedEpollFd(assignedEpollFd),
    _req(),
    _keepAlive(false),
    _writingResponse(false),
    _readBuffer(Settings::getSettings().max_request_size),
    _writeBuffer(Settings::getSettings().max_response_size)
{
    updateActivity();
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

socket_t Session::getAssignedEpollFd() const noexcept
{
    return _assignedEpollFd;
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
    _lastActivity = std::chrono::steady_clock::now();
}

bool Session::isIdle(std::chrono::milliseconds timeout) const noexcept
{
    return (std::chrono::steady_clock::now() - _lastActivity) > timeout;
}

void Session::updateIoInterest(SessionInterest interest) noexcept
{
    if (_socket == SOCKET_ERROR_VALUE) return;

    struct epoll_event ev;
    ev.data.fd = _socket;
    ev.events = EPOLLET;
    if (interest == SessionInterest::ON_READ)
    {
        ev.events |= EPOLLIN;
    }
    else
    {
        ev.events |= EPOLLOUT;
    }

    epoll_ctl(_assignedEpollFd, EPOLL_CTL_MOD, _socket, &ev);
}
void Session::read()
{
    if (_writingResponse) return;

    while (true)
    {
        size_t availableSpace;
        char* writePtr = _readBuffer.getWriteBuffer(availableSpace);

        if (!writePtr || availableSpace == 0) {
            INK_ERROR << "Read buffer full";
            close();
            return;
        }

        int bytesRead = recv(_socket, writePtr, availableSpace, 0);

        if (bytesRead > 0)
        {
            _readBuffer.advanceWritePos(bytesRead);

            // Process request recved
            while (parseRequest())
            {
                handleRequest();
                if (_writeBuffer.size() > 0)
                {
                    updateIoInterest(SessionInterest::ON_WRITE);
                    return;
                }
            }
            if (bytesRead < (int)availableSpace)
            {
                break;
            }
        }
        else if (bytesRead == 0) {
            close();
            return;
        }
        else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // SEmpty socket wait for next Epoll event
            }
            close();
            return;
        }
    }

    if (!_writingResponse)
    {
        updateIoInterest(SessionInterest::ON_READ);
    }
}

bool Session::parseRequest()
{
    size_t avail;
    const char* data = _readBuffer.getReadBuffer(avail);
    if (__builtin_expect(!data || avail < MIN_REQUEST_SIZE, 0))
        return false;

    const char* p   = data;
    const char* end = data + avail;

    // REQUEST LINE
    const char* lineEnd = find_crlf(p, end);
    if (__builtin_expect(!lineEnd || lineEnd + 1 >= end || lineEnd[1] != '\n', 0))
        return false;

    // METHOD
    const char* methodEnd = p;
    while (methodEnd < lineEnd && *methodEnd != ' ') ++methodEnd;
    if (__builtin_expect(methodEnd == lineEnd, 0)) return false;

    std::string_view method(p, methodEnd - p);

    // PATH
    const char* pathStart = methodEnd + 1;
    const char* pathEnd   = pathStart;
    while (pathEnd < lineEnd && *pathEnd != ' ') ++pathEnd;
    if (__builtin_expect(pathEnd == lineEnd, 0)) return false;

    std::string_view path(pathStart, pathEnd - pathStart);

    // VERSION
    const char* verStart = pathEnd + 1;
    std::string_view version(verStart, lineEnd - verStart);

    _req.setMethod(HttpRequest::parseMethod(method));
    _req.setPath(path);
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

        const char* hEnd = find_crlf(p, end);
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

        const char* k = p;
        size_t klen = colon - p;

        const char* v = colon + 1;
        if (v < hEnd && *v == ' ')
        {
            ++v;
        }
        size_t vlen = hEnd - v;

        // hash
        uint32_t h = 2166136261u;
        for (const char* s = k; s < k + klen; ++s)
        {
            h ^= uint8_t(*s | 32);
            h *= 16777619u;
        }

        switch (h)
        {
        case HASH_CONNECTION:
            if (klen == 10)
            {
                _keepAlive = iequals_small(std::string_view(v, vlen), "keep-alive");
            }
            break;

        case HASH_CONTENT_LENGTH:
            if (klen == 14)
            {
                contentLength = fast_atoi(v, vlen);
                hasContentLen = true;
                if (contentLength > Settings::getSettings().max_body_size)
                {
                    return false;
                }
            }
            break;
        }

        _req.addHeader(k, klen, v, vlen);
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

        if (_writeBuffer.size() > 0)
        {
            // more to write
            updateIoInterest(SessionInterest::ON_WRITE);
        }
        else
        {
            // All data sent
            onWriteComplete();
        }
    }
    else
    {
        if (errno == EAGAIN && errno == EWOULDBLOCK)
        {
            // Socket buffer full. Keep waiting for ON_WRITE.
            updateIoInterest(SessionInterest::ON_WRITE);
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
            updateIoInterest(SessionInterest::ON_WRITE);
        }
    }
    else
    {
        close();
    }
}
