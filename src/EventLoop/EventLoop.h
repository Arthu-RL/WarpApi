#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#pragma once

#include "WarpDefs.h"
#include <atomic>
#include <thread>
#include <vector>

class Session;

/**
 * @class EventLoop
 * @brief Shared-nothing worker pool: one listening socket, one poller and one
 *        session pool per thread, with no shared mutable state between them.
 *
 * The listening sockets are created up front on the calling thread so bind or
 * permission failures surface at startup instead of inside a worker, and so the
 * SO_REUSEPORT group is complete before the kernel steering program is attached.
 */
class WARP_API EventLoop {
public:
    EventLoop();
    ~EventLoop();

    void start();
    void stop();

private:
    /** @brief Creates and binds one listening socket per worker. */
    bool setupListeners(u32 count);

    /** @brief Even connection distribution across the SO_REUSEPORT group. */
    void attachReuseportSteering();

    void closeListeners();

    // The main function running on every thread
    void runWorker(i32 threadIdx, socket_t listenFd);

    std::atomic<bool> _running;
    std::vector<std::thread> _threads;
    std::vector<socket_t> _listenFds;
};

#endif // EVENT_LOOP_H
