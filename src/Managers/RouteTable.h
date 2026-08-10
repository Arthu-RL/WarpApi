#ifndef ROUTETABLE_H
#define ROUTETABLE_H

#pragma once

#include <bit>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <ink/ink_base.hpp>

/**
 * @class RouteTable
 * @brief Frozen, open-addressed exact-match route index.
 *
 * Routes are all registered before the workers start and never change
 * afterwards, which makes the general-purpose radix tree strictly more
 * machinery than the lookup needs: walking it costs one dependent pointer
 * dereference per path segment, and each node's label is a heap-allocated
 * std::string, so a miss is several cache misses deep.
 *
 * Since the key set is known and fixed at build time, this instead hashes the
 * whole path once and probes a flat array. The table is sized to at least 4x
 * the route count and rounded to a power of two, keeping the load factor under
 * 25% so linear probing almost always resolves in the first slot — and that
 * slot holds the hash inline, so the common case touches exactly one cache
 * line and only compares the string after the 64-bit hash already matched.
 *
 * @note Build once via insert()/freeze(); after that it is read-only and
 *       therefore safe to query concurrently from every worker with no
 *       synchronisation at all.
 */
template <typename T>
class RouteTable {
    static_assert(std::is_pointer_v<T>, "RouteTable uses a null value as its empty-slot marker");

public:
    /** @return false if @p path was already registered, or if already frozen. */
    bool insert(std::string_view path, T value)
    {
        // Inserting after freeze() would reallocate _entries and dangle every
        // key view the probe array holds, so it is refused outright rather
        // than left as a call-order landmine.
        if (_frozen)
            return false;

        for (const auto& e : _entries)
        {
            if (*e.key == path)
                return false;
        }
        _entries.push_back({std::make_unique<std::string>(path), value});
        return true;
    }

    /** Builds the probe array. Must be called before any lookup(). */
    void freeze()
    {
        // Load factor <= 25%: with a well-mixed hash this makes multi-probe
        // lookups vanishingly rare without the table ever being large enough
        // to matter (a few hundred routes is still only a handful of pages).
        usize cap = 16;
        while (cap < _entries.size() * 4)
            cap <<= 1;

        _slots.assign(cap, Slot{});
        _mask = cap - 1;

        for (const auto& e : _entries)
        {
            const u64 h = hash(*e.key);
            usize i = static_cast<usize>(h) & _mask;
            while (_slots[i].value != nullptr)
                i = (i + 1) & _mask;

            _slots[i].hash = h;
            // Heap-stable: the key lives in its own allocation, so growing
            // _entries can never move the bytes this view points at.
            _slots[i].key = *e.key;
            _slots[i].value = e.value;
        }

        _frozen = true;
    }

    T lookup(std::string_view path) const noexcept
    {
        if (!_frozen) [[unlikely]]
            return nullptr;

        const u64 h = hash(path);
        usize i = static_cast<usize>(h) & _mask;

        for (;;)
        {
            const Slot& s = _slots[i];
            if (s.value == nullptr)
                return nullptr;                      // empty slot terminates the probe
            if (s.hash == h && s.key == path) [[likely]]
                return s.value;
            i = (i + 1) & _mask;
        }
    }

    usize size() const noexcept { return _entries.size(); }

    /**
     * @brief 64-bit string hash with multiply-xor mixing.
     *
     * Consumes 8 bytes per round through a 64x64->128 folded multiply, which
     * gives full avalanche (every input bit affects every output bit) at a
     * couple of cycles per word — far stronger than FNV-1a's byte-at-a-time
     * mixing, so the low bits used for bucketing are well distributed and
     * adversarial paths cannot easily be made to collide en masse.
     */
    static u64 hash(std::string_view s) noexcept
    {
        constexpr u64 k0 = 0xA0761D6478BD642FULL;
        constexpr u64 k1 = 0xE7037ED1A0B428DBULL;

        auto mix = [](u64 a, u64 b) noexcept -> u64 {
            const __uint128_t r = static_cast<__uint128_t>(a) * b;
            return static_cast<u64>(r) ^ static_cast<u64>(r >> 64);
        };

        const char* p = s.data();
        usize len = s.size();
        u64 seed = k0 ^ static_cast<u64>(len);

        while (len >= 8)
        {
            u64 v;
            std::memcpy(&v, p, 8);
            seed = mix(seed ^ v, k1);
            p += 8;
            len -= 8;
        }

        if (len)
        {
            // Tail: pack the remaining 1-7 bytes without reading past the end.
            u64 v = 0;
            std::memcpy(&v, p, len);
            seed = mix(seed ^ v, k1);
        }

        return mix(seed, k0 ^ k1);
    }

private:
    struct Entry {
        std::unique_ptr<std::string> key;  // stable address for the frozen views
        T value;
    };

    struct Slot {
        u64 hash = 0;
        std::string_view key{};
        T value = nullptr;
    };

    std::vector<Entry> _entries;
    std::vector<Slot> _slots;
    usize _mask = 0;
    bool _frozen = false;
};

#endif // ROUTETABLE_H
