#include "Settings.h"

#include <algorithm>
#include <thread>

// Static member initialization
SettingsData Settings::_data;
bool Settings::_initialized = false;

bool SettingsData::isValid() const {
    if (port < 1) {
        INK_ERROR << "Invalid port number: " << port;
        return false;
    }

    if (ip.empty()) {
        INK_ERROR << "IP address cannot be empty";
        return false;
    }

    if (max_threads == 0) {
        INK_ERROR << "max_threads must be at least 1";
        return false;
    }

    if (backlog_size <= 0) {
        INK_ERROR << "backlog_size must be greater than 0";
        return false;
    }

    if (connection_timeout_ms < 1000) {
        INK_ERROR << "connection_timeout_ms must be at least 1000 (the timer wheel ticks per second)";
        return false;
    }

    if (read_buffer_size == 0 || write_buffer_size == 0) {
        INK_ERROR << "read_buffer_size and write_buffer_size must be greater than 0";
        return false;
    }

    // The read buffer has to be able to hold headers plus the largest body we
    // advertise, otherwise a legal request could never be assembled.
    if (max_request_size < max_body_size) {
        INK_ERROR << "max_request_size (" << max_request_size
                  << ") must be >= max_body_size (" << max_body_size << ')';
        return false;
    }

    if (max_request_size < read_buffer_size || max_response_size < write_buffer_size) {
        INK_ERROR << "Buffer caps must be >= their initial sizes";
        return false;
    }

    return true;
}

Settings::Settings(const ink::EnhancedJson& configs)
{
    if (!_initialized) {
        loadSettings(configs, _data);
        _initialized = _data.isValid();
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
        const u32 hw = std::max(1u, std::thread::hardware_concurrency());

        data.ip = configs.get<std::string>("ip", "0.0.0.0");
        data.port = configs.get<u16>("port", 8080);

        // 0 means "one worker per core", which is the shared-nothing sweet spot.
        const u32 requested = configs.get<u32>("max_threads", 0);
        data.max_threads = (requested == 0) ? hw : std::min(requested, hw);

        data.backlog_size = configs.get<i32>("backlog_size", SOMAXCONN);
        data.connection_timeout_ms = configs.get<usize>("connection_timeout_ms", 60000);

        data.max_body_size = configs.get<usize>("max_body_size", 64 * 1024);
        data.max_request_size = configs.get<usize>("max_request_size", 128 * 1024);
        data.max_response_size = configs.get<usize>("max_response_size", 1024 * 1024);

        data.read_buffer_size = configs.get<usize>("read_buffer_size", 4096);
        data.write_buffer_size = configs.get<usize>("write_buffer_size", 4096);

        data.cpu_affinity = configs.get<bool>("cpu_affinity", true);
        data.reuseport_cbpf = configs.get<bool>("reuseport_cbpf", true);

        return true;
    }
    catch (const std::exception& e) {
        INK_ERROR << "Failed to load settings: " << e.what();
        return false;
    }
}
