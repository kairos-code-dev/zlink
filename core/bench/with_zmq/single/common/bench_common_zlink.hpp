#ifndef BENCH_COMMON_HPP
#define BENCH_COMMON_HPP

#include <chrono>
#include <algorithm>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <iostream>
#include <iomanip>
#include <thread>
#include <set>
#include <zlink.h>

// --- Configuration ---
static const std::vector<size_t> MSG_SIZES = {64, 256, 1024, 65536, 131072, 262144};
static const std::vector<std::string> TRANSPORTS = {"tcp", "inproc", "ipc"};
static const size_t MAX_SOCKET_STRING = 256;
static const int SETTLE_TIME_MS = 300;

// --- Stopwatch ---
class stopwatch_t {
public:
    void start() { _start = std::chrono::steady_clock::now(); }
    double elapsed_ms() const {
        auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(end - _start).count();
    }

private:
    std::chrono::steady_clock::time_point _start;
};

inline void print_result(const std::string &lib_type,
                         const std::string &pattern,
                         const std::string &transport,
                         size_t size,
                         double throughput,
                         double latency)
{
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",throughput," << std::fixed
              << std::setprecision(2) << throughput << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",latency," << std::fixed
              << std::setprecision(2) << latency << std::endl;
}

inline bool bench_debug_enabled()
{
    static const bool enabled = std::getenv("BENCH_DEBUG") != NULL;
    return enabled;
}

inline int bench_io_threads()
{
    static int threads = -1;
    if (threads >= 0)
        return threads;

    const char *env = std::getenv("BENCH_IO_THREADS");
    if (!env || !*env) {
        threads = 0;
        return threads;
    }

    const int val = std::atoi(env);
    threads = val > 0 ? val : 0;
    return threads;
}

inline int bench_stream_server_io_threads()
{
    static int threads = -1;
    if (threads >= 0)
        return threads;

    const char *env = std::getenv("BENCH_STREAM_SERVER_IO_THREADS");
    if (!env || !*env)
        env = std::getenv("BENCH_IO_THREADS");

    if (!env || !*env) {
        threads = 4;
        return threads;
    }

    const int val = std::atoi(env);
    threads = val > 0 ? val : 4;
    return threads;
}

inline int bench_stream_client_threads(size_t clients_)
{
    const int fallback = clients_ >= 10000 ? 4 : 1;
    const char *env = std::getenv("BENCH_STREAM_CLIENT_THREADS");
    if (!env || !*env)
        env = std::getenv("BENCH_MULTI_STREAM_SEND_WORKERS");

    int threads = fallback;
    if (env && *env) {
        const int parsed = std::atoi(env);
        if (parsed > 0)
            threads = parsed;
    }

    if (threads < 1)
        threads = 1;
    if (threads > 4)
        threads = 4;
    if (clients_ > 0 && threads > static_cast<int>(clients_))
        threads = static_cast<int>(clients_);
    return threads;
}

inline void apply_io_threads(void *ctx_)
{
    const int threads = bench_io_threads();
    if (threads <= 0)
        return;

    const int rc = zlink_ctx_set(ctx_, ZLINK_IO_THREADS, threads);
    if (rc != 0 && bench_debug_enabled()) {
        std::cerr << "zlink_ctx_set(ZLINK_IO_THREADS) failed: "
                  << zlink_strerror(zlink_errno()) << std::endl;
    }
}

inline void apply_stream_server_io_threads(void *ctx_)
{
    const int threads = bench_stream_server_io_threads();
    if (threads <= 0)
        return;

    const int rc = zlink_ctx_set(ctx_, ZLINK_IO_THREADS, threads);
    if (rc != 0 && std::getenv("BENCH_DEBUG") != NULL) {
        std::cerr << "zlink_ctx_set(ZLINK_IO_THREADS, stream) failed: "
                  << zlink_strerror(zlink_errno()) << std::endl;
    }
}

class ctx_guard_t {
public:
    ctx_guard_t() : _ctx(zlink_ctx_new())
    {
        if (_ctx)
            apply_io_threads(_ctx);
    }

    ~ctx_guard_t()
    {
        if (_ctx)
            zlink_ctx_term(_ctx);
    }

    void *get() const { return _ctx; }
    bool valid() const { return _ctx != NULL; }

private:
    ctx_guard_t(const ctx_guard_t &);
    ctx_guard_t &operator=(const ctx_guard_t &);

    void *_ctx;
};

class socket_guard_t {
public:
    socket_guard_t() : _socket(NULL) {}

    socket_guard_t(void *ctx_, int type_) : _socket(zlink_socket(ctx_, type_)) {}

    ~socket_guard_t()
    {
        if (_socket)
            zlink_close(_socket);
    }

    void *get() const { return _socket; }
    bool valid() const { return _socket != NULL; }
    operator void *() const { return _socket; }

private:
    socket_guard_t(const socket_guard_t &);
    socket_guard_t &operator=(const socket_guard_t &);

    void *_socket;
};

struct connect_monitor_t {
    void *owner;
    void *monitor;
    connect_monitor_t() : owner(NULL), monitor(NULL) {}
};

inline int bench_monitor_hwm()
{
    static int monitor_hwm = -1;
    if (monitor_hwm >= 0)
        return monitor_hwm;

    const char *env = std::getenv("BENCH_MULTI_MONITOR_HWM");
    if (!env || !*env) {
        monitor_hwm = 200000;
        return monitor_hwm;
    }

    errno = 0;
    char *end = NULL;
    const long parsed = std::strtol(env, &end, 10);
    if (errno != 0 || end == env || parsed <= 0) {
        monitor_hwm = 0;
        return monitor_hwm;
    }

    monitor_hwm = static_cast<int>(parsed);
    return monitor_hwm;
}

inline bool open_connect_monitor(void *socket_, connect_monitor_t &out_)
{
    int events = ZLINK_EVENT_CONNECTION_READY;
#ifdef ZLINK_EVENT_CONNECTED
    events |= ZLINK_EVENT_CONNECTED;
#endif
#ifdef ZLINK_EVENT_ACCEPTED
    events |= ZLINK_EVENT_ACCEPTED;
#endif
    void *monitor = zlink_socket_monitor_open(socket_, events);
    if (!monitor)
        return false;

    const int linger_ms = 0;
    zlink_setsockopt(monitor, ZLINK_LINGER, &linger_ms, sizeof(linger_ms));
    const int monitor_hwm = bench_monitor_hwm();
    if (monitor_hwm > 0) {
        zlink_setsockopt(
          monitor, ZLINK_RCVHWM, &monitor_hwm, sizeof(monitor_hwm));
        zlink_setsockopt(
          monitor, ZLINK_SNDHWM, &monitor_hwm, sizeof(monitor_hwm));
    }

    out_.owner = socket_;
    out_.monitor = monitor;
    return true;
}

inline int poll_connect_ready_count(connect_monitor_t &monitor_)
{
    if (!monitor_.monitor)
        return 0;

    int ready = 0;
    for (;;) {
        zlink_monitor_event_t ev;
        if (zlink_monitor_recv(monitor_.monitor, &ev, ZLINK_DONTWAIT) != 0)
            break;
        if (ev.event == ZLINK_EVENT_CONNECTION_READY
#ifdef ZLINK_EVENT_CONNECTED
            || ev.event == ZLINK_EVENT_CONNECTED
#endif
#ifdef ZLINK_EVENT_ACCEPTED
            || ev.event == ZLINK_EVENT_ACCEPTED
#endif
        )
            ++ready;
    }
    return ready;
}

inline bool wait_connect_ready_count(connect_monitor_t &monitor_,
                                     size_t expected_ready_,
                                     int timeout_ms_)
{
    if (expected_ready_ == 0)
        return true;

    size_t ready = 0;
    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(std::max(0, timeout_ms_));
    while (std::chrono::steady_clock::now() < deadline && ready < expected_ready_) {
        ready += static_cast<size_t>(poll_connect_ready_count(monitor_));
        if (ready >= expected_ready_)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return ready >= expected_ready_;
}

inline void close_connect_monitor(connect_monitor_t &monitor_)
{
    if (monitor_.owner)
        zlink_socket_monitor(monitor_.owner, NULL, 0);
    if (monitor_.monitor)
        zlink_close(monitor_.monitor);
    monitor_.owner = NULL;
    monitor_.monitor = NULL;
}

inline bool set_sockopt_int(void *socket_, int option_, int value_,
                            const char *name_)
{
    const int rc = zlink_setsockopt(socket_, option_, &value_, sizeof(value_));
    if (rc != 0 && bench_debug_enabled()) {
        std::cerr << "setsockopt(" << name_ << ") failed: "
                  << zlink_strerror(zlink_errno()) << std::endl;
    }

    if (bench_debug_enabled()) {
        int out = 0;
        size_t out_size = sizeof(out);
        const int grc = zlink_getsockopt(socket_, option_, &out, &out_size);
        if (grc == 0) {
            std::cerr << "setsockopt(" << name_ << ") = " << out << std::endl;
        }
    }

    return rc == 0;
}

inline void apply_benchmark_hwm(void *socket_, int inflight_)
{
    if (inflight_ <= 0)
        return;

    set_sockopt_int(socket_, ZLINK_SNDHWM, inflight_, "ZLINK_SNDHWM");
    set_sockopt_int(socket_, ZLINK_RCVHWM, inflight_, "ZLINK_RCVHWM");
}

inline void apply_debug_timeouts(void *socket_, const std::string &transport)
{
    if (!bench_debug_enabled())
        return;

    if (transport == "tcp" || transport == "ws") {
        const int timeout_ms = 2000;
        set_sockopt_int(socket_, ZLINK_SNDTIMEO, timeout_ms, "ZLINK_SNDTIMEO");
        set_sockopt_int(socket_, ZLINK_RCVTIMEO, timeout_ms, "ZLINK_RCVTIMEO");
    }
}

inline std::string make_endpoint(const std::string &transport,
                                 const std::string &id)
{
    if (transport == "inproc")
        return "inproc://" + id;
    if (transport == "ipc")
        return "ipc://*";
    if (transport == "ws")
        return "ws://127.0.0.1:*";
    return "tcp://127.0.0.1:*";
}

inline std::string bind_and_resolve_endpoint(void *socket_,
                                             const std::string &transport,
                                             const std::string &id)
{
    std::string endpoint = make_endpoint(transport, id);
    if (zlink_bind(socket_, endpoint.c_str()) != 0) {
        std::cerr << "bind failed for " << endpoint << ": "
                  << zlink_strerror(zlink_errno()) << std::endl;
        return std::string();
    }

    if (transport != "inproc") {
        char last_endpoint[MAX_SOCKET_STRING] = "";
        size_t size = sizeof(last_endpoint);
        if (zlink_getsockopt(socket_, ZLINK_LAST_ENDPOINT, last_endpoint, &size)
            != 0) {
            std::cerr << "getsockopt(ZLINK_LAST_ENDPOINT) failed: "
                      << zlink_strerror(zlink_errno()) << std::endl;
            return std::string();
        }

        endpoint.assign(last_endpoint);
        if (transport == "tcp" || transport == "ws") {
            const std::string tcp_any = "://0.0.0.0:";
            const std::string tcp_ipv6_any = "://[::]:";
            size_t pos = endpoint.find(tcp_any);
            if (pos != std::string::npos) {
                endpoint.replace(pos, tcp_any.size(), "://127.0.0.1:");
            } else {
                pos = endpoint.find(tcp_ipv6_any);
                if (pos != std::string::npos)
                    endpoint.replace(pos, tcp_ipv6_any.size(), "://127.0.0.1:");
            }
        }

        if (bench_debug_enabled()) {
            std::cerr << "Resolved endpoint (" << transport
                      << "): " << endpoint << std::endl;
        }
    }

    return endpoint;
}

inline bool transport_available(const std::string &transport)
{
    if (transport == "ipc")
        return zlink_has("ipc") != 0;
    return true;
}

inline void settle()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(SETTLE_TIME_MS));
}

inline bool connect_checked(void *socket_, const std::string &endpoint)
{
    if (zlink_connect(socket_, endpoint.c_str()) != 0) {
        std::cerr << "connect failed for " << endpoint << ": "
                  << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }

    if (bench_debug_enabled())
        std::cerr << "Connected to " << endpoint << std::endl;
    return true;
}

inline bool setup_connected_pair(void *bind_socket_,
                                 void *connect_socket_,
                                 const std::string &transport_,
                                 const std::string &id_)
{
    std::string endpoint = bind_and_resolve_endpoint(bind_socket_, transport_, id_);
    if (endpoint.empty())
        return false;
    if (!connect_checked(connect_socket_, endpoint))
        return false;

    apply_debug_timeouts(bind_socket_, transport_);
    apply_debug_timeouts(connect_socket_, transport_);
    settle();
    return true;
}

template <typename StepFn>
inline void repeat_n(int count_, StepFn step_)
{
    for (int i = 0; i < count_; ++i)
        step_();
}

template <typename RoundTripFn>
inline double measure_roundtrip_latency_us(int roundtrip_count_,
                                           RoundTripFn roundtrip_)
{
    stopwatch_t sw;
    sw.start();
    repeat_n(roundtrip_count_, roundtrip_);
    return (sw.elapsed_ms() * 1000.0) / (roundtrip_count_ * 2);
}

template <typename SendOneFn, typename RecvOneFn>
inline double measure_throughput_msgs_per_sec(int msg_count_,
                                              SendOneFn send_one_,
                                              RecvOneFn recv_one_)
{
    std::thread receiver([&]() { repeat_n(msg_count_, recv_one_); });
    stopwatch_t sw;
    sw.start();
    repeat_n(msg_count_, send_one_);
    receiver.join();

    const double elapsed_ms = sw.elapsed_ms();
    return elapsed_ms > 0 ? (double)msg_count_ / (elapsed_ms / 1000.0) : 0.0;
}

inline int resolve_msg_count(size_t size)
{
    int count = (size <= 1024) ? 200000 : 20000;
    if (const char *env = std::getenv("BENCH_MSG_COUNT")) {
        errno = 0;
        const long override = std::strtol(env, NULL, 10);
        if (errno == 0 && override > 0)
            count = static_cast<int>(override);
    }
    return count;
}

inline int resolve_bench_count(const char *env_name, int default_value)
{
    if (const char *env = std::getenv(env_name)) {
        errno = 0;
        const long override = std::strtol(env, NULL, 10);
        if (errno == 0 && override > 0)
            return static_cast<int>(override);
    }
    return default_value;
}

template <typename RunFn>
inline int run_standard_bench_main(int argc_, char **argv_, RunFn run_)
{
    if (argc_ < 4)
        return 1;
    std::string lib_name = argv_[1];
    std::string transport = argv_[2];
    size_t size = std::stoul(argv_[3]);
    int count = resolve_msg_count(size);
    run_(transport, size, count, lib_name);
    return 0;
}

#endif
