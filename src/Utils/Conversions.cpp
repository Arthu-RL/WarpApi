#include "Conversions.h"

#include <sstream>

Conversions::Conversions() {}

const std::string Conversions::urlEncode(const std::string& input)
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

const std::string Conversions::urlDecode(const std::string& input)
{
    std::string decoded;

    for (size_t i = 0; i < input.length(); ++i)
    {
        if (input[i] == '%')
        {
            std::string hexValue = input.substr(i + 1, 2);
            const char decodedChar = static_cast<char>(std::stoi(hexValue, nullptr, 16));
            decoded += decodedChar;
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
