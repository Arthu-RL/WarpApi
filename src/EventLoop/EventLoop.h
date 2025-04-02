#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#pragma once

#include "WarpDefs.h"
#include <memory>
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
    // TODO USE ONE EPOLLFD AND WAKEFD FOR EACH WORKER
    std::unordered_map<std::thread::id, ink_i32> _workerEpollFd;
    std::unordered_map<std::thread::id, ink_i32> _workerWakeupFd;

    // Helper methods
    void runLinux();
    void updateSessionInterestLinux(std::shared_ptr<Session> session, bool interestedInReading, bool interestedInWriting);
#endif

    // Session tracking with thread safety
    SessionTable _sessions;

    std::vector<std::thread> _threads;
    std::atomic<ink_bool> _running;

    static ink_u16 _currentThread;
    std::mutex _addSessionMutex;
};

#endif // EVENT_LOOP_H
