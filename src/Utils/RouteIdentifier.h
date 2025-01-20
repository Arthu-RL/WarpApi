#ifndef ROUTEIDENTIFIER_H
#define ROUTEIDENTIFIER_H

#pragma once

#include <boost/beast.hpp>
#include <string>

namespace beast = boost::beast;
namespace http = beast::http;

class RouteIdentifier
{
public:
    RouteIdentifier();

    static std::string generateIdentifier(const std::string& route, const http::verb method);
};


#endif // ROUTEIDENTIFIER_H
