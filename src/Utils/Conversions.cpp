#include "Conversions.h"

#include <algorithm>

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
    std::string result;
    result.reserve(input.size());

    static constexpr char hexval[256] = { /* lookup table for 0-9A-Fa-f -> 0-15 */
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,1,2,3,4,5,6,7,8,9,0,0,0,0,0,0,
        10,11,12,13,14,15,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,10,11,12,13,14,15 // A-F
    };

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%')
        {
            if (i + 2 < input.size())
            {
                unsigned char h1 = hexval[static_cast<unsigned char>(input[++i])];
                unsigned char h2 = hexval[static_cast<unsigned char>(input[++i])];
                if (h1 < 16 && h2 < 16)
                {
                    result += static_cast<char>((h1 << 4) | h2);
                    continue;
                }
            }
        }
        else if (input[i] == '+')
        {
            result += ' ';
        }
        else
        {
            result += input[i];
        }
    }
    return result;
}

const bool Conversions::iequals(std::string_view a, std::string_view b) noexcept
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [&](char a, char b){
        return std::tolower(a) == std::tolower(b);
    });
}
