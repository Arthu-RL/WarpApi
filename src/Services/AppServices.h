#ifndef APPSERVICES_H
#define APPSERVICES_H

#pragma once

#include <atomic>
#include <string>

#include "WarpDefs.h"

/**
 * @file AppServices.h
 * @brief Example application services, injected rather than reached globally.
 *
 * These exist to demonstrate the shape a real dependency takes: constructed
 * once at startup, owned by the ServiceContainer, and handed to routes by
 * reference.
 *
 * @warning Every worker thread runs handlers concurrently and WarpApi's
 *          workers never synchronize with each other, so a service is shared
 *          mutable state by definition. Each one below is therefore either
 *          immutable after construction (BuildInfo) or explicitly atomic
 *          (RequestCounter). A service that is neither is a data race.
 */

/** Immutable after construction, so it is trivially safe to share. */
class WARP_API BuildInfo {
public:
    BuildInfo(std::string name, std::string version) :
        _name(std::move(name)), _version(std::move(version)) {}

    const std::string& name() const noexcept { return _name; }
    const std::string& version() const noexcept { return _version; }

private:
    const std::string _name;
    const std::string _version;
};

/**
 * Mutable and shared, so the counter is atomic.
 *
 * `relaxed` is deliberate: the count orders nothing else, and readers only
 * need it to be a valid number, not to synchronize with the work that
 * produced it. Paying for acquire/release here would be pure cost.
 */
class WARP_API RequestCounter {
public:
    void increment() noexcept { _count.fetch_add(1, std::memory_order_relaxed); }
    u64 value() const noexcept { return _count.load(std::memory_order_relaxed); }

private:
    std::atomic<u64> _count{0};
};

#endif // APPSERVICES_H
