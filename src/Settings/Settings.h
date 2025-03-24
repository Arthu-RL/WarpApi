#ifndef SETTINGS_H
#define SETTINGS_H
#pragma once
#include "WarpDefs.h"
#include <string>
#include <mutex>

struct WARP_API SettingsData {
    uint16_t port;
    std::string ip;
    uint max_threads;
    uint max_auxiliar_threads;
    uint backlog_size;
    uint connection_timeout_ms;

    // Add validation function
    bool isValid() const;
};

class WARP_API Settings
{
public:
    // Constructor loads settings
    Settings(const ink::EnhancedJson& configs);

    // Get settings with thread safety
    static SettingsData getSettings();

    // Check if current settings are valid
    static bool isValid();

    // Update settings (thread-safe)
    static bool updateSettings(const ink::EnhancedJson& configs);

private:
    // Load settings from config into data
    static bool loadSettings(const ink::EnhancedJson& configs, SettingsData& data);

    static SettingsData _data;
    static std::mutex _mutex; // For thread safety
    static bool _initialized;
};

#endif // SETTINGS_H
