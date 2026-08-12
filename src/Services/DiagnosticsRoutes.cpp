#include "Services/DiagnosticsRoutes.h"

#include "Request/HttpRequest.h"
#include "Response/HttpResponse.h"
#include "Services/AppServices.h"

void configureDiagnosticsRoutes(warp::Router& router)
{
    // Resolved once, here, while the application is being wired. The handlers
    // below capture references, so serving a request performs no container
    // lookup at all. Calling router.services().get<T>() from inside a handler
    // would move that lookup onto the hot path for no benefit.
    auto& build   = router.services().get<BuildInfo>();
    auto& counter = router.services().get<RequestCounter>();

    router.group("/diag", [&build, &counter](warp::Router& diag) {
        diag.get("/info", [&build](const HttpRequest&, HttpResponse& response) {
            auto obj = ink::EnhancedJson();
            obj["name"] = build.name();
            obj["version"] = build.version();
            response.setBody(obj.toCompactString());
        });

        diag.get("/hits", [&counter](const HttpRequest&, HttpResponse& response) {
            counter.increment();

            auto obj = ink::EnhancedJson();
            obj["hits"] = counter.value();
            response.setBody(obj.toCompactString());
        });

        // Path parameters: `:name` segments are captured into the request as
        // views into the read buffer, so reading them allocates nothing.
        diag.get("/echo/:id", [](const HttpRequest& request, HttpResponse& response) {
            auto obj = ink::EnhancedJson();
            obj["id"] = std::string(request.param("id"));
            response.setBody(obj.toCompactString());
        });

        diag.get("/echo/:id/part/:sub", [](const HttpRequest& request, HttpResponse& response) {
            auto obj = ink::EnhancedJson();
            obj["id"] = std::string(request.param("id"));
            obj["sub"] = std::string(request.param("sub"));
            response.setBody(obj.toCompactString());
        });
    });
}
