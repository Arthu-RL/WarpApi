# Changelog

All notable changes to this project are documented in this file.

The format is loosely based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/); this
project follows [Semantic Versioning](https://semver.org/) once it reaches 1.0.0 — before that,
minor versions may include breaking changes.

## [0.1.0]

### Added

**Core architecture**
- Shared-nothing, per-core worker design: each thread owns its own `SO_REUSEPORT` listening
  socket, event loop, connection pool, and buffers — no locks and no shared mutable state on
  the request path.
- Two interchangeable, compile-time-selected I/O backends implementing identical protocol
  logic: `epoll` (portable, zero special privileges) and `io_uring` (Linux 5.11+, with a
  setup-flag ladder that prefers `IORING_SETUP_SINGLE_ISSUER` + `DEFER_TASKRUN`, then
  `COOP_TASKRUN`, then optional `SQPOLL`, falling back to a plain ring or a smaller one rather
  than failing a worker outright).
- Zero-copy `send_zc` sends on `io_uring` for responses above a configurable size threshold.
- CPU-topology-aware worker pinning: workers are assigned one per distinct physical core first
  (reading `/sys/devices/system/cpu/*/topology/`) before any SMT sibling is reused, correctly
  handling hybrid P-core/E-core parts as well as plain SMT.
- Optional `SO_ATTACH_REUSEPORT_CBPF` classic-BPF program steering new connections to the
  listener whose worker runs on the interrupting CPU, with silent, logged fallback to the
  kernel's default hash on kernels/containers that don't allow it.
- Lock-free, per-worker timer-wheel keep-alive reaping in O(1), with no cross-thread
  coordination.
- Startup-time raising of `RLIMIT_NOFILE` (and, on the `io_uring` build, `RLIMIT_MEMLOCK`) as
  far as the process's hard limit allows.

**HTTP**
- Allocation-free HTTP/1.1 request parsing: method, path, query, headers, and body are all
  `string_view`s into the connection's read buffer.
- SWAR (SIMD-within-a-register) method parsing and AVX2/SSE2-accelerated CRLF scanning, with a
  SWAR scalar fallback for short tails.
- A contiguous, growable per-connection `ByteBuffer` (compact-on-demand, never wraps) replacing
  a ring buffer, so a message can never straddle a discontinuity; buffers grow on demand and
  shrink back down once idle.
- Keep-alive, HTTP pipelining, and write-side backpressure (pipelined reads pause once queued
  output crosses a high-water mark, resuming as the socket drains).
- `HEAD` served transparently from the matching `GET` handler, response body suppressed.
- `Expect: 100-continue` support, with the interim response emitted before the body is awaited
  (not after), avoiding a deadlock against conforming clients.
- Duplicate/conflicting `Content-Length` headers rejected (request-smuggling defense);
  `Transfer-Encoding: chunked` explicitly rejected rather than mis-framed.
- Case-insensitive, length-bucketed header name matching (not matched by length alone, which
  previously conflated distinct same-length header names).
- Per-thread cached `Date` header and precomputed 200-OK response prelude, refreshed at most
  once per second.
- `405 Method Not Allowed` with a correct `Allow` header for a path that exists under a
  different method, instead of a misleading `404`.

**Routing**
- O(1) expected-time exact-path routing via a frozen, open-addressed hash table (64-bit
  multiply-xor mixed hash, ≤25% load factor), queried lock-free by every worker after startup.
- `:param` path segments as a fallback for the exact-match table, captured as zero-allocation
  `string_view`s into the read buffer (up to 8 per request); exact routes always win over a
  pattern that would also match, and segment-count mismatches never match.
- Declarative route registration via a `Router` object passed to plain configuration functions
  (`void configureXRoutes(Router&)`) — no base class, no self-registration; `group()` nests URL
  prefixes with seam normalization so adjacent slashes can't produce a silently unreachable
  route.

**Dependency injection**
- `ServiceContainer`: eager, ordered service construction with direct constructor-argument
  injection (`add<Repo>(get<Dependency>())`) — no separate factory-registration concept,
  reverse-order destruction, and RTTI-free type keys (release builds use `-fno-rtti`) via a
  per-type `static constexpr` address as identity, with `__PRETTY_FUNCTION__`-derived type
  names for diagnostics. Dependency cycles are inexpressible by construction.

**WebSocket (RFC 6455)**
- Full opening handshake (`Sec-WebSocket-Key`/`Accept`, version negotiation with a `426` +
  `Sec-WebSocket-Version` response on mismatch).
- Message fragmentation (multi-frame reassembly) with control frames (ping/pong/close)
  correctly interleaved mid-fragment.
- In-place, 8-byte-at-a-time client-frame unmasking.
- Proper closing handshake (code echoed back, socket closed only after the close frame is
  flushed) and strict validation of reserved bits, opcodes, close-code ranges, control-frame
  size limits, and minimum-length-encoding for extended payload lengths.
- Per-connection maximum message size enforcement (reassembled fragments included), closing
  with code `1009` on overflow.

**Configuration & operations**
- `config.json`-driven configuration: bind address/port, worker count (`0` = one per core),
  listen backlog, keep-alive timeout, body/request/response size caps, initial per-connection
  buffer sizes, CPU affinity, and `SO_REUSEPORT` CBPF steering — all validated at startup with
  descriptive errors rather than failing silently.
- Graceful shutdown on `SIGINT`/`SIGTERM`.

**Build**
- C++23 throughout; `-O3`, native-arch tuning (when supported), link-time optimization,
  `--gc-sections`, `-fno-semantic-interposition`, hidden visibility, and static
  `libstdc++`/`libgcc` linking in release builds.
- `USE_EPOLL` / `USE_IOURING` CMake options selecting the backend at configure time.

**Bundled example routes**
- `/`, `/plaintext`, `/json`, `/test`, `/apibenchmark`, `/health`, `/version`, and a
  `/ws/echo` WebSocket endpoint.
- `/diag/info`, `/diag/hits`, `/diag/echo/:id`, `/diag/echo/:id/part/:sub` — demonstrating
  injected-service routes and path parameters.
