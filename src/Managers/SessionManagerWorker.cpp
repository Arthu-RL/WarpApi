#include "SessionManagerWorker.h"

#include "Server/Session.h"
#include "Settings/Settings.h"

SessionManagerWorker::SessionManagerWorker(size_t cleanup_delay_secs) :
    ink::WorkerThread(ink::WorkerThread::Policy::KillImediately, cleanup_delay_secs),
    _connectionTimeoutMs(Settings::getSettings().connection_timeout_ms)
{
    // Empty
}

SessionManagerWorker* SessionManagerWorker::getInstance() noexcept
{
    static SessionManagerWorker instance;
    return &instance;
}

std::shared_ptr<Session> SessionManagerWorker::getSession(socket_t client_sock_fd) noexcept
{
    auto it = _connections.find(client_sock_fd);
    if (it == _connections.end())
    {
        return nullptr;
    }

    return it->second;
}

void SessionManagerWorker::addClientSession(socket_t client_sock_fd, std::shared_ptr<Session> session) noexcept
{
    _connections[client_sock_fd] = session;
}

void SessionManagerWorker::setConnectionTimeout(size_t connection_timeout_ms) noexcept
{
    _connectionTimeoutMs = connection_timeout_ms;
}

void SessionManagerWorker::process()
{
    auto timeout = std::chrono::milliseconds(_connectionTimeoutMs);

    for (auto it = _connections.begin(); it != _connections.end();)
    {
        if (it->second->isIdle(timeout)) {
            it->second->close();
            it = _connections.unsafe_erase(it);
        }
        else if (!it->second->isActive())
        {
            it = _connections.unsafe_erase(it);
        }
        else {
            ++it;
        }
    }
}
