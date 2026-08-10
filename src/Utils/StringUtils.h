#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#pragma once

#include <ink/ink_base.hpp>
#include <string>
#include <string_view>

#include "Utils/HeadersList.h"

class StringUtils
{
public:
    StringUtils() = delete;

    static u32 hashStr(const char* str, size_t len) noexcept;

    static const char* find_crlf(const char* data, const char* end) noexcept;

    static bool is_crlf(const char* p, const char* end) noexcept;

    static bool is_header_end(const char* p, const char* end) noexcept;

    /** Case-insensitive ASCII compare; folds only A-Z so punctuation stays exact. */
    static bool iequals_small(std::string_view a, std::string_view b) noexcept;

    /**
     * @brief Case-insensitive search for @p token inside a comma separated
     *        header list value (e.g. "keep-alive, Upgrade").
     *
     * Needed because RFC 7230 lets clients send several connection options in
     * one field; a plain equality test misses the very common
     * `Connection: keep-alive, Upgrade` sent by browsers.
     */
    static bool containsToken(std::string_view list, std::string_view token) noexcept;

    /** Maps a header field name to its ordinal, or HeaderUnknown. */
    static HeaderType matchHeaderName(const char* name, size_t len) noexcept;

    /** Parses a decimal integer; returns false on overflow or a non-digit. */
    static bool parseDecimal(const char* str, size_t len, u64& out) noexcept;

    static size_t fast_atoi(const char* str, size_t len) noexcept;
    static std::string_view fast_itoa(char* buf, size_t len, size_t value) noexcept;

    static std::string base64Encode(const u8* data, usize len);

    /** IMF-fixdate ("Sun, 06 Nov 1994 08:49:37 GMT") for the Date header. */
    static void formatHttpDate(char (&buf)[30], time_t t) noexcept;

    static inline char lowerAscii(char c) noexcept
    {
        // Branchless: only shift when the byte is in 'A'..'Z'.
        return static_cast<char>(c + ((static_cast<unsigned char>(c - 'A') < 26u) << 5));
    }
};

#endif // STRINGUTILS_H
