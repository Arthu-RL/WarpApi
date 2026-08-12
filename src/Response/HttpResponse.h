#ifndef HTTPRESPONSE_H
#define HTTPRESPONSE_H

#pragma once

#include <array>

#include "Utils/ByteBuffer.h"
#include "Utils/HeadersList.h"
#include "Utils/StringUtils.h"

/**
 * @brief Per-thread cache of the constant part of a 200 OK response.
 *
 * Server / Date / Content-Type do not change between requests, and Date only
 * changes once per second. Rebuilding that prefix per request costs several
 * short memcpys plus a gmtime call; caching it turns the common case into a
 * single contiguous blit.
 */
class WARP_API ResponsePrelude {
public:
    /** Status line + Server + Date + Content-Type for a 200 OK, refreshed at most once per second. */
    static std::string_view commonOk() noexcept;
    /** Just "Date: <imf-fixdate>\r\n", for non-200 responses. */
    static std::string_view dateLine() noexcept;
};

struct WARP_API HttpResponseData {
    i32 status = StatusCode::ok;
    std::string_view version;

    /**
     * @note Deliberately uninitialized and never bulk-cleared. Slots are only
     *       ever read back through active_headers[0, header_count), every one
     *       of which is written before it is read, so stale contents from the
     *       previous response on this connection are unobservable. Zeroing
     *       these two arrays cost ~360 bytes of memset per request.
     */
    std::array<std::string_view, MAX_HEADERS_SIZE> headers;
    std::array<HeaderType, MAX_HEADERS_SIZE> active_headers;

    u32 header_count = 0;
    HeaderMask present = 0;

    ByteBuffer* body = nullptr;
};

class WARP_API HttpResponse
{
public:
    HttpResponse() = default;

    static bool writeAll(ByteBuffer& out, const char* data, size_t len)
    {
        return out.append(data, len);
    }

    /**
     * @brief Binds the response to a connection's write buffer.
     * @param headOnly Suppresses the payload (HEAD) while keeping Content-Length.
     */
    void begin(ByteBuffer* writeBuffer, std::string_view version, bool keepAlive, bool headOnly) noexcept
    {
        _data.status = StatusCode::ok;
        _data.version = version;
        _data.header_count = 0;
        _data.present = 0;
        _data.body = writeBuffer;
        _keepAlive = keepAlive;
        _headOnly = headOnly;
        _committed = false;
        _failed = false;
    }

    i32 getStatus() const noexcept { return _data.status; }
    void setStatus(i32 status) noexcept { _data.status = status; }
    void setVersion(std::string_view version) noexcept { _data.version = version; }

    bool isCommitted() const noexcept { return _committed; }
    bool failed() const noexcept { return _failed; }

    /**
     * @note @p value must stay alive until the response is committed; the
     *       response stores a view, it does not copy.
     */
    void addHeader(HeaderType key, std::string_view value) noexcept
    {
        if (key >= HeaderType::HeaderCount)
            return;

        const HeaderMask bit = headerBit(key);
        if ((_data.present & bit) == 0)
        {
            _data.present |= bit;
            _data.active_headers[_data.header_count++] = key;
        }

        _data.headers[key] = value;
    }

    void setContentType(std::string_view type) noexcept { addHeader(HeaderType::ContentType, type); }

    /** @deprecated kept for source compatibility; begin() supersedes it. */
    void initBody(ByteBuffer* writeBufferPtr) noexcept { _data.body = writeBufferPtr; }

    /** Serializes status line, headers and payload. Safe to call only once. */
    bool setBody(std::string_view body);

    /** Emits an empty-bodied response when a handler returned without one. */
    bool finalize()
    {
        if (_committed)
            return !_failed;
        return setBody({});
    }

    static std::string_view getStatusString(i32 status) noexcept;

private:
    void emitCustomHeaders();

    HttpResponseData _data;
    bool _keepAlive = true;
    bool _headOnly = false;
    bool _committed = false;
    bool _failed = false;
};

#endif // HTTPRESPONSE_H
