#ifndef PATTERNROUTES_H
#define PATTERNROUTES_H

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Request/HttpRequest.h"

namespace warp {

/**
 * @brief Routes containing `:name` segments, matched segment by segment.
 *
 * Deliberately separate from RouteTable: a pattern cannot be hashed, so
 * exact routes keep their O(1) probe and only fall through to here on a miss.
 * Real APIs are mostly exact paths, so the common case never touches this.
 *
 * @note match() is a linear scan over every registered pattern — O(n) in the
 *       number of *pattern* routes specifically (not total routes: exact
 *       paths never reach here at all). Left unbucketed deliberately: a REST
 *       API has tens of `:param` routes, not thousands, so a segment-count
 *       index would add a real data structure to save a scan over a handful
 *       of entries that would already be resolved by the time you noticed.
 *       Revisit only if a real workload registers enough pattern routes
 *       (roughly triple digits) for this to show up in a profile.
 */
template <typename T>
class PatternRoutes {
public:
    /** @return false if @p pattern has no `:` segment (caller should use the exact table). */
    static bool isPattern(std::string_view pattern) noexcept
    {
        for (usize i = 0; i + 1 < pattern.size(); ++i)
        {
            if (pattern[i] == '/' && pattern[i + 1] == ':')
                return true;
        }
        return false;
    }

    /** @return false if an identical pattern is already registered. */
    bool insert(std::string_view pattern, T value)
    {
        std::vector<std::string> segs;
        split(pattern, segs);

        for (const auto& r : _routes)
        {
            if (r.segments == segs)
                return false;
        }

        _routes.push_back({std::move(segs), value});
        return true;
    }

    /**
     * @brief First pattern matching @p path; captured segments go to @p req.
     *
     * Params are only written on a successful match, so a failed candidate
     * cannot leave stale captures behind for the next one.
     */
    T match(std::string_view path, HttpRequest* req) const noexcept
    {
        if (_routes.empty())
            return nullptr;

        for (const auto& r : _routes)
        {
            if (tryMatch(r, path, req))
                return r.value;
        }
        return nullptr;
    }

    usize size() const noexcept { return _routes.size(); }

private:
    struct Route {
        std::vector<std::string> segments; ///< a leading ':' marks a capture
        T value;
    };

    /** Splits on '/', ignoring the leading and any trailing slash. */
    static void split(std::string_view p, std::vector<std::string>& out)
    {
        if (!p.empty() && p.front() == '/') p.remove_prefix(1);
        while (!p.empty() && p.back() == '/') p.remove_suffix(1);
        if (p.empty()) return;

        usize pos = 0;
        while (pos <= p.size())
        {
            usize end = p.find('/', pos);
            if (end == std::string_view::npos) end = p.size();
            out.emplace_back(p.substr(pos, end - pos));
            pos = end + 1;
        }
    }

    static bool tryMatch(const Route& r, std::string_view path, HttpRequest* req) noexcept
    {
        if (!path.empty() && path.front() == '/') path.remove_prefix(1);
        while (!path.empty() && path.back() == '/') path.remove_suffix(1);

        // Capture into a scratch buffer first: writing straight into the
        // request would leave partial captures behind when a later segment
        // rejects the candidate.
        std::string_view names[HttpRequest::kMaxParams];
        std::string_view values[HttpRequest::kMaxParams];
        u32 captured = 0;

        usize idx = 0;
        usize pos = 0;

        if (path.empty())
            return r.segments.empty();

        while (pos <= path.size())
        {
            if (idx >= r.segments.size())
                return false;

            usize end = path.find('/', pos);
            if (end == std::string_view::npos) end = path.size();
            const std::string_view seg = path.substr(pos, end - pos);

            const std::string& pat = r.segments[idx];
            if (!pat.empty() && pat[0] == ':')
            {
                if (seg.empty() || captured >= HttpRequest::kMaxParams)
                    return false;
                names[captured] = std::string_view(pat).substr(1);
                values[captured] = seg;
                ++captured;
            }
            else if (seg != pat)
            {
                return false;
            }

            ++idx;
            pos = end + 1;
        }

        if (idx != r.segments.size())
            return false;

        if (req)
        {
            for (u32 i = 0; i < captured; ++i)
                req->setParam(names[i], values[i]);
        }
        return true;
    }

    std::vector<Route> _routes;
};

} // namespace warp

#endif // PATTERNROUTES_H
