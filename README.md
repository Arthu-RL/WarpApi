# WarpApi

WarpApi is a blazing-fast, C++ multithreaded API framework engineered for extreme performance. Built from the ground up for modern hardware, it leverages the power and efficiency of HTTP/2 with full support for multiplexing, allowing you to handle massive concurrent workloads with minimal overhead.

Designed with a "Shared-Nothing" per-core architecture, WarpApi eliminates lock contention and context-switching bottlenecks by offering two distinct, highly optimized asynchronous event-loop backends: standard **`epoll`** and bleeding-edge **`io_uring`**.

## ✨ Key Features
* **True HTTP/2 Multiplexing:** Handle multiple simultaneous requests over a single TCP connection.
* **Pluggable Event Loops:** Choose between battle-tested `epoll` or extreme-throughput `io_uring` at compile time.
* **Shared-Nothing Multithreading:** Each thread manages its own memory pools, buffers, and event loops, preventing cache-line bouncing.
* **Zero-Copy Ready:** Optimized memory pipelines for both parsing and network transport.

---

## 🛠️ Prerequisites
Before building WarpApi, ensure your system meets the following requirements:
* **OS:** Modern Linux Distribution (Kernel 5.11+ recommended for advanced `io_uring` features).
* **Compiler:** GCC or Clang with C++20 support.
* **Build System:** CMake 3.15 or higher.
* **Dependencies:** `liburing` (If building with the `io_uring` backend).

---

## 🏗️ Building WarpApi

WarpApi uses CMake for its build system. You can compile the server to use either `epoll` (highly compatible) or `io_uring` (maximum performance).

**1. Generate the Build Files**
Choose your preferred backend by passing the corresponding flag to CMake.

*To build with `io_uring` (Recommended for max RPS):*
```bash
cmake -B build/release -DCMAKE_BUILD_TYPE=Release -DUSE_IOURING=ON
```

*To build with `epoll`:*
```bash
cmake -B build/release -DCMAKE_BUILD_TYPE=Release -DUSE_EPOLL=ON
```

**2. Compile the Project**
```bash
/usr/local/bin/cmake --build build/release --target all -j$(nproc)
```

---

## ⚙️ Configuration (`config.json`)

WarpApi is configured dynamically via a `config.json` file located in the working directory alongside the executable. 

Create a `config.json` file with the following structure:

```json
{
    "port": 41385,
    "max_threads": 12,
    "backlog_size": 10000,
    "connection_timeout_ms": 60000,
    "max_request_size": 8192,
    "max_response_size": 8192
}
```

### Configuration Options
* `port`: The TCP port the server will bind to.
* `max_threads`: Number of worker threads to spawn. For optimal performance, set this to the number of physical CPU cores you want to utilize.
* `backlog_size`: The maximum length of the queue of pending connections for the socket.
* `connection_timeout_ms`: Keep-Alive timeout before the server drops idle connections.
* `max_request_size` / `max_response_size`: Pre-allocated RingBuffer sizes per session (in bytes).

---

## 🚀 Running & Performance Tuning

To achieve millions of requests per second, WarpApi requires the operating system to allow a massive number of concurrent file descriptors. 

**1. Raise the OS Limits**
Before starting the server, increase the open file limit in your terminal session:
```bash
ulimit -n 1000000
```

**2. Start the Server**
```bash
./build/release/WarpApi
```

### 🏎️ Benchmarking Tips
If you are load testing WarpApi locally (e.g., using `wrk`, `oha`, or `bombardier`), the load tester and the API will fight for the same CPU cores, artificially lowering your results. 

For accurate benchmarking on `localhost`:
1.  **Isolate CPU Cores:** Use `taskset` to bind the server to half your cores, and the load tester to the other half.
2.  **Force Hash Distribution:** If testing without HTTP pipelining, ensure you use a high number of concurrent connections (e.g., `-c 15000`) so the Linux `SO_REUSEPORT` hashes connections evenly across all WarpApi threads.