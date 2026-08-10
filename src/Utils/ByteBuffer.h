#ifndef BYTEBUFFER_H
#define BYTEBUFFER_H

#pragma once

#include <bit>
#include <cstring>
#include <new>
#include <string_view>
#include <utility>

#include <ink/ink_base.hpp>

/**
 * @class ByteBuffer
 * @brief Contiguous, growable, single-threaded I/O buffer.
 *
 * Deliberately *not* a ring buffer. A ring hands out only the bytes up to the
 * wrap point, so any HTTP request (or WebSocket frame) that straddles the wrap
 * can never be parsed as one contiguous view — the parser stalls forever and
 * the connection hangs until the keep-alive timer kills it.
 *
 * Instead this keeps a single contiguous region and reclaims consumed bytes by
 * compaction, only reallocating when a message genuinely does not fit. In the
 * steady state (request consumed fully, buffer drained) `advanceRead` resets
 * both cursors to zero, so neither a memmove nor an allocation ever happens on
 * the hot path.
 *
 * Cache-line aligned so the read cursor of one session never shares a line with
 * unrelated data.
 */
class ByteBuffer {
public:
    static constexpr usize kAlignment = 64;
    static constexpr usize kMinCapacity = 256;

    explicit ByteBuffer(usize initialCapacity = 8192, usize maxCapacity = 1u << 20)
    {
        _cap = roundPow2(initialCapacity < kMinCapacity ? kMinCapacity : initialCapacity);
        _max = maxCapacity < _cap ? _cap : maxCapacity;
        _data = allocate(_cap);
        if (!_data) // let the caller fail this one connection, not the process
            _cap = 0;
    }

    /** False when the initial allocation failed; the buffer is unusable. */
    bool valid() const noexcept { return _data != nullptr; }

    ~ByteBuffer() { deallocate(_data, _cap); }

    ByteBuffer(const ByteBuffer&) = delete;
    ByteBuffer& operator=(const ByteBuffer&) = delete;

    ByteBuffer(ByteBuffer&& o) noexcept :
        _data(o._data), _cap(o._cap), _max(o._max), _read(o._read), _write(o._write)
    {
        o._data = nullptr;
        o._cap = o._read = o._write = 0;
    }

    ByteBuffer& operator=(ByteBuffer&& o) noexcept
    {
        if (this != &o)
        {
            deallocate(_data, _cap);
            _data = o._data; _cap = o._cap; _max = o._max;
            _read = o._read; _write = o._write;
            o._data = nullptr;
            o._cap = o._read = o._write = 0;
        }
        return *this;
    }

    void swap(ByteBuffer& o) noexcept
    {
        std::swap(_data, o._data);
        std::swap(_cap, o._cap);
        std::swap(_max, o._max);
        std::swap(_read, o._read);
        std::swap(_write, o._write);
    }

    /** Bytes produced but not yet consumed. */
    usize size() const noexcept { return _write - _read; }
    usize capacity() const noexcept { return _cap; }
    usize maxCapacity() const noexcept { return _max; }
    bool empty() const noexcept { return _write == _read; }

    /** Start of the readable region; always contiguous. */
    const char* readPtr(usize& avail) const noexcept
    {
        avail = _write - _read;
        return _data + _read;
    }

    const char* readPtr() const noexcept { return _data + _read; }

    /** Mutable view of the readable region (used for in-place WebSocket unmasking). */
    char* mutableReadPtr(usize& avail) noexcept
    {
        avail = _write - _read;
        return _data + _read;
    }

    void advanceRead(usize n) noexcept
    {
        _read += (n > size() ? size() : n);
        if (_read == _write) // fully drained: rewind, keeps the hot path allocation-free
            _read = _write = 0;
    }

    /**
     * @brief Reserve at least @p minSpace writable bytes and return the region.
     * @return nullptr when the buffer is at its hard cap and cannot make room.
     */
    char* prepareWrite(usize minSpace, usize& space) noexcept
    {
        if (!ensureWritable(minSpace))
        {
            space = 0;
            return nullptr;
        }
        space = _cap - _write;
        return _data + _write;
    }

    void advanceWrite(usize n) noexcept
    {
        const usize room = _cap - _write;
        _write += (n > room ? room : n);
    }

    bool ensureWritable(usize minSpace) noexcept
    {
        if (_cap - _write >= minSpace)
            return true;

        const usize used = _write - _read;

        // Compaction alone is enough: slide the live bytes to the front.
        if (_cap - used >= minSpace)
        {
            if (used && _read)
                std::memmove(_data, _data + _read, used);
            _read = 0;
            _write = used;
            return true;
        }

        const usize need = used + minSpace;
        if (need > _max)
            return false;

        // Round up to the next power of two in one instruction rather than a
        // shift loop: countl_zero maps directly to LZCNT/BSR.
        usize ncap = nextPow2(need);
        if (ncap > _max)
            ncap = _max;

        char* nd = allocate(ncap);
        if (!nd)
            return false; // out of memory: fail this connection, not the process

        if (used)
            std::memcpy(nd, _data + _read, used);
        deallocate(_data, _cap);

        _data = nd;
        _cap = ncap;
        _read = 0;
        _write = used;
        return true;
    }

    bool append(const char* p, usize n) noexcept
    {
        if (n == 0) return true;
        if (!ensureWritable(n)) return false;
        std::memcpy(_data + _write, p, n);
        _write += n;
        return true;
    }

    bool append(std::string_view sv) noexcept { return append(sv.data(), sv.size()); }

    bool appendByte(char c) noexcept
    {
        if (!ensureWritable(1)) return false;
        _data[_write++] = c;
        return true;
    }

    void reset() noexcept { _read = _write = 0; }

    /**
     * @brief Give memory back after a burst so idle keep-alive connections do
     *        not each pin a grown buffer for the lifetime of the process.
     */
    void shrinkTo(usize target) noexcept
    {
        const usize ncap = roundPow2(target < kMinCapacity ? kMinCapacity : target);

        // Bail out when the rounded target is not actually smaller, otherwise a
        // non-power-of-two target churns an allocate+free pair for nothing.
        if (ncap >= _cap || size() > ncap)
            return;

        const usize used = _write - _read;
        char* nd = allocate(ncap);
        if (!nd)
            return; // shrinking is opportunistic; keeping the bigger buffer is fine

        if (used)
            std::memcpy(nd, _data + _read, used);
        deallocate(_data, _cap);

        _data = nd;
        _cap = ncap;
        _read = 0;
        _write = used;
    }

private:
    /** Non-throwing: callers turn a null result into a per-connection failure. */
    static char* allocate(usize n) noexcept
    {
        return static_cast<char*>(
            ::operator new[](n, std::align_val_t(kAlignment), std::nothrow));
    }

    static void deallocate(char* p, usize n) noexcept
    {
        if (p)
            ::operator delete[](p, n, std::align_val_t(kAlignment));
    }

    static usize nextPow2(usize v) noexcept
    {
        if (v <= kMinCapacity) return kMinCapacity;
        return usize{1} << (64 - static_cast<usize>(std::countl_zero(v - 1)));
    }

    static usize roundPow2(usize v) noexcept { return nextPow2(v); }

    char* _data = nullptr;
    usize _cap = 0;
    usize _max = 0;
    usize _read = 0;
    usize _write = 0;
};

#endif // BYTEBUFFER_H
