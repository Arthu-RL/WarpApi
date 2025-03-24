#include "RouteIdentifier.h"

RouteIdentifier::RouteIdentifier()
{
    // Empty
}

const std::string httpMethodToString(Method method)
{
    switch (method)
    {
    case Method::GET:
        return "GET";
    case Method::POST:
        return "POST";
    case Method::PUT:
        return "PUT";
    case Method::PATCH:
        return "PATCH";
    case Method::DELETE:
        return "DELETE";
    case Method::OPTIONS:
        return "OPTIONS";
    case Method::HEAD:
        return "HEAD";
    default:
        return "UNKNOWN";
    }
}

std::string RouteIdentifier::generateIdentifier(const std::string& route, const Method method)
{
    return route + ":" + httpMethodToString(method);
}
