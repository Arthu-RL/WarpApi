# WarpApi

WarpApi is a small, blazing-fast C++23 HTTP/1.1 + WebSocket server engineered for extreme
per-core throughput. It is a **shared-nothing, multithreaded** design: every worker thread
owns its own listening socket (via `SO_REUSEPORT`), its own event loop, its own connection
pool, and its own buffers — no locks, no shared mutable state, no cache-line bouncing between
cores.

Two interchangeable, compile-time-selected I/O backends are provided:

* **`epoll`** — the portable default. Battle-tested, works everywhere, zero special privileges required.
* **`io_uring`** — the maximum-throughput option on modern Linux (5.11+), with optional `SQPOLL`
  (kernel-side submission polling, no syscall on the hot path) and zero-copy sends for large
  payloads.

Both backends implement the exact same protocol logic (`Session`, `HttpRequest`/`HttpResponse`,
the WebSocket codec) — only the I/O multiplexing strategy differs.

---

## ✨ Key Features

* **Shared-nothing per-core architecture.** Each worker thread binds its own `SO_REUSEPORT`
  listener, pins itself to one CPU core, and never touches another thread's state. The kernel
  (optionally steered by an attached `SO_ATTACH_REUSEPORT_CBPF` program, see below) load-balances
  new connections across workers.
* **Pluggable event loops.** `epoll` (portable) or `io_uring` (max throughput), selected at
  CMake configure time.
* **Radix-tree routing.** Routes are matched with a compressed trie (`ink::InkixTree`), one per
  HTTP method, built once at startup and read-only afterward — safe to query concurrently from
  every worker with no locking.
* **Allocation-free request parsing.** Every header and the body are `string_view`s into the
  connection's read buffer; nothing is copied or heap-allocated to parse a request.
* **Full WebSocket support (RFC 6455).** Fragmented messages, ping/pong, a proper closing
  handshake, control frames interleaved mid-fragment, masking/unmasking done in place, and
  strict validation of reserved bits, opcodes, and close codes.
* **Zero-copy sends on io_uring.** Responses above a configurable threshold are sent with
  `send_zc`, handing pages to the NIC instead of copying them.
* **Self-tuning buffers.** Each connection's read/write buffers start small, grow on demand
  (never by wrapping — a straight contiguous grow-and-compact buffer, so a request can never
  straddle a discontinuity), and shrink back down once idle, so a burst of large requests
  doesn't permanently inflate memory for a million idle keep-alive connections.
* **Lock-free keep-alive reaping.** One timer wheel per worker thread evicts idle connections
  in O(1) without any cross-thread coordination.

---

## 🛠️ Prerequisites

* **OS:** Linux (5.11+ recommended if you want the `io_uring` backend).
* **Compiler:** GCC or Clang with **C++23** support (the `ink` library this project is built on
  requires `std::expected`, `std::source_location`, and C++23 `<sstream>` — this is a hard
  requirement, not a preference).
* **Build system:** CMake 3.16+.
* **Dependencies:** OpenSSL (for the WebSocket handshake's SHA-1), `liburing` (only if building
  the `io_uring` backend), and [`libink`](../libink) (logging, JSON, ring/timer/pool
  primitives, radix tree).

---

## 🏗️ Building WarpApi

**1. Generate the build files.** Pick exactly one backend:

```bash
# Recommended for maximum throughput on a modern kernel:
cmake -B build/release -DCMAKE_BUILD_TYPE=Release -DUSE_IOURING=ON -DUSE_EPOLL=OFF

# Portable / works everywhere, including inside most containers:
cmake -B build/release -DCMAKE_BUILD_TYPE=Release -DUSE_EPOLL=ON -DUSE_IOURING=OFF
```

`libink`'s headers and static library must be discoverable — set `LIBRARY_PATH` to the
prefix that contains `include/ink/` and `lib/libink.a` before configuring:

```bash
export LIBRARY_PATH=/usr/local/linux/release   # adjust to your ink install
```

**2. Compile:**

```bash
cmake --build build/release -j$(nproc)
```

Release builds are compiled with `-O3`, `-march=native` (when supported by the toolchain),
link-time optimization, and `--gc-sections`, and link `libstdc++`/`libgcc` statically for an
easily deployable binary.

### Building & running inside a container (e.g. this project's `vulkan-dev` dev container)

```bash
docker exec -w /path/to/WarpApi vulkan-dev bash -lc '
  export LIBRARY_PATH=/usr/local/linux/release
  cmake -B build/release -DCMAKE_BUILD_TYPE=Release -DUSE_EPOLL=ON -DUSE_IOURING=OFF
  cmake --build build/release -j$(nproc)
  ./build/release/WarpApi
'
```

> **`io_uring` inside Docker:** Docker's default seccomp profile blocks the `io_uring_setup` /
> `io_uring_enter` / `io_uring_register` syscalls entirely. To run the `io_uring` backend in a
> container you need `--security-opt seccomp=unconfined` (or a custom profile allowlisting
> those three syscalls). `SQPOLL` mode additionally needs `CAP_SYS_NICE` (`--cap-add=SYS_NICE`)
> and a `RLIMIT_MEMLOCK` high enough for every worker's ring (containers default to 8MB, which
> covers only a handful of workers — WarpApi raises its own limit as far as the container's hard
> cap allows, but that cap itself typically needs `--cap-add=SYS_RESOURCE` or an explicit
> `--ulimit memlock=...` to move). None of this is needed for `epoll`, and WarpApi degrades
> gracefully either way: any worker that can't stand up a ring falls back to a smaller one, and
> if it still can't get one, it closes its listener so the `SO_REUSEPORT` group excludes that
> shard instead of silently dropping the connections routed to it.

---

## ⚙️ Configuration (`config.json`)

WarpApi reads `config.json` from its working directory at startup.

```json
{
  "ip": "0.0.0.0",
  "port": 41385,
  "max_threads": 0,
  "backlog_size": 8192,
  "connection_timeout_ms": 60000,
  "max_body_size": 65536,
  "max_request_size": 131072,
  "max_response_size": 1048576,
  "read_buffer_size": 4096,
  "write_buffer_size": 4096,
  "cpu_affinity": true,
  "reuseport_cbpf": true
}
```

| Option                    | Meaning                                                                                                          |
|---------------------------|--------------------------------------------------------------------------------------------------------------------|
| `ip` / `port`             | Bind address and TCP port.                                                                                      |
| `max_threads`             | Worker (= listener) count. **`0` means "one per physical/logical core"** — the sweet spot for the shared-nothing design. Set explicitly to reserve cores for a co-located load generator or other process. |
| `backlog_size`            | `listen()` backlog **per worker's own socket** (each worker has its own `SO_REUSEPORT` listener). |
| `connection_timeout_ms`   | Idle keep-alive timeout before a connection is reaped by the timer wheel.                                       |
| `max_body_size`           | Largest request body / WebSocket message (after reassembly) accepted; anything larger is rejected (`413`) or closed (`1009`). |
| `max_request_size`        | Hard cap the per-connection **read** buffer may grow to. Must be `>= max_body_size`.                            |
| `max_response_size`       | Hard cap the per-connection **write** buffer may grow to.                                                        |
| `read_buffer_size` / `write_buffer_size` | Starting size of each per-connection buffer. They grow on demand up to their `max_*` cap and shrink back down once idle — this is the steady-state memory cost per idle connection. |
| `cpu_affinity`            | Pin each worker thread to one CPU core (`true` for the shared-nothing design; disable if co-locating with other CPU-heavy processes). |
| `reuseport_cbpf`          | Attach a `SO_ATTACH_REUSEPORT_CBPF` classic-BPF program that steers each new connection to the listener whose worker runs on the CPU that received the interrupt, instead of the kernel's default 4-tuple hash. Falls back silently (with a log line) if the running kernel/container doesn't allow it. |

---

## 🚀 Running & scaling to very high request rates

WarpApi's own `main()` raises `RLIMIT_NOFILE` (and, on the `io_uring` build, `RLIMIT_MEMLOCK`)
as far as the process's hard limit allows at startup — you do not need to `ulimit` it yourself
in the common case. If you still want to raise the *hard* limit (which the process cannot do
for itself without `CAP_SYS_RESOURCE`), do it in the shell that launches WarpApi:

```bash
ulimit -n 1000000
./build/release/WarpApi
```

### Scaling model

Set `max_threads` to the number of cores you want WarpApi to use (or leave it `0` for "all of
them"). Because every worker is fully independent — its own listener, epoll/io_uring instance,
session pool, and timer wheel — throughput scales close to linearly with core count up to
however many cores the machine actually has: there is no shared lock or shared data structure
on the request path to contend on. The `EndpointManager`'s radix trees are the one thing every
worker reads, and they are built once at startup and never mutated afterward, so concurrent
reads from all workers need no synchronization at all.

To sustain very high aggregate request rates (the "billions of requests" regime), that means:
run one instance with `max_threads` set to your full core count (or scale further with multiple
instances/machines behind a load balancer — WarpApi does not share any state that would prevent
that). A single modern 12-24 core box easily sustains multiple million requests/second for small
payloads (see benchmarks below); at that rate a single such box serves on the order of a
hundred billion requests per day.

### 🏎️ Benchmarking notes

If you load-test on the same machine you're serving from (e.g. with `wrk`), the load generator
and the API compete for the same cores, which understates real throughput. For accurate
same-box numbers:

**Give the server and the load generator disjoint *physical* cores — not just disjoint
logical CPU numbers.** On any SMT or hybrid (P-core/E-core) machine those are different
things, and getting it wrong is the single most common way to produce a misleading result.
Check the topology first:

```bash
lscpu -p=CPU,CORE | grep -v '^#'      # logical CPU -> physical core mapping
```

On the machine these numbers came from, that prints `0:0 1:0 2:1 3:1 … 14:7 15:7` followed by
`16:8 17:9 … 23:15` — i.e. 8 hyperthreaded P-cores (logical 0-15) plus 8 single-threaded
E-cores (logical 16-23). Logical CPUs 0 and 1 are the *same physical core*. WarpApi's
`cpu_affinity` already accounts for this and spreads workers one-per-physical-core before it
reuses any SMT sibling (it logs the plan at startup), so you only need to keep the load
generator off the cores the server is using:

```bash
# Server: max_threads: 8, cpu_affinity: true -> pinned to logical 0,2,4,6,8,10,12,14
#         (8 distinct physical P-cores; the startup log prints the exact plan)
# Client: the leftover SMT siblings + the E-cores
taskset -c 1,3,5,7,9,11,13,15,16-23 wrk -t12 -c1000 -d15s --latency \
    http://127.0.0.1:41385/plaintext
```

Use enough concurrent connections (several hundred to a few thousand) that `SO_REUSEPORT`
hashes traffic evenly across every worker. Raise `ulimit -n` for the load generator process
too — at a few thousand connections the *client* hits the descriptor limit first, and the
resulting `connect` errors look exactly like a server fault when they are not.

Finally: check that the load generator is not itself the ceiling. If two very different server
configurations report near-identical throughput, you are almost certainly measuring `wrk`.

### Results

All figures below are real `wrk` runs from this project's test sessions, repeated to check
they reproduce — not projections. The test host is a 16-physical-core hybrid desktop
(8 SMT P-cores + 8 E-cores, 24 logical CPUs) that was *also running a desktop session, several
containers and ~8 GB of swap traffic*, so treat the absolute numbers as a lower bound with
meaningful run-to-run variance, and pay more attention to the controlled comparisons.

**Peak observed throughput, 12 workers, `/plaintext`, container, client on separate physical
cores:**

```
$ taskset -c 12-23 wrk -t12 -c400 -d15s --latency http://127.0.0.1:41385/plaintext   # epoll
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    92.43us  154.09us  15.89ms   99.55%
    Req/Sec   204.84k    25.44k  254.29k    66.72%
  Latency Distribution
     50%   83.00us   75%   92.00us   90%  119.00us   99%  181.00us
  36930408 requests in 15.10s, 5.50GB read
Requests/sec: 2445745.87
```

Re-running the identical configuration later, with the host under heavier load, gave
**2.30M req/s** — the same binary, ~6% lower, purely from machine conditions. A repeated
6-sample A/B on that same box measured **2.402M ± 0.074M** before the optimization pass and
**2.408M ± 0.015M** after: throughput unchanged (the workload is syscall-bound, see below),
but **run-to-run variance dropped ~5x**, which is the more useful property.

**CPU pinning is worth more than every micro-optimization combined.** With 4 workers, holding
everything else constant and only changing how they are pinned:

| 4 workers pinned to…                          | Requests/sec (3 runs)          |
|-----------------------------------------------|--------------------------------|
| logical CPUs 0-3 = **2 physical cores** (naive `i % ncpu`) | 1.016M, 1.012M, 1.021M |
| logical CPUs 0,2,4,6 = **4 physical cores** (topology-aware) | 1.374M, 1.429M, 1.430M |

That is a reproducible **+39%** for the same thread count, and it is why `cpu_affinity`
enumerates `/sys/devices/system/cpu/*/topology` and assigns one worker per physical core
before reusing any SMT sibling. The naive mapping silently parked two workers on each of half
as many cores while the rest of the machine idled.

**epoll vs io_uring, both host-native, same machine, same client:**

```
=== epoll ===                          === io_uring (DEFER_TASKRUN|SINGLE_ISSUER) ===
  Latency   117.94us avg               Latency   123.59us avg
     50%   96.00us                        50%   91.00us
     75%  120.00us                        75%  115.00us
     90%  203.00us                        90%  288.00us
     99%  288.00us                        99%  412.00us
Requests/sec: 2115900.61               Requests/sec: 2057991.14
```

Throughput is a wash; io_uring wins slightly at the median and loses at the tail. But note
what `DEFER_TASKRUN` alone is worth — the same io_uring backend, before and after the ring
setup flags were added:

| io_uring configuration                    | Req/sec | p50    | p99        |
|-------------------------------------------|---------|--------|------------|
| plain ring (no modern setup flags)        | 2.07M   | 147µs  | **2.54ms** |
| `DEFER_TASKRUN` + `SINGLE_ISSUER`         | 2.10M   | 98µs   | **316µs**  |

Same throughput, but an **8x better 99th percentile**. That is the real reason to reach for
io_uring here.

Under pipelining (16 requests per write, which amortizes the syscalls away) both backends
reach **~16.8M req/s**, at which point `wrk` itself is a significant part of what is being
measured.

At 5000 `wrk` connections (still `epoll`, client-side `ulimit -n` raised to 65536 first —
without that the *client* fails to open sockets and it looks like a server problem when it
isn't): **1.36M req/s with zero socket errors**, `wrk`'s own 12 threads juggling 5000
connections each becoming the bottleneck rather than the server. Server fd count and RSS were
checked before/after every run above (`/proc/<pid>/fd`, `VmRSS`) and never grew — no leaked
connections or memory under sustained load.

**Is >2M req/s for one process actually real, or an artifact?** It's real for what it's
measuring, and worth being precise about what that is:

* `/plaintext` returns a fixed 13-byte body — no JSON serialization, no allocation, a single
  memcpy-like append into a pre-sized buffer. This isolates the framework's own overhead rather
  than an application's.
* It's loopback (client and server on the same host), so there's no physical NIC, no real-world
  RTT, and no packet loss to amortize — `/proc/net` shows this as pure kernel socket-buffer
  copies. Real network conditions will lower absolute throughput; the *relative* cost of the
  framework itself (parsing, routing, response assembly) is what this benchmark isolates.
  Server and client cores are still disjoint (`taskset`-pinned), so this isn't the load
  generator starving itself either.
* Each of the 12 workers is a fully independent `epoll` loop with its own listening socket
  (`SO_REUSEPORT`), so 2.45M req/s here is ~204k req/s **per core** (matches the per-thread
  `Req/Sec` wrk reports directly) — a plausible, unglamorous number for a non-blocking
  event loop doing a few hundred bytes of parsing and no syscalls beyond `recv`/`send` per
  request, not a claim about magically breaking single-core limits.
* No TLS termination is involved. Terminating TLS would cost real CPU per connection and bring
  this down substantially — if your deployment terminates TLS in-process, benchmark that
  configuration specifically rather than assuming these numbers carry over.

**Where the time actually goes.** ~204k req/s per core is roughly 4.9µs, or ~14,000 cycles,
per request — far more than parsing a 40-byte request line can account for. Non-pipelined
HTTP costs one `recv` and one `send` syscall per request, and in a container with speculative
-execution mitigations enabled those dominate everything else. That is why the userspace
micro-optimizations in this codebase (SWAR method matching, AVX2 CRLF scanning, the frozen
route index, eliminating ~650 bytes of per-request memset) measure as *noise* here, while CPU
pinning — which changes how many physical cores actually run the syscalls — is worth 39%.
Optimize the syscall count first; that is what `io_uring` with `SQPOLL` or multishot receive
is for.

> **On `SQPOLL`:** none of the io_uring figures above use it. WarpApi tries it as one rung of
> its setup ladder, but it needs `CAP_SYS_NICE` *and* enough `RLIMIT_MEMLOCK` to pin every
> worker's rings, and the test machine's 8 MB hard memlock cap (unraisable without
> `CAP_SYS_RESOURCE`) rules it out for a realistic worker count. `SQPOLL` is the one change
> that removes the per-request submission syscall entirely, so on a host that grants those
> privileges it is the most likely source of a real step change — benchmark it there rather
> than assuming these numbers transfer.

---

## 🌐 Endpoints registered by the bundled `GeneralServices` example

| Method | Path            | Description                                             |
|--------|-----------------|----------------------------------------------------------|
| GET    | `/`             | Library metadata (`ink::EnhancedJsonUtils::meta_info()`) |
| GET    | `/plaintext`    | Constant `"Hello, World!"` body — cheapest possible route, useful for measuring the framework itself rather than JSON serialization. |
| GET    | `/json`         | `{"message":"Hello, World!"}`                            |
| GET    | `/test`         | Echoes query parameters and JSON body fields back as JSON. |
| POST   | `/apibenchmark` | Echoes the request body back verbatim.                   |
| GET    | `/health`       | `{"status":"ok"}`                                         |
| GET    | `/version`      | Server version info.                                      |
| WS     | `/ws/echo`      | WebSocket echo: replies with the same opcode (text/binary) and payload it received. |

---

## 🔌 Writing your own endpoints

```cpp
class MyService : public BaseService {
public:
    MyService() { registerAllEndpoints(); }

    void registerAllEndpoints() override {
        registerEndpoint("/hello", Method::GET,
            [](const HttpRequest& req, HttpResponse& res) {
                res.setBody("Hello!");
            });

        registerWebSocketEndpoint("/ws/chat", {
            /* onOpen    */ [](WebSocketContext& ctx) { ctx.sendText("welcome"); },
            /* onMessage */ [](WebSocketContext& ctx, std::string_view msg, bool isBinary) {
                isBinary ? ctx.sendBinary(msg) : ctx.sendText(msg);
            },
            /* onClose   */ [](WebSocketContext&) {}
        });
    }
};
```

Register your service(s) before calling `server.start()` in `main()` — the route trees are
built once at startup and are read-only for the lifetime of the process, which is what lets
every worker thread query them without any locking.

---

## 🧪 Testing

There's no bundled test binary; the server was verified with a black-box functional suite
(raw-socket HTTP + RFC 6455 WebSocket conformance — pipelining, keep-alive, malformed input,
Content-Length smuggling, buffer-growth edge cases, fragmentation, control frames, close
handshake, protocol-violation handling, etc.) and load-tested with `wrk` and a concurrent
WebSocket echo client, on both backends. See the project's development history for the exact
scenarios covered.

---

## 📄 License

See [LICENSE](LICENSE).
