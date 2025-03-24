#ifndef THREADPOOLMANAGER_H
#define THREADPOOLMANAGER_H

#pragma once

#include "WarpDefs.h"

class WARP_API ThreadPoolManager
{
public:
    ThreadPoolManager();

    static ink::ThreadPool* pool() noexcept;
private:
    static ink::ThreadPool _pool;
};

#endif // THREADPOOLMANAGER_H
