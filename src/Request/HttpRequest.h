#ifndef HTTPREQUEST_H
#define HTTPREQUEST_H

#pragma once

#include <bit>
#include <cstring>
#include <unordered_map>

#include "WarpDefs.h"
#include "Utils/Conversions.h"
#include "Utils/HeadersList.h"

struct WARP_API RequestData {
    RequestData() = default;

    Method method = Method::UNKNOWN;
    std::string_view path;
    std::string_view query;
    std::string_view version;
    std::string_view body;

    /**
     * Indexed by the HeaderType *ordinal* — never by a bitmask.
     *
     * @note Deliberately left uninitialised and never bulk-cleared. A slot is
     *       only ever read after its presence bit has been checked, so stale
     *       contents from a previous request on the same connection are
     *       unobservable. Clearing it cost a 288-byte memset on every single
     *       request; resetting one 32-bit mask is exactly equivalent.
     */
    std::array<std::string_view, MAX_HEADERS_SIZE> headers;

    void clear() noexcept
    {
        method = Method::UNKNOWN;
        path = {};
        query = {};
        version = {};
        body = {};
    }
};

class WARP_API HttpRequest {
public:
    explicit HttpRequest() = default;

    /**
     * @brief Method lookup via SWAR: one unaligned load and one integer
     *        compare per candidate instead of a byte-at-a-time loop.
     *
     * Methods are at most 7 bytes, so each fits in a u64. The caller guarantees
     * @p m points into the read buffer with the request line's trailing CRLF
     * still ahead of it, so a 4- or 8-byte read never runs off the end — but we
     * assemble the word from exactly m.size() bytes anyway so the comparison
     * cannot be polluted by whatever follows.
     */
    static Method parseMethod(std::string_view m) noexcept
    {
        // Little-endian packing of a short ASCII literal into an integer.
        constexpr auto pack4 = [](const char (&s)[5]) constexpr -> u32 {
            return static_cast<u32>(static_cast<u8>(s[0]))
                 | (static_cast<u32>(static_cast<u8>(s[1])) << 8)
                 | (static_cast<u32>(static_cast<u8>(s[2])) << 16)
                 | (static_cast<u32>(static_cast<u8>(s[3])) << 24);
        };

        switch (m.size())
        {
            case 3:
            {
                u32 w = 0;
                std::memcpy(&w, m.data(), 3); // top byte stays zero
                if (w == (pack4("GET\0") & 0x00FFFFFFu)) return GET;
                if (w == (pack4("PUT\0") & 0x00FFFFFFu)) return PUT;
                break;
            }
            case 4:
            {
                u32 w;
                std::memcpy(&w, m.data(), 4);
                if (w == pack4("POST")) return POST;
                if (w == pack4("HEAD")) return HEAD;
                break;
            }
            case 5:
            {
                u64 w = 0;
                std::memcpy(&w, m.data(), 5);
                if (w == 0x0000004843544150ULL) return PATCH; // "PATCH"
                break;
            }
            case 6:
            {
                u64 w = 0;
                std::memcpy(&w, m.data(), 6);
                if (w == 0x00004554454C4544ULL) return DELETE; // "DELETE"
                break;
            }
            case 7:
            {
                u64 w = 0;
                std::memcpy(&w, m.data(), 7);
                if (w == 0x00534E4F4954504FULL) return OPTIONS; // "OPTIONS"
                break;
            }
            default:
                break;
        }

        return UNKNOWN;
    }

    Method method() const noexcept { return _data.method; }
    void setMethod(Method method) noexcept { _data.method = method; }

    const std::string_view& path() const noexcept { return _data.path; }
    const std::string_view& query() const noexcept { return _data.query; }

    void setPath(const std::string_view& path, const std::string_view& query) noexcept
    {
        _data.path = path;
        _data.query = query;
    }

    std::string_view body() const noexcept { return _data.body; }
    void setBody(const std::string_view& buffer) noexcept { _data.body = buffer; }

    void addHeader(HeaderType key, const char* v, size_t vLen) noexcept
    {
        if (key < HeaderType::HeaderCount) [[likely]]
        {
            _data.headers[key] = std::string_view(v, vLen);
            _presentHeaders |= headerBit(key);
        }
    }

    /**
     * @note The presence bit is the authority here, not the slot contents —
     *       see RequestData::headers. Reading a slot whose bit is clear would
     *       hand back the previous request's value on a keep-alive connection.
     */
    std::string_view getHeader(HeaderType key) const noexcept
    {
        if ((_presentHeaders & headerBit(key)) == 0)
            return {};

        return _data.headers[key];
    }

    /** Iterates only the headers actually present, cheapest-first. */
    template <typename Fn>
    void forEachHeader(Fn&& fn) const
    {
        HeaderMask remaining = _presentHeaders;
        while (remaining)
        {
            const u32 idx = static_cast<u32>(std::countr_zero(remaining));
            remaining &= remaining - 1; // clear lowest set bit
            fn(static_cast<HeaderType>(idx), _data.headers[idx]);
        }
    }

    bool hasHeaderField(HeaderType key) const noexcept
    {
        return (_presentHeaders & headerBit(key)) != 0;
    }

    /**
     * Query parameters are decoded lazily: the map costs several allocations
     * per request, and the vast majority of routes never look at it. The first
     * call parses, subsequent calls reuse the result.
     */
    const std::unordered_map<std::string, std::string>& queryParams() const
    {
        if (!_queryParsed)
        {
            _queryParsed = true;
            if (!_data.query.empty())
                parseQueryParams();
        }
        return _queryParams;
    }

    std::string_view version() const noexcept { return _data.version; }
    void setVersion(const std::string_view& version) noexcept { _data.version = version; }

    const RequestData& getRequestData() const noexcept { return _data; }

    void reset() noexcept
    {
        _data.clear();
        _presentHeaders = 0;
        if (!_queryParams.empty())
            _queryParams.clear();
        _queryParsed = false;
    }

    HeaderMask presentHeaders() const noexcept { return _presentHeaders; }

private:
    void parseQueryParams() const
    {
        std::string_view target = _data.query;
        while (!target.empty())
        {
            auto amp = target.find('&');
            std::string_view part = (amp == std::string_view::npos) ? target : target.substr(0, amp);
            target = (amp == std::string_view::npos) ? std::string_view{} : target.substr(amp + 1);

            if (part.empty())
                continue;

            auto eq = part.find('=');
            std::string_view k = (eq == std::string_view::npos) ? part : part.substr(0, eq);
            std::string_view v = (eq == std::string_view::npos) ? std::string_view{} : part.substr(eq + 1);

            _queryParams.emplace(Conversions::urlDecode(k), Conversions::urlDecode(v));
        }
    }

    RequestData _data;
    HeaderMask _presentHeaders = 0;

    mutable std::unordered_map<std::string, std::string> _queryParams;
    mutable bool _queryParsed = false;
};

#endif // HTTPREQUEST_H
