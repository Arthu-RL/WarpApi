#include "Settings.h"

// Static member initialization
SettingsData Settings::_data;
bool Settings::_initialized = false;

bool SettingsData::isValid() const {
    // Check port is within valid range
    if (port < 1 || port > 65535) {
        INK_ERROR << "Invalid port number: " << port;
        return false;
    }

    // Check IP format (basic validation)
    if (ip.empty()) {
        INK_ERROR << "IP address cannot be empty";
        return false;
    }

    // Check thread counts
    if (max_threads == 0) {
        INK_ERROR << "max_threads must be at least 1";
        return false;
    }

    if (max_auxiliar_threads == 0 || max_auxiliar_threads > max_threads) {
        INK_ERROR << "max_event_loop_threads must be between 1 and max_threads";
        return false;
    }

    // Check reasonable backlog size
    if (backlog_size == 0) {
        INK_ERROR << "backlog_size must be greater than 0";
        return false;
    }

    return true;
}

Settings::Settings(const ink::EnhancedJson& configs)
{
    if (!_initialized) {
        loadSettings(configs, _data);
        _initialized = isValid();
    }
}

const SettingsData& Settings::getSettings() noexcept
{
    return _data;
}

bool Settings::isValid() noexcept
{
    return _data.isValid();
}

bool Settings::updateSettings(const ink::EnhancedJson& configs)
{
    SettingsData newData = _data; // Start with existing settings

    if (loadSettings(configs, newData) && newData.isValid())
    {
        _data = newData;
        return true;
    }
    return false;
}

bool Settings::loadSettings(const ink::EnhancedJson& configs, SettingsData& data)
{
    try {
        data.ip = configs.get<std::string>("ip", "0.0.0.0");
        data.port = configs.get<uint16_t>("port", 8080);
        data.max_threads = std::min(configs.get<uint>("max_threads", 1),
                                    std::thread::hardware_concurrency());
        data.max_auxiliar_threads = std::min(configs.get<uint>("max_event_loop_threads", 1),
                                               data.max_threads);
        data.backlog_size = configs.get<size_t>("backlog_size", SOMAXCONN);
        data.connection_timeout_ms = configs.get<size_t>("connection_timeout_ms", 60000);
        data.max_body_size = configs.get<size_t>("max_body_size", 1 * 1024 * 1024);
        data.max_request_size = configs.get<size_t>("max_body_size", 1 * 1024 * 1024);
        data.max_response_size = configs.get<size_t>("max_body_size", 1 * 1024 * 1024);

        return true;
    }
    catch (const std::exception& e) {
        INK_ERROR << "Failed to load settings: " << e.what();
        return false;
    }
}
