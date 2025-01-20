#include "RouteIdentifier.h"

RouteIdentifier::RouteIdentifier()
{
    // Empty
}

const std::string httpMethodToString(http::verb method)
{
    switch (method)
    {
    case http::verb::get:
        return "GET";
    case http::verb::post:
        return "POST";
    case http::verb::put:
        return "PUT";
    case http::verb::delete_:
        return "DELETE";
    case http::verb::patch:
        return "PATCH";
    case http::verb::options:
        return "OPTIONS";
    case http::verb::head:
        return "HEAD";
    default:
        return "UNKNOWN";
    }
}

std::string RouteIdentifier::generateIdentifier(const std::string& route, const http::verb method)
{
    return route + ":" + httpMethodToString(method);
}
