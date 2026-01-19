#include "SessionManagerWorker.h"

#include "Server/Session.h"
#include "Settings/Settings.h"

SessionManagerWorker::SessionManagerWorker(size_t cleanup_delay_secs) :
    ink::WorkerThread(ink::WorkerThread::Policy::WaitProcessFinish, cleanup_delay_secs),
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

size_t SessionManagerWorker::getSessionTableSize() noexcept
{
    return _connections.size();
}

void SessionManagerWorker::addClientSession(socket_t client_sock_fd, std::shared_ptr<Session> session) noexcept
{
    _connections[client_sock_fd] = session;
}

// void SessionManagerWorker::removeSession(socket_t client_sock_fd) noexcept
// {
//     _connections.unsafe_erase(client_sock_fd);
// }

void SessionManagerWorker::setConnectionTimeout(size_t connection_timeout_ms) noexcept
{
    _connectionTimeoutMs = connection_timeout_ms;
}

void SessionManagerWorker::process()
{
    static auto timeout = std::chrono::milliseconds(_connectionTimeoutMs);
    std::vector<socket_t> timedOutSockets;

    for (auto it = _connections.begin(); it != _connections.end(); ++it)
    {
        if (it->second->getSocket() == SOCKET_ERROR_VALUE || it->second->isIdle(timeout)) {
            timedOutSockets.push_back(it->first);
        }
    }

    for (socket_t sock : timedOutSockets)
    {
        auto it = _connections.find(sock);
        if (it != _connections.end())
        {
            it->second->close();
            _connections.unsafe_erase(it);
        }
    }
}
