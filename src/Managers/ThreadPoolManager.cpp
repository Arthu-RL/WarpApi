#include "ThreadPoolManager.h"
#include "Settings/Settings.h"

ink::ThreadPool ThreadPoolManager::_pool(Settings::getSettings().max_threads);

ThreadPoolManager::ThreadPoolManager()
{
    // Empty
}

ink::ThreadPool* ThreadPoolManager::pool() noexcept
{
    return &_pool;
}
