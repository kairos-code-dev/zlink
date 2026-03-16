#ifndef PERF_RUNTIME_COMMON_HPP
#define PERF_RUNTIME_COMMON_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <thread>
#include <fstream>
#include <climits>
#include <map>
#include <mutex>
#include <new>
#include <zlink.h>

#include "../../../src/core/recv_internal.hpp"
#include "../../../src/core/monitor_dispatch_internal.hpp"

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <dlfcn.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

// --- TLS Socket Options ---
#ifndef ZLINK_TLS_CERT
#define ZLINK_TLS_CERT 95
#endif
#ifndef ZLINK_TLS_KEY
#define ZLINK_TLS_KEY 96
#endif
#ifndef ZLINK_TLS_CA
#define ZLINK_TLS_CA 97
#endif
#ifndef ZLINK_TLS_HOSTNAME
#define ZLINK_TLS_HOSTNAME 100
#endif

#if defined(_WIN32)
typedef unsigned int zlink_fd_t;
#else
typedef int zlink_fd_t;
#endif

#ifndef ZLINK_POLLIN
#define ZLINK_POLLIN 1
#endif
#ifndef ZLINK_POLLOUT
#define ZLINK_POLLOUT 2
#endif
#ifndef ZLINK_POLLERR
#define ZLINK_POLLERR 4
#endif
#ifndef ZLINK_POLLPRI
#define ZLINK_POLLPRI 8
#endif

typedef struct zlink_pollitem_t
{
    void *socket;
    zlink_fd_t fd;
    short events;
    short revents;
} zlink_pollitem_t;

typedef std::chrono::steady_clock steady_clock_t;
typedef std::chrono::milliseconds milliseconds_t;
typedef std::chrono::seconds seconds_t;
typedef std::chrono::nanoseconds nanoseconds_t;
typedef std::chrono::duration<double> floating_seconds_t;

inline int perf_socket_poll(zlink_pollitem_t *items_, int nitems_, long timeout_)
{
    if (nitems_ < 0) {
        errno = EINVAL;
        return -1;
    }

    if (nitems_ == 0 || !items_) {
        if (timeout_ > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout_));
        return 0;
    }

    const steady_clock_t::time_point start = steady_clock_t::now();
    while (true) {
        int ready = 0;
        for (int i = 0; i < nitems_; ++i) {
            items_[i].revents = 0;
            if (!items_[i].socket) {
                if (items_[i].fd != 0) {
                    errno = EINVAL;
                    return -1;
                }
                continue;
            }

            int events = 0;
            size_t events_len = sizeof(events);
            if (zlink_getsockopt(items_[i].socket, ZLINK_EVENTS, &events,
                                 &events_len)
                != 0) {
                return -1;
            }

            if ((items_[i].events & ZLINK_POLLIN) != 0
                && (events & ZLINK_POLLIN) != 0)
                items_[i].revents |= ZLINK_POLLIN;
            if ((items_[i].events & ZLINK_POLLOUT) != 0
                && (events & ZLINK_POLLOUT) != 0)
                items_[i].revents |= ZLINK_POLLOUT;

            if (items_[i].revents != 0)
                ++ready;
        }

        if (ready > 0 || timeout_ == 0)
            return ready;

        if (timeout_ > 0) {
            const long elapsed_ms = std::chrono::duration_cast<milliseconds_t>(
                                      steady_clock_t::now() - start)
                                      .count();
            if (elapsed_ms >= timeout_)
                return 0;
        }

        std::this_thread::sleep_for(milliseconds_t(1));
    }
}

inline steady_clock_t::duration to_clock_duration (double seconds)
{
    return std::chrono::duration_cast<steady_clock_t::duration> (
      floating_seconds_t (seconds));
}

inline long remaining_milliseconds (const steady_clock_t::time_point &deadline,
                                    const steady_clock_t::time_point &now)
{
    return std::chrono::duration_cast<milliseconds_t> (deadline - now).count ();
}

// --- Configuration ---
static const std::vector<size_t> MSG_SIZES = {64, 256, 1024, 65536, 131072, 262144};
static const std::vector<std::string> TRANSPORTS = {"tcp", "inproc", "ipc"};
static const std::vector<std::string> STREAM_TRANSPORTS = {"tcp", "tls", "ws", "wss"};
static const size_t MAX_SOCKET_STRING = 256;
static const int SETTLE_TIME_MS = 300;

// --- Stopwatch ---
class stopwatch_t {
public:
    void start() { _start = steady_clock_t::now(); }
    double elapsed_ms() const {
        auto end = steady_clock_t::now();
        return std::chrono::duration<double, std::milli>(end - _start).count();
    }
private:
    steady_clock_t::time_point _start;
};

inline int parse_positive_env(const char *name_, int default_value_)
{
    if (!name_)
        return default_value_;

    const char *env = std::getenv(name_);
    if (!env || !*env)
        return default_value_;

    errno = 0;
    char *end = NULL;
    const long parsed = std::strtol(env, &end, 10);
    if (errno != 0 || end == env || parsed <= 0)
        return default_value_;

    if (parsed > INT_MAX)
        return INT_MAX;
    return static_cast<int>(parsed);
}

inline size_t resolve_latency_sample_cap()
{
    const int cap =
      parse_positive_env("PERF_LATENCY_SAMPLE_CAP", 200000);
    return cap > 0 ? static_cast<size_t>(cap) : static_cast<size_t>(200000);
}

struct bench_latency_stats_t {
    bench_latency_stats_t() : mean_us(0.0), p95_us(0.0), p99_us(0.0) {}
    double mean_us;
    double p95_us;
    double p99_us;
};

struct server_queue_stats_t {
    server_queue_stats_t() :
        snd_pending_max(0.0),
        rcv_pending_max(0.0),
        rcv_pending_end(0.0)
    {}

    double snd_pending_max;
    double rcv_pending_max;
    double rcv_pending_end;
};

class bench_latency_sampler_t {
public:
    explicit bench_latency_sampler_t(
      size_t sample_cap_ = resolve_latency_sample_cap()) :
      _sample_cap(sample_cap_ > 0 ? sample_cap_ : 1),
      _count(0),
      _sum_us(0.0),
      _rng_state(0x9e3779b97f4a7c15ULL)
    {
        _samples.reserve(_sample_cap);
    }

    void add(double latency_us_)
    {
        const double sample = latency_us_ >= 0.0 ? latency_us_ : 0.0;
        ++_count;
        _sum_us += sample;

        if (_samples.size() < _sample_cap) {
            _samples.push_back(sample);
            return;
        }

        const unsigned long long slot = next_rand_u64() % _count;
        if (slot < static_cast<unsigned long long>(_sample_cap))
            _samples[static_cast<size_t>(slot)] = sample;
    }

    void reset()
    {
        _count = 0;
        _sum_us = 0.0;
        _rng_state = 0x9e3779b97f4a7c15ULL;
        _samples.clear();
    }

    void merge_from(const bench_latency_sampler_t &other_)
    {
        if (other_._count == 0)
            return;

        _count += other_._count;
        _sum_us += other_._sum_us;
        for (size_t i = 0; i < other_._samples.size(); ++i) {
            if (_samples.size() < _sample_cap) {
                _samples.push_back(other_._samples[i]);
                continue;
            }

            const unsigned long long slot = next_rand_u64() % _count;
            if (slot < static_cast<unsigned long long>(_sample_cap))
                _samples[static_cast<size_t>(slot)] = other_._samples[i];
        }
    }

    unsigned long long count() const { return _count; }
    double sum_us() const { return _sum_us; }

    void append_samples(std::vector<double> *out_) const
    {
        if (!out_ || _samples.empty())
            return;
        out_->insert(out_->end(), _samples.begin(), _samples.end());
    }

    bench_latency_stats_t snapshot()
    {
        bench_latency_stats_t out;
        if (_count == 0)
            return out;

        out.mean_us = _sum_us / static_cast<double>(_count);
        if (_samples.empty()) {
            out.p95_us = out.mean_us;
            out.p99_us = out.mean_us;
            return out;
        }

        std::sort(_samples.begin(), _samples.end());
        out.p95_us = percentile_from_sorted(_samples, 0.95);
        out.p99_us = percentile_from_sorted(_samples, 0.99);
        return out;
    }

private:
    static double percentile_from_sorted(const std::vector<double> &sorted_,
                                         double q_)
    {
        if (sorted_.empty())
            return 0.0;
        if (q_ <= 0.0)
            return sorted_.front();
        if (q_ >= 1.0)
            return sorted_.back();

        const double pos = (sorted_.size() - 1) * q_;
        const size_t lo = static_cast<size_t>(pos);
        const size_t hi = lo + 1 < sorted_.size() ? lo + 1 : lo;
        const double frac = pos - static_cast<double>(lo);
        return sorted_[lo] + (sorted_[hi] - sorted_[lo]) * frac;
    }

    unsigned long long next_rand_u64()
    {
        if (_rng_state == 0)
            _rng_state = 0x9e3779b97f4a7c15ULL;
        unsigned long long x = _rng_state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        _rng_state = x;
        return x;
    }

    size_t _sample_cap;
    unsigned long long _count;
    double _sum_us;
    unsigned long long _rng_state;
    std::vector<double> _samples;
};

inline int bench_io_threads()
{
    return parse_positive_env("PERF_IO_THREADS", 4);
}

inline int bench_max_sockets()
{
    const int explicit_max = parse_positive_env("PERF_MAX_SOCKETS", 0);
    if (explicit_max > 0)
        return explicit_max;

    const int clients = parse_positive_env("PERF_CLIENTS", 0);
    if (clients <= 0)
        return 0;

    // Multi mode keeps the benchmark socket and monitor plumbing per client.
    // Reserve enough context socket slots for large STREAM runs.
    const long required = static_cast<long>(clients) * 3L + 4096L;
    if (required > INT_MAX)
        return INT_MAX;
    return static_cast<int>(required);
}

inline int bench_ctx_blocky()
{
    const char *value = std::getenv("PERF_CTX_BLOCKY");
    if (!value || !*value)
        return 0;

    errno = 0;
    char *end = NULL;
    const long parsed = std::strtol(value, &end, 10);
    if (errno != 0 || end == value)
        return 0;
    return parsed != 0 ? 1 : 0;
}

inline void apply_ctx_options(void *ctx_)
{
    const bool debug = std::getenv("PERF_DEBUG") != NULL;
    const int io_threads = bench_io_threads();
    if (io_threads > 0) {
        const int rc = zlink_ctx_set(ctx_, ZLINK_IO_THREADS, io_threads);
        if (rc != 0 && debug) {
            std::cerr << "zlink_ctx_set(ZLINK_IO_THREADS) failed: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        }
    }

    const int max_sockets = bench_max_sockets();
    if (max_sockets > 0) {
        const int rc = zlink_ctx_set(ctx_, ZLINK_MAX_SOCKETS, max_sockets);
        if (rc != 0 && debug) {
            std::cerr << "zlink_ctx_set(ZLINK_MAX_SOCKETS) failed: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        }
    }

    const int blocky = bench_ctx_blocky();
    const int blocky_rc =
      zlink_ctx_set(ctx_, static_cast<zlink_ctx_option_t>(ZLINK_BLOCKY), blocky);
    if (blocky_rc != 0 && debug) {
        std::cerr << "zlink_ctx_set(ZLINK_BLOCKY) failed: "
                  << zlink_strerror(zlink_errno()) << std::endl;
    }
}

class ctx_guard_t {
public:
    ctx_guard_t() : _ctx(zlink_ctx_new()) {
        if (_ctx)
            apply_ctx_options(_ctx);
    }
    ~ctx_guard_t() {
        if (_ctx) {
            zlink_ctx_shutdown(_ctx);

            //  In high-throughput STREAM benchmarks, blocking ctx_term can hang for
            //  a long time after metrics are already emitted. Keep shutdown as the
            //  default and allow explicit full term via PERF_CTX_TERM=1.
            const char *term_env = std::getenv("PERF_CTX_TERM");
            if (term_env && std::strcmp(term_env, "0") != 0)
                zlink_ctx_term(_ctx);
        }
    }

    // Force full context termination immediately (blocking) and clear handle.
    void force_term()
    {
        if (!_ctx)
            return;
        zlink_ctx_shutdown(_ctx);
        zlink_ctx_term(_ctx);
        _ctx = NULL;
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
    socket_guard_t(void *ctx_, zlink_socket_type_t type_) : _socket(zlink_socket(ctx_, type_)) {}
    ~socket_guard_t() {
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

inline int zlink_msg_recv(zlink_msg_t *msg_, void *socket_, int flags_)
{
    return zlink::recv_msg_internal(socket_, msg_, flags_);
}

inline int zlink_recv(void *socket_, void *buf_, size_t len_, int flags_)
{
    return zlink::recv_buffer_internal(socket_, buf_, len_, flags_);
}

inline bool bench_debug_enabled();
inline int bench_hwm_from_env(const char *name_, int default_hwm_);
inline bool set_sockopt_int(void *socket_,
                            zlink_socket_option_t option_,
                            int value_,
                            const char *label_);

struct connect_monitor_state_t {
    connect_monitor_state_t() :
        connection_ready_count(0),
        accepted_count(0),
        connected_count(0),
        error_code(0)
    {}

    std::mutex sync;
    std::condition_variable cv;
    size_t connection_ready_count;
    size_t accepted_count;
    size_t connected_count;
    int error_code;
};

struct connect_monitor_t {
    connect_monitor_t() : owner(NULL), monitor(NULL), state(NULL) {}

    void *owner;
    void *monitor;
    connect_monitor_state_t *state;
};

struct connect_monitor_registry_t {
    std::mutex sync;
    std::map<void *, connect_monitor_state_t *> states;
};

inline connect_monitor_registry_t &connect_monitor_registry()
{
    static connect_monitor_registry_t registry;
    return registry;
}

inline void register_connect_monitor(void *monitor_,
                                     connect_monitor_state_t *state_)
{
    if (!monitor_ || !state_)
        return;

    connect_monitor_registry_t &registry = connect_monitor_registry();
    std::lock_guard<std::mutex> lock(registry.sync);
    registry.states[monitor_] = state_;
}

inline void unregister_connect_monitor(void *monitor_)
{
    if (!monitor_)
        return;

    connect_monitor_registry_t &registry = connect_monitor_registry();
    std::lock_guard<std::mutex> lock(registry.sync);
    registry.states.erase(monitor_);
}

inline connect_monitor_state_t *find_connect_monitor_state_for_current_dispatch()
{
    void *monitor = zlink::current_monitor_dispatch_handle();
    if (!monitor)
        return NULL;

    connect_monitor_registry_t &registry = connect_monitor_registry();
    std::lock_guard<std::mutex> lock(registry.sync);
    std::map<void *, connect_monitor_state_t *>::iterator it =
      registry.states.find(monitor);
    return it != registry.states.end() ? it->second : NULL;
}

inline void connect_monitor_handler(const zlink_monitor_event_t *event_)
{
    connect_monitor_state_t *state =
      find_connect_monitor_state_for_current_dispatch();
    if (!state || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock(state->sync);
        switch (event_->event) {
            case ZLINK_EVENT_CONNECTION_READY:
                ++state->connection_ready_count;
                break;

            case ZLINK_EVENT_ACCEPTED:
                ++state->accepted_count;
                break;

            case ZLINK_EVENT_CONNECTED:
                ++state->connected_count;
                break;

            case ZLINK_EVENT_BIND_FAILED:
            case ZLINK_EVENT_ACCEPT_FAILED:
            case ZLINK_EVENT_CLOSE_FAILED:
            case ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL:
            case ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL:
            case ZLINK_EVENT_HANDSHAKE_FAILED_AUTH:
                if (state->error_code == 0)
                    state->error_code =
                      event_->value > 0 ? static_cast<int>(event_->value) : EIO;
                break;

            default:
                break;
        }
    }
    state->cv.notify_all();
}

inline size_t connect_ready_count(const connect_monitor_state_t *state_)
{
    if (!state_)
        return 0;
    return std::max(std::max(state_->connection_ready_count,
                             state_->accepted_count),
                    state_->connected_count);
}

inline bool open_connect_monitor(void *socket_, connect_monitor_t &out_)
{
    out_.owner = socket_;
    out_.monitor = NULL;
    out_.state = NULL;
    if (!socket_)
        return false;

    connect_monitor_state_t *state = new (std::nothrow) connect_monitor_state_t();
    if (!state)
        return false;

    void *monitor = zlink_socket_monitor_open(
      socket_,
      ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_CONNECTED
        | ZLINK_EVENT_ACCEPTED | ZLINK_EVENT_BIND_FAILED
        | ZLINK_EVENT_ACCEPT_FAILED | ZLINK_EVENT_CLOSE_FAILED
        | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
        | ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
        | ZLINK_EVENT_HANDSHAKE_FAILED_AUTH,
      &connect_monitor_handler);
    if (!monitor) {
        delete state;
        return false;
    }

    const int monitor_hwm = bench_hwm_from_env("PERF_MONITOR_HWM", 1000);
    set_sockopt_int(monitor, ZLINK_LINGER, 0, "ZLINK_LINGER");
    if (monitor_hwm > 0) {
        set_sockopt_int(monitor, ZLINK_SNDHWM, monitor_hwm, "ZLINK_SNDHWM");
        set_sockopt_int(monitor, ZLINK_RCVHWM, monitor_hwm, "ZLINK_RCVHWM");
    }

    register_connect_monitor(monitor, state);
    out_.monitor = monitor;
    out_.state = state;
    return true;
}

inline void stop_and_close_socket_monitor(void *owner_, void **monitor_p_)
{
    if (!monitor_p_ || !*monitor_p_)
        return;

    (void) owner_;
    void *monitor = *monitor_p_;
    *monitor_p_ = NULL;
    (void) zlink_close(monitor);
}

inline int poll_connect_ready_count(connect_monitor_t &monitor_)
{
    if (!monitor_.state)
        return 0;

    std::lock_guard<std::mutex> lock(monitor_.state->sync);
    return static_cast<int>(connect_ready_count(monitor_.state));
}

inline bool wait_connect_ready_count(connect_monitor_t &monitor_,
                                     size_t expected_ready_,
                                     int timeout_ms_)
{
    if (expected_ready_ == 0)
        return true;
    if (!monitor_.state)
        return false;

    std::unique_lock<std::mutex> lock(monitor_.state->sync);
    if (monitor_.state->error_code != 0)
        return false;
    if (connect_ready_count(monitor_.state) >= expected_ready_)
        return true;

    const int bounded_timeout = timeout_ms_ > 0 ? timeout_ms_ : 0;
    if (bounded_timeout == 0)
        return false;

    const bool signaled = monitor_.state->cv.wait_for(
      lock,
      milliseconds_t(bounded_timeout),
      [&monitor_, expected_ready_]() {
          return monitor_.state->error_code != 0
                 || connect_ready_count(monitor_.state) >= expected_ready_;
      });
    if (!signaled && bench_debug_enabled()) {
        std::cerr << "[perf-multi] connect ready timeout connected="
                  << monitor_.state->connected_count
                  << " accepted=" << monitor_.state->accepted_count
                  << " ready=" << monitor_.state->connection_ready_count
                  << " expected=" << expected_ready_ << std::endl;
    }
    return signaled && monitor_.state->error_code == 0
           && connect_ready_count(monitor_.state) >= expected_ready_;
}

inline void close_connect_monitor(connect_monitor_t &monitor_)
{
    connect_monitor_state_t *state = monitor_.state;
    void *owner = monitor_.owner;
    void *monitor = monitor_.monitor;
    void *monitor_id = monitor;
    monitor_.owner = NULL;
    monitor_.monitor = NULL;
    monitor_.state = NULL;

    if (!monitor && !state)
        return;

    if (monitor) {
        stop_and_close_socket_monitor(owner, &monitor);
        unregister_connect_monitor(monitor_id);
        delete state;
        return;
    }

    if (bench_debug_enabled()) {
        std::cerr << "[perf-multi] monitor close failed";
        if (monitor)
            std::cerr << ": " << zlink_strerror(zlink_errno());
        std::cerr << std::endl;
    }
}

inline void print_result(const std::string& lib_type,
                         const std::string& pattern,
                         const std::string& transport,
                         size_t size,
                         double throughput,
                         double latency,
                         double latency_p95,
                         double latency_p99) {
    const bool is_echo_pattern =
      pattern == "DEALER_ROUTER"
      || pattern == "ROUTER_ROUTER"
      || pattern == "GATEWAY"
      || pattern == "STREAM_CALLBACK"
      || pattern == "MULTI_DEALER_ROUTER"
      || pattern == "MULTI_ROUTER_ROUTER"
      || pattern == "MULTI_GATEWAY"
      || pattern == "MULTI_STREAM_CALLBACK";
    const double direction_factor = is_echo_pattern ? 2.0 : 1.0;
    const double bandwidth_mb_s =
      (throughput * static_cast<double>(size) * direction_factor) / 1000000.0;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport << "," << size
              << ",throughput," << std::fixed << std::setprecision(3) << throughput << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport << "," << size
              << ",bandwidth," << std::fixed << std::setprecision(3) << bandwidth_mb_s << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport << "," << size
              << ",latency," << std::fixed << std::setprecision(3) << latency << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport << "," << size
              << ",latency_p95," << std::fixed << std::setprecision(3) << latency_p95 << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport << "," << size
              << ",latency_p99," << std::fixed << std::setprecision(3) << latency_p99 << std::endl;
}

inline void print_server_queue_metrics(const std::string &lib_type,
                                       const std::string &pattern,
                                       const std::string &transport,
                                       size_t size,
                                       const server_queue_stats_t &queue_stats)
{
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",server_snd_pending_max,"
              << std::fixed << std::setprecision(3) << queue_stats.snd_pending_max
              << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",server_rcv_pending_max,"
              << std::fixed << std::setprecision(3) << queue_stats.rcv_pending_max
              << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",server_rcv_pending_end,"
              << std::fixed << std::setprecision(3) << queue_stats.rcv_pending_end
              << std::endl;
}

inline bool read_socket_snapshot_once(void *socket_,
                                      zlink_monitor_snapshot_t *out_)
{
    if (!socket_ || !out_)
        return false;

    void *monitor = zlink_socket_monitor_open(
      socket_, ZLINK_EVENT_ALL, &zlink_monitor_ignore_handler);
    if (!monitor)
        return false;

    memset(out_, 0, sizeof(*out_));
    const int rc = zlink_monitor_snapshot(monitor, out_);
    stop_and_close_socket_monitor(socket_, &monitor);
    return rc == 0;
}

inline bool read_gateway_snapshot_once(void *gateway_,
                                       zlink_monitor_snapshot_t *out_)
{
    if (!gateway_ || !out_)
        return false;

    void *monitor = zlink_gateway_monitor_open(
      gateway_,
      ZLINK_GATEWAY_SERVICE_READY | ZLINK_GATEWAY_SERVICE_LOST
        | ZLINK_GATEWAY_SEND_READY_CHANGED | ZLINK_GATEWAY_ROUTE_UP
        | ZLINK_GATEWAY_ROUTE_DOWN | ZLINK_GATEWAY_MONITOR_EVENT_ERROR,
      &zlink_service_monitor_ignore_handler);
    if (!monitor)
        return false;

    memset(out_, 0, sizeof(*out_));
    const int rc = zlink_monitor_snapshot(monitor, out_);
    (void) zlink_service_monitor_close(&monitor);
    return rc == 0;
}

inline bool read_spot_snapshot_once(void *spot_,
                                    zlink_spot_role_t role_,
                                    zlink_monitor_snapshot_t *out_)
{
    if (!spot_ || !out_)
        return false;

    void *monitor = zlink_spot_monitor_open(
      spot_, role_,
      ZLINK_MONITOR_EVENT_READY | ZLINK_MONITOR_EVENT_LOST
        | ZLINK_MONITOR_EVENT_PEER_UP | ZLINK_MONITOR_EVENT_PEER_DOWN
        | ZLINK_MONITOR_EVENT_ERROR
        | ZLINK_SPOT_PUB_DELIVERY_READY_CHANGED
        | ZLINK_SPOT_PUB_FIRST_DELIVERY_READY_CHANGED
        | ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED,
      &zlink_service_monitor_ignore_handler);
    if (!monitor)
        return false;

    memset(out_, 0, sizeof(*out_));
    const int rc = zlink_monitor_snapshot(monitor, out_);
    (void) zlink_service_monitor_close(&monitor);
    return rc == 0;
}

inline size_t read_socket_ready_peer_count(void *socket_)
{
    zlink_monitor_snapshot_t snapshot;
    if (!read_socket_snapshot_once(socket_, &snapshot)
        || !(snapshot.detail_flags
             & ZLINK_MONITOR_SNAPSHOT_DETAIL_READY_PEER_COUNT)) {
        return 0;
    }
    return static_cast<size_t>(snapshot.ready_peer_count);
}

inline server_queue_stats_t sample_server_queue_stats(void *send_socket_,
                                                      void *recv_socket_)
{
    server_queue_stats_t out;
    zlink_monitor_snapshot_t snapshot;

    if (read_socket_snapshot_once(send_socket_, &snapshot)
        && (snapshot.detail_flags
            & ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS)) {
        out.snd_pending_max =
          static_cast<double>(static_cast<unsigned long long>(
            snapshot.snd_pending_msgs));
    }
    if (read_socket_snapshot_once(recv_socket_, &snapshot)
        && (snapshot.detail_flags
            & ZLINK_MONITOR_SNAPSHOT_DETAIL_RCV_PENDING_MSGS)) {
        const double pending = static_cast<double>(
          static_cast<unsigned long long>(snapshot.rcv_pending_msgs));
        out.rcv_pending_max = pending;
        out.rcv_pending_end = pending;
    }

    return out;
}

inline void print_result(const std::string& lib_type,
                         const std::string& pattern,
                         const std::string& transport,
                         size_t size,
                         double throughput,
                         double latency) {
    print_result(
      lib_type, pattern, transport, size, throughput, latency, latency, latency);
}

inline bool bench_debug_enabled() {
    static const bool enabled = std::getenv("PERF_DEBUG") != nullptr;
    return enabled;
}

inline bool bench_transition_debug_enabled() {
    static const bool enabled =
      std::getenv("PERF_DEBUG_TRANSITIONS") != nullptr;
    return enabled;
}

inline bool set_sockopt_int(void *socket_, zlink_socket_option_t option_,
                            int value_, const char *name_) {
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

inline int bench_hwm_from_env(const char *name_, int default_hwm_)
{
    if (!name_ || !*name_)
        return default_hwm_;

    const char *value = std::getenv(name_);
    if (!value || !*value)
        return default_hwm_;

    errno = 0;
    char *end = NULL;
    const long parsed = std::strtol(value, &end, 10);
    if (errno != 0 || end == value || parsed <= 0)
        return default_hwm_;
    if (parsed > INT_MAX)
        return INT_MAX;
    return static_cast<int>(parsed);
}

inline void apply_benchmark_hwm(void *socket_, int hwm_value)
{
    if (hwm_value <= 0)
        return;

    const int sndhwm =
      bench_hwm_from_env("PERF_SNDHWM", hwm_value);
    const int rcvhwm =
      bench_hwm_from_env("PERF_RCVHWM", hwm_value);
    set_sockopt_int(socket_, ZLINK_SNDHWM, sndhwm, "ZLINK_SNDHWM");
    set_sockopt_int(socket_, ZLINK_RCVHWM, rcvhwm, "ZLINK_RCVHWM");
}

inline int bench_timeout_ms_from_env(const char *name_, int default_ms_)
{
    if (!name_ || !*name_)
        return default_ms_;

    const char *value = std::getenv(name_);
    if (!value || !*value)
        return default_ms_;

    errno = 0;
    char *end = NULL;
    const long parsed = std::strtol(value, &end, 10);
    if (errno != 0 || end == value || parsed <= 0)
        return default_ms_;
    if (parsed > INT_MAX)
        return INT_MAX;
    return static_cast<int>(parsed);
}

inline int parse_byte_size_token(const char *value_, int default_value_)
{
    if (!value_ || !*value_)
        return default_value_;

    errno = 0;
    char *end = NULL;
    const unsigned long long parsed = std::strtoull(value_, &end, 10);
    if (errno != 0 || end == value_)
        return default_value_;

    unsigned long long multiplier = 1;
    if (end && *end) {
        char suffix[3] = {0, 0, 0};
        size_t suffix_len = 0;
        while (end[suffix_len] != '\0' && suffix_len < 2) {
            suffix[suffix_len] =
              static_cast<char>(std::tolower(static_cast<unsigned char>(end[suffix_len])));
            ++suffix_len;
        }
        if (end[suffix_len] != '\0')
            return default_value_;

        if (suffix[0] == 'b' && suffix[1] == '\0')
            multiplier = 1;
        else if (suffix[0] == 'k' && (suffix[1] == '\0' || suffix[1] == 'b'))
            multiplier = 1024ULL;
        else if (suffix[0] == 'm' && (suffix[1] == '\0' || suffix[1] == 'b'))
            multiplier = 1024ULL * 1024ULL;
        else if (suffix[0] == 'g' && (suffix[1] == '\0' || suffix[1] == 'b'))
            multiplier = 1024ULL * 1024ULL * 1024ULL;
        else
            return default_value_;
    }

    const unsigned long long bytes = parsed * multiplier;
    if (bytes == 0)
        return default_value_;
    if (bytes > static_cast<unsigned long long>(INT_MAX))
        return INT_MAX;
    return static_cast<int>(bytes);
}

inline int bench_socket_buffer_bytes_from_env(const char *name_,
                                              int default_bytes_)
{
    if (!name_ || !*name_)
        return default_bytes_;

    const char *value = std::getenv(name_);
    if (!value || !*value)
        return default_bytes_;

    return parse_byte_size_token(value, default_bytes_);
}

inline void apply_debug_timeouts(void *socket_, const std::string &transport) {
    if (transport == "inproc")
        return;

    const int sndtimeo_ms =
      bench_timeout_ms_from_env("PERF_SNDTIMEO_MS", 200);
    const int rcvtimeo_ms =
      bench_timeout_ms_from_env("PERF_RCVTIMEO_MS", 200);
    set_sockopt_int(socket_, ZLINK_SNDTIMEO, sndtimeo_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int(socket_, ZLINK_RCVTIMEO, rcvtimeo_ms, "ZLINK_RCVTIMEO");
}

inline void apply_benchmark_socket_options(void *socket_,
                                           int hwm_value,
                                           const std::string &transport)
{
    if (!socket_)
        return;

    const int linger_ms = 0;
    const int sndbuf =
      bench_socket_buffer_bytes_from_env("PERF_SNDBUF", -1);
    const int rcvbuf =
      bench_socket_buffer_bytes_from_env("PERF_RCVBUF", -1);
    set_sockopt_int(socket_, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    if (sndbuf > 0)
        set_sockopt_int(socket_, ZLINK_SNDBUF, sndbuf, "ZLINK_SNDBUF");
    if (rcvbuf > 0)
        set_sockopt_int(socket_, ZLINK_RCVBUF, rcvbuf, "ZLINK_RCVBUF");
    apply_benchmark_hwm(socket_, hwm_value);
    apply_debug_timeouts(socket_, transport);
}

inline std::string transport_from_endpoint(const std::string &endpoint)
{
    const std::string::size_type pos = endpoint.find("://");
    if (pos == std::string::npos)
        return std::string();
    return endpoint.substr(0, pos);
}

inline std::string make_endpoint(const std::string& transport, const std::string& id) {
    if (transport == "pgm" || transport == "epgm") {
        if (transport == "pgm") {
            if (const char *env = std::getenv("PERF_PGM_ENDPOINT")) {
                if (*env)
                    return std::string(env);
            }
        } else {
            if (const char *env = std::getenv("PERF_EPGM_ENDPOINT")) {
                if (*env)
                    return std::string(env);
            }
        }
#if !defined(_WIN32)
        struct ifaddrs *ifaddr = nullptr;
        if (getifaddrs(&ifaddr) == 0) {
            for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr)
                    continue;
                if (!(ifa->ifa_flags & IFF_UP))
                    continue;
                if (!(ifa->ifa_flags & IFF_MULTICAST))
                    continue;
                if (ifa->ifa_flags & IFF_LOOPBACK)
                    continue;
                if (ifa->ifa_addr->sa_family != AF_INET)
                    continue;
                char addr[INET_ADDRSTRLEN];
                const struct sockaddr_in *sa =
                  reinterpret_cast<const struct sockaddr_in *>(ifa->ifa_addr);
                if (inet_ntop(AF_INET, &sa->sin_addr, addr, sizeof(addr))) {
                    std::string endpoint =
                      transport + "://" + addr + ";239.192.1.1:5555";
                    freeifaddrs(ifaddr);
                    return endpoint;
                }
            }
            freeifaddrs(ifaddr);
        }
#endif
        return std::string();
    }
    if (transport == "inproc") return "inproc://" + id;
    if (transport == "ipc") return "ipc://*";
    if (transport == "ws") return "ws://127.0.0.1:*";
    if (transport == "wss") return "wss://127.0.0.1:*";
    if (transport == "tls") return "tls://127.0.0.1:*";
    return "tcp://127.0.0.1:*";
}

inline std::string make_fixed_endpoint(const std::string& transport, int port) {
    const std::string host = "127.0.0.1";
    const std::string port_str = std::to_string(port);
    if (transport == "ws") return "ws://" + host + ":" + port_str;
    if (transport == "wss") return "wss://" + host + ":" + port_str;
    if (transport == "tls") return "tls://" + host + ":" + port_str;
    return "tcp://" + host + ":" + port_str;
}

inline void *resolve_symbol(const char *name) {
#if defined(_WIN32)
    HMODULE module = GetModuleHandleA(NULL);
    if (!module)
        return NULL;
    return reinterpret_cast<void *>(GetProcAddress(module, name));
#else
    return dlsym(RTLD_DEFAULT, name);
#endif
}

// --- Embedded Test Certificates for TLS ---
namespace test_certs {

static const char *ca_cert_pem =
  "-----BEGIN CERTIFICATE-----\n"
  "MIIDlzCCAn+gAwIBAgIUbGLNLbwV7np9Q07zD9ZWvmA+nkAwDQYJKoZIhvcNAQEL\n"
  "BQAwWzELMAkGA1UEBhMCVVMxDTALBgNVBAgMBFRlc3QxDTALBgNVBAcMBFRlc3Qx\n"
  "FjAUBgNVBAoMDVpMaW5rIFRlc3QgQ0ExFjAUBgNVBAMMDVpMaW5rIFRlc3QgQ0Ew\n"
  "HhcNMjYwMTEyMTEyMjUzWhcNMzYwMTEwMTEyMjUzWjBbMQswCQYDVQQGEwJVUzEN\n"
  "MAsGA1UECAwEVGVzdDENMAsGA1UEBwwEVGVzdDEWMBQGA1UECgwNWkxpbmsgVGVz\n"
  "dCBDQTEWMBQGA1UEAwwNWkxpbmsgVGVzdCBDQTCCASIwDQYJKoZIhvcNAQEBBQAD\n"
  "ggEPADCCAQoCggEBAKHAdjzB5SsoFlce8T4XBvQa0LAbYP9hQ+jcLXSzoF/QDmeP\n"
  "sxGSE1WINM7ZT9BOqNa8OKl7kWWWYS45XeeqrNLVHDQbz9DvUAqUVaSsoxyAxCtV\n"
  "8Zq+F6Zy01qbLXi+Nv1jWz685X9KSc5SCKz9acoOSBU7IOtJKCQ+QM+/x9PMqQeg\n"
  "B+aRNkv+WE4RRLbpQnIGqSiZkUsNI6Z97o2otsHkGa1oVWWXmKqzUAmembVHjiCl\n"
  "Rn9Ut4/HqqopLn/k2m7/Lj62QT6sOcB8ixDe+H4TwDF6sbxgHcs/1sdobys6VsUF\n"
  "gFSJ5Dm33yYBjQmLfxXRaKMxKGukLmAofa+f28sCAwEAAaNTMFEwHQYDVR0OBBYE\n"
  "FO3BqMenuNdTJuCz5tywoNrd11KjMB8GA1UdIwQYMBaAFO3BqMenuNdTJuCz5tyw\n"
  "oNrd11KjMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQELBQADggEBADF2GjWc\n"
  "BuvU/3bG2406XNFtl7pb4V70zClo269Gb/SYVrF0k6EXp2I8UQ7cPXM+ueWu8JeG\n"
  "XCbSTRADWxw702VxryCXLIYYMZ5hwF5ZtDGOagZQWSz38UFy2acCRNqY2ijyISQn\n"
  "3M8YtRdeEGOan+gtTC6/xB3IIRX1tFohT35G/wjld8hs6kJVokYhVfKhk4EZKSxH\n"
  "IiHsVaafpjUwm4EkAwCmwAWkOalKijbo5Jdq9h3UNfOn4RblN80FU/jD2cBFP+L8\n"
  "U/Juz13KFa/4NXp9flzUl/1w5o//V1UXUpfYOMsVT8BaP3dV1pa9lDwhoJERyiI1\n"
  "xj0kGsPBIt3nVwE=\n"
  "-----END CERTIFICATE-----\n";

static const char *server_cert_pem =
  "-----BEGIN CERTIFICATE-----\n"
  "MIIDrTCCApWgAwIBAgIUH3bva6lTINNSQ2BpgpJStZpT5NQwDQYJKoZIhvcNAQEL\n"
  "BQAwWzELMAkGA1UEBhMCVVMxDTALBgNVBAgMBFRlc3QxDTALBgNVBAcMBFRlc3Qx\n"
  "FjAUBgNVBAoMDVpMaW5rIFRlc3QgQ0ExFjAUBgNVBAMMDVpMaW5rIFRlc3QgQ0Ew\n"
  "HhcNMjYwMTEyMTEyMzAxWhcNMjcwMTEyMTEyMzAxWjBUMQswCQYDVQQGEwJVUzEN\n"
  "MAsGA1UECAwEVGVzdDENMAsGA1UEBwwEVGVzdDETMBEGA1UECgwKWkxpbmsgVGVz\n"
  "dDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIB\n"
  "CgKCAQEAxZ5FpHxoY5JaTfbS3D1nSlz+BdvnrsZ5PqG+P/H1oGXJnY/2MMZGEeUZ\n"
  "SZg9pVn6ZRURyGTwAHN1X+xarpX057pKfqWtHLztj2+WSJLbBfzSzwPdYNMP/h1C\n"
  "MX9zMbui6ui8Tbys1g5IKO/ZEMRN8bVNHOJ4xkK829RzEu6f/4YCuf4Lz+Z1X4en\n"
  "VBi7DGkWRSUiACjlGvVyZ24KHkLCggbAO3HhhyjZ4FwVd9JuE+d2/jm/neUu6HTt\n"
  "J/9d/5GCovUamkuYWn+e62HA1FkpSnXNbgRrkmAkOrliJG1uCqh3btVzuF1c91Jj\n"
  "8wjm0wm23lDeGVrCWExvyFhk3LBFCwIDAQABo3AwbjAsBgNVHREEJTAjgglsb2Nh\n"
  "bGhvc3SHBH8AAAGHEAAAAAAAAAAAAAAAAAAAAAEwHQYDVR0OBBYEFFrMgnC8k4I0\n"
  "XMjURlF0zXV59HJYMB8GA1UdIwQYMBaAFO3BqMenuNdTJuCz5tywoNrd11KjMA0G\n"
  "CSqGSIb3DQEBCwUAA4IBAQCcXiKLN5y7rumetdr55PMDdx+4EV1Wl28fWCOB5nur\n"
  "kFZRy876pFphFqZppjGCHWiiHzUIsZXUej/hBmY+OhsL13ojfGiACz/44OFzqCUa\n"
  "I83V1M9ywbty09zhdqFc9DFfpiC2+ltDCn7o+eF7THUzgDg4fRZYHYM1njZElZaG\n"
  "ecFImsQzqFIpmhB/TfZIZVmBQryYN+V1fl4sUJFiYEOr49RjWnATf6RKY3J5VKHp\n"
  "TWSm7rTd4jB0CvyNlPpS+fYBdGC72m6R3zrce8Scfto+HPH4YdIU5AdoRHCCtOrA\n"
  "Mq9brLTPUzAqlzC7zDw41hI/MS1Cdcxb1dZkKHgMXu8W\n"
  "-----END CERTIFICATE-----\n";

static const char *server_key_pem =
  "-----BEGIN PRIVATE KEY-----\n"
  "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDFnkWkfGhjklpN\n"
  "9tLcPWdKXP4F2+euxnk+ob4/8fWgZcmdj/YwxkYR5RlJmD2lWfplFRHIZPAAc3Vf\n"
  "7FqulfTnukp+pa0cvO2Pb5ZIktsF/NLPA91g0w/+HUIxf3Mxu6Lq6LxNvKzWDkgo\n"
  "79kQxE3xtU0c4njGQrzb1HMS7p//hgK5/gvP5nVfh6dUGLsMaRZFJSIAKOUa9XJn\n"
  "bgoeQsKCBsA7ceGHKNngXBV30m4T53b+Ob+d5S7odO0n/13/kYKi9RqaS5haf57r\n"
  "YcDUWSlKdc1uBGuSYCQ6uWIkbW4KqHdu1XO4XVz3UmPzCObTCbbeUN4ZWsJYTG/I\n"
  "WGTcsEULAgMBAAECggEACAoWclsKcmqN71yaf7ZbyBZBP95XW9UAn7byx25UDn5H\n"
  "3woUsgr8nehSyJuIx6CULMKPGVs3lXP4bpXbqyG4CeAss/H+XeekkL5D0nO4IsE5\n"
  "BSBkaL/Wh275kbCA8HyU9gAZkQLkZbPFCb+XCKLfOpntcHWGut2CLs/VVzCLbX1A\n"
  "hHerqJf3qEW+cU1Va5On+A2BEK7XtYFIR6IabS2LN5ecoZUfQ4EoeypdpQPRKwqM\n"
  "m1tSet4CsRfovguLdY5Z/hAhFLZCMKF5zs8zzGln9+S+G5y2fdJ4VxwbeR0OqyAh\n"
  "cB56xJo3L7rLm6hAoIb0mVXaiyRRGEuCBE/t9/pmSQKBgQD2hQgHpC20bQCyh08B\n"
  "1CyJKz1ObZJeYCWR6hE0stUKKq9QizY9Ci8Q1Hg8eEAtKCKjW74DbJ7bgGJBm6rS\n"
  "yNgpZZ3zw6NDSm4wY33y4alB5jzMR+H7izb6vxMPVcXn3DpjzoklxkN4l8JvgTbt\n"
  "KxZWxD3hS+C6NuNKE4LHipJO1wKBgQDNN89O/71ktIBpxiEZk4sKzdq3JZMErFBi\n"
  "cFJ4vATJ1LstrWdOAtOgRqQN81GhCSZ79vybrcOaq4Q4qLzsOWrAo7nb53gq684Y\n"
  "GaVAZfxzA+qECyEY3CzrKnwIbSFvJY+IfA1QL/ricce8oL7lIRIP1+MuhvGUdw55\n"
  "vXs01Wv47QKBgDo1sW60esJW1spRHvvMkPOWzTQetWgphdWNkqCB9cIf0CPRq24A\n"
  "YJq1wOpubqD7ECrIt/ZxCJXGG+1oB48cM8aaoxBzSrLR+XDdnVjjpibUadjGxHq0\n"
  "JbhRs/t0AnY8T2FP3JyZ00a/dv8DYOfhu7WjQwVW+GqgGU1djAz4EJIjAoGBAJe+\n"
  "iOBVYmowvjN4eck7vDiE9xEuC4QNFnNzssfr326Oism/yv94P5voIC7gmJ+G8JoB\n"
  "i9BhsJ2R7fcnbmsOGc3QQwJEKisyqfZQIE16HC2/240/3X1QcTaC96wTZgGVuIin\n"
  "kgCVOeJvV8423nD2/zAP5sDkr4Wkc2O5pHzwwyIRAoGAID2/HQQbczTqQlEAXltB\n"
  "K8YbNLP75FY+9w10SH3B0hUnEP+9YdeHvxkXdWtewn+TjkXnc3AYlb9A9u7GUuB+\n"
  "K2AF/TMl2YdHFOEDtMAZ8IT6womo6JHYj4+FfbxPiMmOfBmOKrdxQ/WrqfCnZwEs\n"
  "Dhpkrp6xWJWSNvXS0XcWGfM=\n"
  "-----END PRIVATE KEY-----\n";

}  // namespace test_certs

// Write certificate to temp file and return path
inline std::string write_temp_cert(const char* content, const std::string& suffix) {
    std::string path = "/tmp/bench_" + suffix + ".pem";
    std::ofstream ofs(path);
    if (ofs) {
        ofs << content;
        ofs.close();
    }
    return path;
}

// Setup TLS options for server socket
inline bool setup_tls_server(void* socket, const std::string& transport) {
    if (transport != "tls" && transport != "wss") return true;

    static std::string cert_path = write_temp_cert(test_certs::server_cert_pem, "server_cert");
    static std::string key_path = write_temp_cert(test_certs::server_key_pem, "server_key");

    if (zlink_setsockopt(socket, ZLINK_TLS_CERT, cert_path.c_str(), cert_path.size()) != 0) {
        if (bench_debug_enabled())
            std::cerr << "Failed to set ZLINK_TLS_CERT: " << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }
    if (zlink_setsockopt(socket, ZLINK_TLS_KEY, key_path.c_str(), key_path.size()) != 0) {
        if (bench_debug_enabled())
            std::cerr << "Failed to set ZLINK_TLS_KEY: " << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }
    return true;
}

// Setup TLS options for client socket
inline bool setup_tls_client(void* socket, const std::string& transport) {
    if (transport != "tls" && transport != "wss") return true;

    static std::string ca_path = write_temp_cert(test_certs::ca_cert_pem, "ca_cert");
    static const char* hostname = "localhost";

    if (zlink_setsockopt(socket, ZLINK_TLS_CA, ca_path.c_str(), ca_path.size()) != 0) {
        if (bench_debug_enabled())
            std::cerr << "Failed to set ZLINK_TLS_CA: " << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }
    if (zlink_setsockopt(socket, ZLINK_TLS_HOSTNAME, hostname, strlen(hostname)) != 0) {
        if (bench_debug_enabled())
            std::cerr << "Failed to set ZLINK_TLS_HOSTNAME: " << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }
    int trust_system = 0;
    if (zlink_setsockopt(socket, ZLINK_TLS_TRUST_SYSTEM, &trust_system, sizeof(trust_system)) != 0) {
        if (bench_debug_enabled())
            std::cerr << "Failed to set ZLINK_TLS_TRUST_SYSTEM: " << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }
    return true;
}

inline std::string bind_and_resolve_endpoint(void *socket_,
                                             const std::string& transport,
                                             const std::string& id) {
    std::string endpoint = make_endpoint(transport, id);
    if (endpoint.empty()) {
        std::cerr << "No endpoint available for transport " << transport << std::endl;
        return std::string();
    }
    if (zlink_bind(socket_, endpoint.c_str()) != 0) {
        std::cerr << "bind failed for " << endpoint << ": "
                  << zlink_strerror(zlink_errno()) << std::endl;
        return std::string();
    }
    if (transport != "inproc") {
        char last_endpoint[MAX_SOCKET_STRING] = "";
        size_t size = sizeof(last_endpoint);
        if (zlink_getsockopt(socket_, ZLINK_LAST_ENDPOINT, last_endpoint, &size) != 0) {
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
                if (pos != std::string::npos) {
                    endpoint.replace(pos, tcp_ipv6_any.size(), "://127.0.0.1:");
                }
            }
        }
        if (bench_debug_enabled()) {
            std::cerr << "Resolved endpoint (" << transport << "): " << endpoint << std::endl;
        }
    }
    apply_debug_timeouts(socket_, transport);
    return endpoint;
}

inline bool transport_available(const std::string& transport) {
    if (transport == "ipc") return zlink_has("ipc") != 0;
    return true;
}

inline void settle() {
    std::this_thread::sleep_for(milliseconds_t(SETTLE_TIME_MS));
}

inline bool connect_checked(void *socket_,
                           const std::string& endpoint,
                           const std::string& transport = std::string()) {
    if (zlink_connect(socket_, endpoint.c_str()) != 0) {
        std::cerr << "connect failed for " << endpoint << ": "
                  << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }
    apply_debug_timeouts(socket_, transport.empty() ? transport_from_endpoint(endpoint)
                                                   : transport);
    if (bench_debug_enabled()) {
        std::cerr << "Connected to " << endpoint << std::endl;
    }
    return true;
}

inline bool setup_connected_pair(void *bind_socket_,
                                 void *connect_socket_,
                                 const std::string &transport_,
                                 const std::string &id_) {
    if (!setup_tls_server(bind_socket_, transport_)
        || !setup_tls_client(connect_socket_, transport_))
        return false;

    std::string endpoint =
      bind_and_resolve_endpoint(bind_socket_, transport_, id_);
    if (endpoint.empty())
        return false;
    if (!connect_checked(connect_socket_, endpoint))
        return false;

    settle();
    return true;
}

inline int resolve_msg_count(size_t size) {
    int count = (size <= 1024) ? 200000 : 20000;
    if (const char *env = std::getenv("PERF_MSG_COUNT")) {
        errno = 0;
        const long override = std::strtol(env, NULL, 10);
        if (errno == 0 && override > 0)
            count = static_cast<int>(override);
    }
    return count;
}

inline std::vector<size_t> resolve_bench_msg_sizes(size_t fallback_size)
{
    const size_t default_size = fallback_size > 0 ? fallback_size : 64;
    std::vector<size_t> sizes;

    if (const char *env = std::getenv("PERF_MSG_SIZES")) {
        const char *cur = env;
        while (*cur) {
            while (*cur == ',' || *cur == ' ' || *cur == '\t')
                ++cur;
            if (!*cur)
                break;

            errno = 0;
            char *end = NULL;
            const unsigned long parsed = std::strtoul(cur, &end, 10);
            if (errno == 0 && end != cur && parsed > 0)
                sizes.push_back(static_cast<size_t>(parsed));

            if (!end || end == cur)
                break;
            cur = end;
            while (*cur && *cur != ',')
                ++cur;
            if (*cur == ',')
                ++cur;
        }
    }

    if (sizes.empty())
        sizes.push_back(default_size);
    return sizes;
}

inline int resolve_bench_count(const char *env_name, int default_value) {
    if (const char *env = std::getenv(env_name)) {
        errno = 0;
        const long override = std::strtol(env, NULL, 10);
        if (errno == 0 && override > 0)
            return static_cast<int>(override);
    }
    return default_value;
}

template <typename RunFn>
inline int run_standard_bench_main(int argc_, char **argv_, RunFn run_) {
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
