#ifndef SETTINGS_H
#define SETTINGS_H

#pragma once

#include "WarpDefs.h"
#include <string>

struct WARP_API SettingsData {
    uint16_t port;
    std::string ip;
    uint max_threads;
    uint max_auxiliar_threads;
    size_t backlog_size;
    size_t connection_timeout_ms;
    size_t max_body_size;
    size_t max_request_size;
    size_t max_response_size;

    // Add validation function
    bool isValid() const;
};

/**
 * @brief Full static Settings class for easy accesss.
 */
class WARP_API Settings
{
public:
    // Constructor loads settings
    Settings(const ink::EnhancedJson& configs);

    // Get settings
    static const SettingsData& getSettings() noexcept;

    // Check if current settings are valid
    static const bool isValid() noexcept;

    // Update settings (thread-safe)
    static bool updateSettings(const ink::EnhancedJson& configs);

private:
    // Load settings from config into data
    static bool loadSettings(const ink::EnhancedJson& configs, SettingsData& data);

    static SettingsData _data;
    static bool _initialized;
};

#endif // SETTINGS_H
