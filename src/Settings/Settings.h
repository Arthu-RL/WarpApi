#ifndef SETTINGS_H
#define SETTINGS_H

#pragma once

#include "WarpDefs.h"
#include <string>

struct WARP_API SettingsData {
    u16 port = 8080;
    std::string ip = "0.0.0.0";
    u32 max_threads = 1;
    i32 backlog_size = SOMAXCONN;
    usize connection_timeout_ms = 60000;

    /// Largest body (or reassembled WebSocket message) we will accept.
    usize max_body_size = 64 * 1024;
    /// Hard cap the per-connection read buffer may grow to.
    usize max_request_size = 128 * 1024;
    /// Hard cap the per-connection write buffer may grow to.
    usize max_response_size = 1024 * 1024;

    /// Starting size of each per-connection buffer; they grow on demand and
    /// shrink back once drained, so this is the steady-state cost per socket.
    usize read_buffer_size = 4096;
    usize write_buffer_size = 4096;

    /// Pin each worker to one core (shared-nothing); disable when sharing a box.
    bool cpu_affinity = true;
    /// Let the kernel hash new connections evenly across the SO_REUSEPORT group.
    bool reuseport_cbpf = true;

    bool isValid() const;
};

/**
 * @brief Process-wide settings.
 *
 * Loaded once at startup, before any worker thread exists, and treated as
 * immutable afterwards; the workers only ever read it.
 */
class WARP_API Settings
{
public:
    explicit Settings(const ink::EnhancedJson& configs);

    static const SettingsData& getSettings() noexcept;

    static bool isValid() noexcept;

    static bool updateSettings(const ink::EnhancedJson& configs);

private:
    static bool loadSettings(const ink::EnhancedJson& configs, SettingsData& data);

    static SettingsData _data;
    static bool _initialized;
};

#endif // SETTINGS_H
