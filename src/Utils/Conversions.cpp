#include "Conversions.h"

#include <algorithm>
#include <sstream>

Conversions::Conversions() {}

const std::string Conversions::urlEncode(const std::string_view input)
{
    std::ostringstream encoded;

    for (const char& c : input)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            encoded << c;
        }
        else
        {
            encoded << '%' << std::uppercase << std::hex << static_cast<int>(static_cast<unsigned char>(c));
        }
    }

    return encoded.str();
}

const std::string Conversions::urlDecode(const std::string_view input)
{
    std::string decoded;

    for (size_t i = 0; i < input.length(); ++i)
    {
        if (input[i] == '%')
        {
            std::string_view hexValue = input.substr(i + 1, 2);
            decoded += static_cast<char>(std::stoi(hexValue.data(), nullptr, 16));
            i += 2;
        }
        else if (input[i] == '+')
        {
            decoded += ' ';
        }
        else
        {
            decoded += input[i];
        }
    }

    return decoded;
}


const bool Conversions::iequals(std::string_view a, std::string_view b) noexcept
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [&](char a, char b){
        return std::tolower(a) == std::tolower(b);
    });
}
