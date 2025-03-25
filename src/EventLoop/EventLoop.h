#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#pragma once

#include "WarpDefs.h"
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>

class WARP_API EventLoop {
public:
    EventLoop();
    ~EventLoop();

    // Start/stop the event loop
    void start();
    void stop();

    // Session management
    void addSession(std::shared_ptr<Session> session);
    void updateSessionInterest(std::shared_ptr<Session> session, bool interestedInReading, bool interestedInWriting);
    void removeSession(std::shared_ptr<Session> session);

private:
    // Event loop thread function
    void run();

#ifdef _WIN32
    // Windows-specific implementation
    // Type for IOCP operations
    enum OperationType {
        READ_OPERATION = 1,
        WRITE_OPERATION = 2,
        EXIT_CODE = 0xFFFFFFFF
    };

    // Windows implementation using IOCP
    HANDLE _completionPort;

    // Helper methods
    void runWindows();
    void updateSessionInterestWindows(std::shared_ptr<Session> session, bool interestedInReading, bool interestedInWriting);
#else
    // Linux-specific implementation
    int _epollFd;
    int _wakeupFd = -1;

    // Helper methods
    void runLinux();
    void updateSessionInterestLinux(std::shared_ptr<Session> session, bool interestedInReading, bool interestedInWriting);
#endif

    // Session tracking with thread safety
    SessionTable _sessions;
    std::mutex _sessionsMutex;

    std::vector<std::thread> _threads;
    std::atomic<bool> _running;
};

#endif // EVENT_LOOP_H
