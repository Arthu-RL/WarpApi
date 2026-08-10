#include "Conversions.h"

#include <algorithm>
#include <array>
#include <cctype>

Conversions::Conversions() {}

std::string Conversions::urlEncode(std::string_view input)
{
    std::string result;
    result.reserve(input.size() * 3 / 2);  // ~50% chars need encoding

    static constexpr char hex[] = "0123456789ABCDEF";

    for (char c : input)
    {
        if (std::isalnum(static_cast<unsigned char>(c)) ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            result += c;
        }
        else
        {
            result += '%';
            result += hex[static_cast<unsigned char>(c) >> 4];
            result += hex[static_cast<unsigned char>(c) & 0xF];
        }
    }

    return result;
}

std::string Conversions::urlDecode(std::string_view input)
{
    // Generated rather than written out by hand: a hand-laid table had its
    // digit row one block too far in, so every %XX escape decoded to NUL.
    // 0xFF marks "not a hex digit", which a table of plain zeroes could not
    // distinguish from a legitimate 0.
    static constexpr auto hexval = [] {
        std::array<unsigned char, 256> t{};
        t.fill(0xFF);
        for (int c = '0'; c <= '9'; ++c) t[c] = static_cast<unsigned char>(c - '0');
        for (int c = 'A'; c <= 'F'; ++c) t[c] = static_cast<unsigned char>(c - 'A' + 10);
        for (int c = 'a'; c <= 'f'; ++c) t[c] = static_cast<unsigned char>(c - 'a' + 10);
        return t;
    }();

    std::string result;
    result.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i)
    {
        const char c = input[i];

        if (c == '%' && i + 2 < input.size())
        {
            const unsigned char h1 = hexval[static_cast<unsigned char>(input[i + 1])];
            const unsigned char h2 = hexval[static_cast<unsigned char>(input[i + 2])];

            if (h1 != 0xFF && h2 != 0xFF)
            {
                result += static_cast<char>((h1 << 4) | h2);
                i += 2;
                continue;
            }
            // Not a valid escape: keep the '%' verbatim instead of swallowing it.
        }

        result += (c == '+') ? ' ' : c;
    }

    return result;
}

bool Conversions::iequals(std::string_view a, std::string_view b) noexcept
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [&](char a, char b){
        return std::tolower(a) == std::tolower(b);
    });
}
