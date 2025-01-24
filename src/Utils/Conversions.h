#ifndef CONVERSIONS_H
#define CONVERSIONS_H

#pragma once

#include <string>

class Conversions
{
public:
    Conversions();

    static const std::string urlEncode(const std::string& input);
    static const std::string urlDecode(const std::string& input);

    static const int cto_int(char c);
};

#endif // CONVERSIONS_H
