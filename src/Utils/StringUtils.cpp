#include "StringUtils.h"

#include <bit>
#include <charconv>
#include <cstring>
#include <ctime>
#include <limits>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

u32 StringUtils::hashStr(const char* str, size_t len) noexcept
{
    u32 hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<u8>(lowerAscii(str[i]));
        hash *= 16777619u;
    }
    return hash;
}

const char* StringUtils::find_crlf(const char* data, const char* end) noexcept
{
    const char* p = data;

#if defined(__AVX2__)
    // 32 bytes per compare. Header lines are typically 20-40 bytes, so this
    // usually resolves the whole line in a single iteration.
    {
        const __m256i cr = _mm256_set1_epi8('\r');
        while (end - p >= 32)
        {
            const __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
            const u32 mask = static_cast<u32>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, cr)));
            if (mask)
                return p + std::countr_zero(mask);
            p += 32;
        }
    }
#endif

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86_FP)
    {
        const __m128i cr = _mm_set1_epi8('\r');
        while (end - p >= 16)
        {
            const __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
            const u32 mask = static_cast<u32>(_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, cr)));
            if (mask)
                return p + std::countr_zero(mask);
            p += 16;
        }
    }
#endif

    // SWAR tail: locate a zero byte with the classic Mycroft/Lamport identity
    // (v - 0x01..01) & ~v & 0x80..80, applied to v XOR broadcast('\r'). Cheaper
    // than 8 separate compares for the 1-15 byte remainder.
    {
        constexpr u64 kOnes = 0x0101010101010101ULL;
        constexpr u64 kHigh = 0x8080808080808080ULL;
        constexpr u64 kCr   = 0x0D0D0D0D0D0D0D0DULL;

        while (end - p >= 8)
        {
            u64 v;
            std::memcpy(&v, p, 8);
            v ^= kCr; // bytes equal to '\r' become 0x00
            const u64 hit = (v - kOnes) & ~v & kHigh;
            if (hit)
                return p + (static_cast<usize>(std::countr_zero(hit)) >> 3);
            p += 8;
        }
    }

    while (p < end)
    {
        if (*p == '\r')
            return p;
        ++p;
    }

    return nullptr;
}

bool StringUtils::is_crlf(const char* p, const char* end) noexcept
{
    return (p + 1 < end) && (p[0] == '\r') && (p[1] == '\n');
}

bool StringUtils::is_header_end(const char* p, const char* end) noexcept
{
    return (p + 3 < end) &&
           (p[0] == '\r') && (p[1] == '\n') &&
           (p[2] == '\r') && (p[3] == '\n');
}

bool StringUtils::iequals_small(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size()) return false;

    const char* pa = a.data();
    const char* pb = b.data();
    size_t len = a.size();

    // SWAR case-insensitive compare, 8 bytes per iteration.
    //
    // OR-ing with 0x20 is an exact lowercase fold over the character set HTTP
    // actually uses in field names and in the tokens we match against
    // (letters, digits, '-'): digits and '-' already have bit 5 set, so they
    // are unchanged. It is *not* a general ASCII fold — bytes like '_' (0x5F)
    // fold to 0x7F — but every literal we compare against is drawn from that
    // restricted set and contains no 0x7F, so no foreign byte can alias onto
    // one. This is why the function is named for small tokens, not free text.
    constexpr u64 kFold = 0x2020202020202020ULL;

    while (len >= 8)
    {
        u64 wa, wb;
        std::memcpy(&wa, pa, 8);
        std::memcpy(&wb, pb, 8);
        if (((wa | kFold) ^ (wb | kFold)) != 0)
            return false;
        pa += 8; pb += 8; len -= 8;
    }

    if (len >= 4)
    {
        u32 wa, wb;
        std::memcpy(&wa, pa, 4);
        std::memcpy(&wb, pb, 4);
        if (((wa | 0x20202020u) ^ (wb | 0x20202020u)) != 0)
            return false;
        pa += 4; pb += 4; len -= 4;
    }

    for (size_t i = 0; i < len; ++i)
    {
        if ((static_cast<u8>(pa[i]) | 0x20u) != (static_cast<u8>(pb[i]) | 0x20u))
            return false;
    }

    return true;
}

bool StringUtils::containsToken(std::string_view list, std::string_view token) noexcept
{
    if (token.empty()) return false;

    size_t i = 0;
    while (i < list.size())
    {
        // Skip separators and optional whitespace
        while (i < list.size() && (list[i] == ',' || list[i] == ' ' || list[i] == '\t'))
            ++i;

        size_t start = i;
        while (i < list.size() && list[i] != ',')
            ++i;

        size_t stop = i;
        while (stop > start && (list[stop - 1] == ' ' || list[stop - 1] == '\t'))
            --stop;

        if (stop > start && iequals_small(list.substr(start, stop - start), token))
            return true;
    }

    return false;
}

HeaderType StringUtils::matchHeaderName(const char* name, size_t len) noexcept
{
    // Dispatch on length first, then do an exact case-insensitive compare.
    // Matching on length alone (as an earlier revision did) collides badly:
    // "Referer" and "Upgrade" are both 7 bytes, "Keep-Alive"/"Set-Cookie" and
    // "Connection" are all 10, so ordinary requests were being misread as
    // WebSocket upgrades.
    const std::string_view n(name, len);

    switch (len)
    {
        case 4:
            if (iequals_small(n, HeaderStrings[HeaderType::Date])) return HeaderType::Date;
            if (iequals_small(n, HeaderStrings[HeaderType::Host])) return HeaderType::Host;
            break;
        case 6:
            if (iequals_small(n, HeaderStrings[HeaderType::Accept])) return HeaderType::Accept;
            if (iequals_small(n, HeaderStrings[HeaderType::Server])) return HeaderType::Server;
            if (iequals_small(n, HeaderStrings[HeaderType::Expect])) return HeaderType::Expect;
            break;
        case 7:
            if (iequals_small(n, HeaderStrings[HeaderType::Upgrade])) return HeaderType::Upgrade;
            break;
        case 10:
            if (iequals_small(n, HeaderStrings[HeaderType::Connection])) return HeaderType::Connection;
            if (iequals_small(n, HeaderStrings[HeaderType::UserAgent])) return HeaderType::UserAgent;
            break;
        case 12:
            if (iequals_small(n, HeaderStrings[HeaderType::ContentType])) return HeaderType::ContentType;
            break;
        case 13:
            if (iequals_small(n, HeaderStrings[HeaderType::Authorization])) return HeaderType::Authorization;
            if (iequals_small(n, HeaderStrings[HeaderType::CacheControl])) return HeaderType::CacheControl;
            break;
        case 14:
            if (iequals_small(n, HeaderStrings[HeaderType::ContentLength])) return HeaderType::ContentLength;
            break;
        case 15:
            if (iequals_small(n, HeaderStrings[HeaderType::AcceptEncoding])) return HeaderType::AcceptEncoding;
            break;
        case 17:
            if (iequals_small(n, HeaderStrings[HeaderType::SecWebSocketKey])) return HeaderType::SecWebSocketKey;
            if (iequals_small(n, HeaderStrings[HeaderType::TransferEncoding])) return HeaderType::TransferEncoding;
            break;
        case 20:
            if (iequals_small(n, HeaderStrings[HeaderType::SecWebSocketAccept])) return HeaderType::SecWebSocketAccept;
            break;
        case 21:
            if (iequals_small(n, HeaderStrings[HeaderType::SecWebSocketVersion])) return HeaderType::SecWebSocketVersion;
            break;
        case 22:
            if (iequals_small(n, HeaderStrings[HeaderType::SecWebSocketProtocol])) return HeaderType::SecWebSocketProtocol;
            break;
        default:
            break;
    }

    return HeaderType::HeaderUnknown;
}

bool StringUtils::parseDecimal(const char* str, size_t len, u64& out) noexcept
{
    if (len == 0) return false;

    constexpr u64 kLimit = std::numeric_limits<u64>::max() / 10;

    u64 result = 0;
    for (size_t i = 0; i < len; ++i)
    {
        const char c = str[i];
        if (c < '0' || c > '9')
            return false;
        if (result > kLimit)
            return false;
        result = result * 10 + static_cast<u64>(c - '0');
    }

    out = result;
    return true;
}

size_t StringUtils::fast_atoi(const char* str, size_t len) noexcept
{
    size_t result = 0;
    for (size_t i = 0; i < len; ++i)
    {
        char c = str[i];
        if (c < '0' || c > '9') break;
        result = result * 10 + (c - '0');
    }
    return result;
}

std::string_view StringUtils::fast_itoa(char* buf, size_t len, size_t value) noexcept
{
    auto [ptr, ec] = std::to_chars(buf, buf + len, value);

    if (ec != std::errc())
    {
        return {};
    }

    return std::string_view(buf, ptr - buf);
}

std::string StringUtils::base64Encode(const u8* data, usize len)
{
    static constexpr char kBase64Table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    if (len == 0)
        return {};

    usize outLen = ((len + 2) / 3) * 4;
    std::string out;
    out.resize(outLen);

    char* outPtr = out.data();
    usize i = 0;
    usize mainLen = len - (len % 3);

    for (; i < mainLen; i += 3)
    {
        u32 chunk = (static_cast<u32>(data[i]) << 16) |
                    (static_cast<u32>(data[i + 1]) << 8) |
                    static_cast<u32>(data[i + 2]);

        *outPtr++ = kBase64Table[(chunk >> 18) & 0x3F];
        *outPtr++ = kBase64Table[(chunk >> 12) & 0x3F];
        *outPtr++ = kBase64Table[(chunk >> 6) & 0x3F];
        *outPtr++ = kBase64Table[chunk & 0x3F];
    }

    if (i < len)
    {
        u32 chunk = static_cast<u32>(data[i]) << 16;
        bool hasSecond = (i + 1 < len);

        if (hasSecond) {
            chunk |= static_cast<u32>(data[i + 1]) << 8;
        }

        *outPtr++ = kBase64Table[(chunk >> 18) & 0x3F];
        *outPtr++ = kBase64Table[(chunk >> 12) & 0x3F];
        *outPtr++ = hasSecond ? kBase64Table[(chunk >> 6) & 0x3F] : '=';
        *outPtr++ = '=';
    }

    return out;
}

void StringUtils::formatHttpDate(char (&buf)[30], time_t t) noexcept
{
    static constexpr char kDays[7][4]   = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static constexpr char kMonths[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    struct tm tmv;
    gmtime_r(&t, &tmv);

    auto two = [](char* d, int v) {
        d[0] = static_cast<char>('0' + (v / 10) % 10);
        d[1] = static_cast<char>('0' + v % 10);
    };

    // "Www, DD Mmm YYYY HH:MM:SS GMT" == 29 chars + NUL
    std::memcpy(buf, kDays[tmv.tm_wday % 7], 3);
    buf[3] = ',';
    buf[4] = ' ';
    two(buf + 5, tmv.tm_mday);
    buf[7] = ' ';
    std::memcpy(buf + 8, kMonths[tmv.tm_mon % 12], 3);
    buf[11] = ' ';

    const int year = tmv.tm_year + 1900;
    buf[12] = static_cast<char>('0' + (year / 1000) % 10);
    buf[13] = static_cast<char>('0' + (year / 100) % 10);
    buf[14] = static_cast<char>('0' + (year / 10) % 10);
    buf[15] = static_cast<char>('0' + year % 10);
    buf[16] = ' ';
    two(buf + 17, tmv.tm_hour);
    buf[19] = ':';
    two(buf + 20, tmv.tm_min);
    buf[22] = ':';
    two(buf + 23, tmv.tm_sec);
    std::memcpy(buf + 25, " GMT", 4);
    buf[29] = '\0';
}
