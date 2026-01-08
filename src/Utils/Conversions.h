#ifndef CONVERSIONS_H
#define CONVERSIONS_H

#pragma once

#include <string>

class Conversions
{
public:
    Conversions();

    static const std::string urlEncode(const std::string_view input);
    static const std::string urlDecode(const std::string_view input);
};

#endif // CONVERSIONS_H
