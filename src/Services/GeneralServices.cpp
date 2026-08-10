#include "GeneralServices.h"

#include "Request/HttpRequest.h"
#include "Response/HttpResponse.h"
#include "Server/WebSocketContext.h"

GeneralServices::GeneralServices() {
    registerAllEndpoints();
}

void GeneralServices::registerAllEndpoints()
{
    registerEndpoint("/", Method::GET,
                     [](const HttpRequest&, HttpResponse& response)
    {
        response.setBody(ink::EnhancedJsonUtils::meta_info().toPrettyString());
    });

    // Cheapest possible route: a constant body, no allocation, no serialisation.
    // Use this one to measure the framework rather than the JSON library.
    registerEndpoint("/plaintext", Method::GET,
                     [](const HttpRequest&, HttpResponse& response)
    {
        response.setContentType(TEXT_CONTENT_TYPE);
        response.setBody("Hello, World!");
    });

    registerEndpoint("/json", Method::GET,
                     [](const HttpRequest&, HttpResponse& response)
    {
        response.setBody(R"({"message":"Hello, World!"})");
    });

    registerEndpoint("/test", Method::GET,
                     [](const HttpRequest& request, HttpResponse& response)
    {
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

    registerEndpoint("/apibenchmark", Method::POST,
                     [](const HttpRequest& request, HttpResponse& response)
    {
        response.setBody(request.body());
    });

    registerEndpoint("/health", Method::GET,
                     [](const HttpRequest&, HttpResponse& response)
    {
        auto obj = ink::EnhancedJson();
        obj["status"] = "ok";

        response.setBody(obj.toCompactString());
    });

    registerEndpoint("/version", Method::GET,
                     [](const HttpRequest&, HttpResponse& response)
    {
        auto obj = ink::EnhancedJson();
        obj["major"] = 1;
        obj["minor"] = 0;
        obj["patch"] = 0;
        obj["text"] = "1.0.0";

        response.setBody(obj.toPrettyString());
    });

    registerWebSocketEndpoint("/ws/echo", {
        [](WebSocketContext& ctx) {
            ctx.sendText("connected");
        },
        [](WebSocketContext& ctx, std::string_view payload, bool isBinary) {
            if (isBinary)
                ctx.sendBinary(payload);
            else
                ctx.sendText(payload);
        },
        [](WebSocketContext&) {
            // Nothing to release for an echo route.
        }
    });
}
