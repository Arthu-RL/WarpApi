#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#pragma once

#include "WarpDefs.h"
// #include <memory>
#include <thread>
#include <vector>
#include <atomic>

class WARP_API EventLoop {
public:
    EventLoop();
    ~EventLoop();

    // Start/stop the event loop
    void start();
    void stop();

    // Session management
    void addSession(std::shared_ptr<Session> session);
    void updateSessionInterest(std::shared_ptr<Session> session, const SessionInterest iOinterest);

private:
    // Event loop thread function
    void run(i32 threadIdx, socket_t epollfd, socket_t wakeupfd);

    // Linux-specific implementation
    std::vector<socket_t> _workerEpollFds;
    std::vector<socket_t> _workerWakeupFds;
    std::vector<std::thread> _threads;

    std::atomic<bool> _running;

    size_t _nextThreadIdx{0};
};

#endif // EVENT_LOOP_H
