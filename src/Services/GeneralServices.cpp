#include "GeneralServices.h"

#include "Request/HttpRequest.h"
#include "Response/HttpResponse.h"

GeneralServices::GeneralServices() {
    registerAllEndpoints();
}

void GeneralServices::registerAllEndpoints()
{
    registerEndpoint("/",Method::GET,
                     [&](const HttpRequest& request, HttpResponse& response)
    {
        // response.setHeader(boost::beast::http::field::connection, "keep-alive");
        ink::EnhancedJson meta_obj = ink::EnhancedJson::meta();
        response.setBody(meta_obj.toPrettyString());
    });

    registerEndpoint("/test", Method::GET,
                     [&](const HttpRequest& request, HttpResponse& response)
    {
        // response.setHeader(boost::beast::http::field::connection, "keep-alive");
        const auto& params = request.queryParams();
        const auto& body = request.body();
        auto jObj = ink::EnhancedJsonUtils::loadFromString(body);

        auto result = ink::EnhancedJson();

        for (auto it=params.begin(); it != params.end(); ++it)
        {
            result[it->first] = it->second;
        }

        for (auto it=jObj.begin(); it != jObj.end(); ++it)
        {
            result[it.key()] = it.value().get<std::string>();
        }

        response.setBody(result.toPrettyString());

        // FOR ENDPOINT VALIDATION

        // auto it = jObj.find("test_body");
        // if (it != jObj.end())
        // {
        //     obj[it.key()] = it.value().get<std::string>();
        // }

        // response.setStatus(http::status::bad_request);
        // response.setBody("Expected `test_id` param.");
    });

    registerEndpoint("/health", Method::GET,
                     [&](const HttpRequest& request, HttpResponse& response)
    {
        // response.setHeader(http::field::content_type, "plain/text");
        auto obj = ink::EnhancedJson();
        obj["status"] = "ok";
        response.setBody(obj.toPrettyString());
    });

    registerEndpoint("/version", Method::GET,
                     [&](const HttpRequest& request, HttpResponse& response)
    {
        response.addHeader("Connection", "keep-alive");

        auto obj = ink::EnhancedJson();
        obj["major"] = 1;
        obj["patch"] = 0;
        obj["minor"] = 0;
        obj["text"] = "1.0.0";

        response.setBody(obj.toPrettyString());
    });
}
