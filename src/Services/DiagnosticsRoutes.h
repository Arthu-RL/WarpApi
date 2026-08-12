#ifndef DIAGNOSTICSROUTES_H
#define DIAGNOSTICSROUTES_H

#pragma once

#include "Core/Router.h"

/**
 * @brief Example: routes whose behavior depends on injected services.
 *
 * These need BuildInfo and RequestCounter, so registration is an ordinary
 * function call: resolve what is needed from @p router 's ServiceContainer,
 * then declare routes that capture it. No interface to implement, nothing to
 * instantiate — a function is the whole unit of composition.
 */
void configureDiagnosticsRoutes(warp::Router& router);

#endif // DIAGNOSTICSROUTES_H
