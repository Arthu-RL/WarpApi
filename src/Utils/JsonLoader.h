#ifndef JSONLOADER_H
#define JSONLOADER_H

#pragma once

#include <nlohmann/json.hpp>
#include <string>

class JsonLoader
{
public:
    JsonLoader();
    ~JsonLoader();

    static nlohmann::json array();

    static nlohmann::json object();

    static nlohmann::json loadJsonFromFile(const std::string& filePath);

    static nlohmann::json loadJsonFromString(const std::string& jDoc);

    static const std::string jsonToCompactString(nlohmann::json& jObj);

    static const std::string jsonToIndentedString(nlohmann::json& jObj);
};

#endif // JSONLOADER_H
