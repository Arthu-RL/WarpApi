#include "GeneralServices.h"

#include <plog/Log.h>
#include <nlohmann/json.hpp>

#include "Utils/JsonLoader.h"

GeneralServices::GeneralServices() {
    registerAllEndpoints();
}

void GeneralServices::registerAllEndpoints()
{
    registerEndpoint("/", http::verb::get, [&](RequestManager<http::string_body>& request,
                                               ResponseManager<http::string_body>& response)
    {
        // response.setHeader(boost::beast::http::field::connection, "keep-alive");
        nlohmann::json meta_obj = JsonLoader::meta_info();
        response.setBody(JsonLoader::jsonToIndentedString(meta_obj));
    });

    registerEndpoint("/hello", http::verb::get, [&](RequestManager<http::string_body>& request,
                                                    ResponseManager<http::string_body>& response)
    {
        response.setHeader(http::field::content_type, "plain/text");
        response.setBody("Hello, World!");
    });

    registerEndpoint("/health", http::verb::get, [&](RequestManager<http::string_body>& request,
                                                    ResponseManager<http::string_body>& response)
    {
        auto obj = JsonLoader::object();
        obj["status"] = "ok";
        response.setBody(JsonLoader::jsonToIndentedString(obj));
    });

    registerEndpoint("/version", http::verb::get, [&](RequestManager<http::string_body>& request,
                                                    ResponseManager<http::string_body>& response)
    {
        response.setHeader(boost::beast::http::field::connection, "keep-alive");

        auto obj = JsonLoader::object();
        obj["major"] = 1;
        obj["patch"] = 0;
        obj["minor"] = 0;
        obj["text"] = "1.0.0";

        response.setBody(JsonLoader::jsonToIndentedString(obj));
    });
}
