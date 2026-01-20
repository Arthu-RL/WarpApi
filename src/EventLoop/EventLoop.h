#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#pragma once

#include "WarpDefs.h"
#include <thread>
#include <vector>
#include <atomic>

class Session;

class WARP_API EventLoop {
public:
    EventLoop();
    ~EventLoop();

    void start();
    void stop();

private:
    // The main function running on every thread
    void runWorker(i32 threadIdx);

    std::atomic<bool> _running;
    std::vector<std::thread> _threads;
};

#endif // EVENT_LOOP_H
