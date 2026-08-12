#ifndef GENERALSERVICES_H
#define GENERALSERVICES_H

#pragma once

#include "Core/Router.h"

/**
 * @brief The framework's bundled example/default routes: meta info, a
 *        plaintext benchmark target, health/version, and a WebSocket echo.
 *
 * Registered like any other route group — call it once while wiring the
 * application, before `EndpointManager::freeze()`. See main.cpp.
 */
void configureGeneralRoutes(warp::Router& router);

#endif // GENERALSERVICES_H
