#include "StringUtils.h"

#include <charconv>
#include <ink/utils.h>
#include <emmintrin.h>

StringUtils::StringUtils() {}

u32 StringUtils::hashStr(const char* str, size_t len) noexcept
{
    u32 hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        char c = str[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        hash ^= static_cast<u8>(c);
        hash *= 16777619u;
    }
    return hash;
}

const char* StringUtils::find_crlf(const char* data, const char* end) noexcept
{
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

    for (size_t i = 0; i < len; ++i) {
        if ((pa[i] | 32) != (pb[i] | 32)) return false;
    }

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

    if (ec == std::errc()) [[unlikely]]
    {
        return {};
    }

    return std::string_view(buf, ptr - buf);
}

