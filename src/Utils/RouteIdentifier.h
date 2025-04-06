#ifndef ROUTEIDENTIFIER_H
#define ROUTEIDENTIFIER_H

#pragma once

#include "WarpDefs.h"

class WARP_API RouteIdentifier
{
public:
    RouteIdentifier();

    static std::string generateIdentifier(const std::string& route, const Method method);
};


#endif // ROUTEIDENTIFIER_H
