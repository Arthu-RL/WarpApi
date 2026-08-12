#include "Services/GeneralServices.h"

#include "Request/HttpRequest.h"
#include "Response/HttpResponse.h"
#include "Server/WebSocketContext.h"

void configureGeneralRoutes(warp::Router& router)
{
    router.get("/", [](const HttpRequest&, HttpResponse& response) {
        response.setBody(ink::EnhancedJsonUtils::meta_info().toPrettyString());
    });

    // Cheapest possible route: a constant body, no allocation, no serialization.
    // Use this one to measure the framework rather than the JSON library.
    router.get("/plaintext", [](const HttpRequest&, HttpResponse& response) {
        response.setContentType(TEXT_CONTENT_TYPE);
        response.setBody("Hello, World!");
    });

    router.get("/json", [](const HttpRequest&, HttpResponse& response) {
        response.setBody(R"({"message":"Hello, World!"})");
    });

    router.get("/test", [](const HttpRequest& request, HttpResponse& response) {
        auto result = ink::EnhancedJson();

        for (const auto& [key, value] : request.queryParams())
            result[key] = value;

        const auto body = request.body();
        if (!body.empty())
        {
            // The body is a view into the connection buffer and is *not* NUL
            // terminated, so it has to be sized explicitly.
            auto jObj = ink::EnhancedJsonUtils::loadFromString(std::string(body));
            for (auto it = jObj.begin(); it != jObj.end(); ++it)
                result[it.key()] = it.value();
        }

        response.setBody(result.toPrettyString());
    });

    router.post("/apibenchmark", [](const HttpRequest& request, HttpResponse& response) {
        response.setBody(request.body());
    });

    router.get("/health", [](const HttpRequest&, HttpResponse& response) {
        auto obj = ink::EnhancedJson();
        obj["status"] = "ok";

        response.setBody(obj.toCompactString());
    });

    router.get("/version", [](const HttpRequest&, HttpResponse& response) {
        auto obj = ink::EnhancedJson();
        obj["major"] = 0;
        obj["minor"] = 1;
        obj["patch"] = 0;
        obj["text"] = "0.1.0";

        response.setBody(obj.toPrettyString());
    });

    router.webSocket("/ws/echo",
        /* onOpen    */ [](WebSocketContext& ctx) {
            ctx.sendText("connected");
        },
        /* onMessage */ [](WebSocketContext& ctx, std::string_view payload, bool isBinary) {
            if (isBinary)
                ctx.sendBinary(payload);
            else
                ctx.sendText(payload);
        }
        // No onClose: nothing to release for an echo route, and it defaults
        // to empty (see Router::webSocket).
    );
}
