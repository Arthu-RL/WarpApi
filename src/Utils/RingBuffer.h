#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#pragma once

#include "WarpDefs.h"

class WARP_API RingBuffer {
public:
    explicit RingBuffer(size_t capacity = 8192)
        : _buffer(new char[capacity]),
        _capacity(capacity),
        _readPos(0),
        _writePos(0),
        _size(0) {}

    ~RingBuffer() {
        delete[] _buffer;
    }

    // Non-copyable
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // Read data from the buffer
    size_t read(char* dest, size_t maxLen)
    {
        if (maxLen == 0 || _size == 0) return 0;

        size_t bytesToRead = std::min(maxLen, _size);

        // Handle wrap-around case
        if (_readPos + bytesToRead <= _capacity) {
            // Simple case: no wrap-around
            memcpy(dest, _buffer + _readPos, bytesToRead);
            _readPos = (_readPos + bytesToRead) % _capacity;
        } else {
            // Complex case: need to wrap around
            size_t firstChunk = _capacity - _readPos;
            memcpy(dest, _buffer + _readPos, firstChunk);

            size_t secondChunk = bytesToRead - firstChunk;
            memcpy(dest + firstChunk, _buffer, secondChunk);

            _readPos = secondChunk;
        }

        _size -= bytesToRead;
        return bytesToRead;
    }

    // Write data to the buffer
    size_t write(const char* data, size_t len)
    {
        if (len == 0 || _size == _capacity) return 0;

        size_t availableSpace = _capacity - _size;
        size_t bytesToWrite = std::min(len, availableSpace);

        // Handle wrap-around case
        if (_writePos + bytesToWrite <= _capacity) {
            // Simple case: no wrap-around
            memcpy(_buffer + _writePos, data, bytesToWrite);
            _writePos = (_writePos + bytesToWrite) % _capacity;
        } else {
            // Complex case: need to wrap around
            size_t firstChunk = _capacity - _writePos;
            memcpy(_buffer + _writePos, data, firstChunk);

            size_t secondChunk = bytesToWrite - firstChunk;
            memcpy(_buffer, data + firstChunk, secondChunk);

            _writePos = secondChunk;
        }

        _size += bytesToWrite;
        return bytesToWrite;
    }

    // Get a contiguous read buffer (for zero-copy operations)
    const char* getReadBuffer(size_t& availableData) const
    {
        if (_size == 0) {
            availableData = 0;
            return nullptr;
        }

        if (_readPos < _writePos)
            availableData = _writePos - _readPos; // Simple case: read position before write position
        else
            availableData = _capacity - _readPos; // Complex case: read position after write position (wrap-around)

        return _buffer + _readPos;
    }

    // Get a contiguous write buffer (for zero-copy operations)
    char* getWriteBuffer(size_t& availableSpace)
    {
        if (_size == _capacity) {
            availableSpace = 0;
            return nullptr;
        }

        if (_writePos >= _readPos)
            availableSpace = _capacity - _writePos; // Write position at or after read position
        else
            availableSpace = _readPos - _writePos; // Write position before read position

        return _buffer + _writePos;
    }

    // Advance read position after reading data
    void advanceReadPos(size_t len)
    {
        size_t bytesToAdvance = std::min(len, _size);
        _readPos = (_readPos + bytesToAdvance) % _capacity;
        _size -= bytesToAdvance;
    }

    // Advance write position after writing data
    void advanceWritePos(size_t len)
    {
        size_t bytesToAdvance = std::min(len, _capacity - _size);
        _writePos = (_writePos + bytesToAdvance) % _capacity;
        _size += bytesToAdvance;
    }

    // Clear buffer
    void clear()
    {
        _readPos = 0;
        _writePos = 0;
        _size = 0;
    }

    // Utility methods
    size_t size() const { return _size; }
    size_t capacity() const { return _capacity; }
    bool empty() const { return _size == 0; }
    bool full() const { return _size == _capacity; }

private:
    char* _buffer;
    size_t _capacity;
    size_t _readPos;
    size_t _writePos;
    size_t _size;
};

#endif // RINGBUFFER_H
