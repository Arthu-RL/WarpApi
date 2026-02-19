#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <ink/ink_base.hpp>
#include <string>

class StringUtils
{
public:
    StringUtils();

    static constexpr u32 fnv1a_hash(const char* str, size_t len) noexcept
    {
        uint32_t hash = 2166136261u;
        for (size_t i = 0; i < len; ++i) {
            char c = str[i];
            if (c >= 'A' && c <= 'Z') c += 32;
            hash ^= static_cast<uint8_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }

    static u32 hashHeaderName(const char* str, size_t len) noexcept;

    static const char* find_crlf(const char* data, const char* end) noexcept;

    static bool is_crlf(const char* p, const char* end) noexcept;

    static bool is_header_end(const char* p, const char* end) noexcept;

    static bool iequals_small(std::string_view a, std::string_view b) noexcept;

    static size_t fast_atoi(const char* str, size_t len) noexcept;
    static std::string_view fast_itoa(char* buf, size_t len, size_t value) noexcept;
};

#endif // STRINGUTILS_H
