#include "GeneralServices.h"

#include <plog/Log.h>
#include <nlohmann/json.hpp>

#include "Utils/JsonLoader.h"

GeneralServices::GeneralServices() {
    registerAllEndpoints();
}

void GeneralServices::registerAllEndpoints()
{
    registerEndpoint("/", http::verb::get, [&](ResponseManager<http::string_body>& response){
        // response.setHeader(boost::beast::http::field::connection, "keep-alive");
        nlohmann::json meta_obj = JsonLoader::meta_info();
        response.setBody(JsonLoader::jsonToIndentedString(meta_obj));
    });

    registerEndpoint("/hello", http::verb::get, [&](ResponseManager<http::string_body>& response){
        nlohmann::json obj = JsonLoader::object();
        obj["message"] = "Hello, World!";
        response.setBody(JsonLoader::jsonToIndentedString(obj));
    });
}
