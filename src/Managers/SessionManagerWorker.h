#ifndef SESSIONMANAGERWORKER_H
#define SESSIONMANAGERWORKER_H

#include <ink/WorkerThread.h>

#include "WarpDefs.h"

class WARP_API SessionManagerWorker : public ink::WorkerThread
{
public:
    static SessionManagerWorker* getInstance() noexcept;

    std::shared_ptr<Session> getSession(socket_t client_sock_fd) noexcept;
    size_t getSessionTableSize() noexcept;
    void addClientSession(socket_t client_sock_fd, std::shared_ptr<Session> session) noexcept;
    // void removeSession(socket_t client_sock_fd) noexcept;
    void setConnectionTimeout(size_t connection_timeout_ms) noexcept;

protected:
    SessionManagerWorker(size_t cleanup_delay_secs = 5);

    virtual void process();

private:
    size_t _connectionTimeoutMs;
    SessionTable _connections;
};

#endif // SESSIONMANAGERWORKER_H
