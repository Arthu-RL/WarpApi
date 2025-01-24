#include "GeneralServices.h"

#include <plog/Log.h>
#include <nlohmann/json.hpp>

#include "Utils/JsonLoader.h"

GeneralServices::GeneralServices() {
    registerAllEndpoints();
}

void GeneralServices::registerAllEndpoints()
{
    registerEndpoint("/", http::verb::get,
                     [&](RequestManager<http::string_body>& request, ResponseManager<http::string_body>& response)
    {
        // response.setHeader(boost::beast::http::field::connection, "keep-alive");
        nlohmann::json meta_obj = JsonLoader::meta_info();
        response.setBody(JsonLoader::jsonToIndentedString(meta_obj));
    });

    registerEndpoint("/test", http::verb::get,
                     [&](RequestManager<http::string_body>& request, ResponseManager<http::string_body>& response)
    {
        const auto& params = request.getQueryParams();
        const auto& body = request.getRequestBody();
        auto jObj = JsonLoader::loadJsonFromString(body);

        PLOG_DEBUG << JsonLoader::jsonToCompactString(jObj);

        auto result = JsonLoader::object();

        for (auto it=params.begin(); it != params.end(); ++it)
        {
            result[it->first] = it->second;
        }

        for (auto it=jObj.begin(); it != jObj.end(); ++it)
        {
            result[it.key()] = it.value().get<std::string>();
        }

        response.setBody(JsonLoader::jsonToIndentedString(result));

        // FOR ENDPOINT VALIDATION

        // auto it = jObj.find("test_body");
        // if (it != jObj.end())
        // {
        //     obj[it.key()] = it.value().get<std::string>();
        // }

        // response.setStatus(http::status::bad_request);
        // response.setBody("Expected `test_id` param.");
    });

    registerEndpoint("/health", http::verb::get,
                     [&](RequestManager<http::string_body>& request, ResponseManager<http::string_body>& response)
    {
        // response.setHeader(http::field::content_type, "plain/text");
        auto obj = JsonLoader::object();
        obj["status"] = "ok";
        response.setBody(JsonLoader::jsonToIndentedString(obj));
    });

    registerEndpoint("/version", http::verb::get,
                     [&](RequestManager<http::string_body>& request, ResponseManager<http::string_body>& response)
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
