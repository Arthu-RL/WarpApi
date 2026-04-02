#ifndef CONVERSIONS_H
#define CONVERSIONS_H

#pragma once

#include <string>

class Conversions
{
public:
    Conversions();

    static std::string urlEncode(const std::string_view input);
    static std::string urlDecode(const std::string_view input);

    static bool iequals(std::string_view a, std::string_view b) noexcept;
};

#endif // CONVERSIONS_H
