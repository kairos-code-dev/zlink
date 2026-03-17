#ifndef PERF_RUNTIME_COMMON_HPP
#define PERF_RUNTIME_COMMON_HPP

#include "../../common/perf_infra.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
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
#include <mutex>
#include <new>
#include <zlink.h>

#include "../../../src/core/recv_internal.hpp"
#include "perf_common_multi.hpp"

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
static const int SETTLE_TIME_MS = 300;

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

inline int bench_hwm_from_env(const char *name_, int default_hwm_);

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

inline void connect_monitor_handler(const zlink_monitor_event_t *event_,
                                    void *userdata_)
{
    connect_monitor_state_t *state =
      static_cast<connect_monitor_state_t *>(userdata_);
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
      &connect_monitor_handler, state);
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
    monitor_.owner = NULL;
    monitor_.monitor = NULL;
    monitor_.state = NULL;

    if (!monitor && !state)
        return;

    if (monitor) {
        stop_and_close_socket_monitor(owner, &monitor);
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
      socket_, ZLINK_EVENT_ALL, &zlink_monitor_ignore_handler, NULL);
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
      &zlink_service_monitor_ignore_handler, NULL);
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
      &zlink_service_monitor_ignore_handler, NULL);
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

inline bool bench_transition_debug_enabled() {
    static const bool enabled =
      std::getenv("PERF_DEBUG_TRANSITIONS") != nullptr;
    return enabled;
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

// ---------------------------------------------------------------------------
// Shared signal-handler infrastructure for multi-server benchmarks
// ---------------------------------------------------------------------------

inline std::atomic<bool> &perf_stop_requested ()
{
    static std::atomic<bool> flag (false);
    return flag;
}

inline void perf_on_signal (int)
{
    perf_stop_requested ().store (true, std::memory_order_release);
}

inline void install_perf_signal_handlers ()
{
    std::signal (SIGINT, perf_on_signal);
#if defined(SIGTERM)
    std::signal (SIGTERM, perf_on_signal);
#endif
}

// ---------------------------------------------------------------------------
// Shared transport validation for multi-server / multi-client benchmarks
// ---------------------------------------------------------------------------

inline bool is_supported_transport (const std::string &transport_)
{
    return transport_ == "tcp" || transport_ == "tls" || transport_ == "ws"
           || transport_ == "wss";
}

// ---------------------------------------------------------------------------
// Shared bind helper for multi-server benchmarks
// ---------------------------------------------------------------------------

inline std::string bind_server_endpoint (void *server_,
                                         const std::string &transport_,
                                         const std::string &token_)
{
    const int bind_port =
      resolve_multi_int_env ("PERF_MULTI_SERVER_BIND_PORT", 0, 0);
    if (bind_port <= 0) {
        std::string endpoint_any = make_endpoint (transport_, token_);
        if (endpoint_any.empty ()) {
            std::cerr << "No endpoint available for transport " << transport_
                      << std::endl;
            return std::string ();
        }
        if (zlink_bind (server_, endpoint_any.c_str ()) != 0) {
            std::cerr << "bind failed for " << endpoint_any << ": "
                      << zlink_strerror (zlink_errno ()) << std::endl;
            return std::string ();
        }

        char last_endpoint[MAX_SOCKET_STRING] = "";
        size_t size = sizeof (last_endpoint);
        if (zlink_getsockopt (server_, ZLINK_LAST_ENDPOINT, last_endpoint, &size)
            == 0) {
            endpoint_any.assign (last_endpoint);
            const std::string any_v4 = "://0.0.0.0:";
            const std::string any_v6 = "://[::]:";
            size_t pos = endpoint_any.find (any_v4);
            if (pos != std::string::npos) {
                endpoint_any.replace (pos, any_v4.size (), "://127.0.0.1:");
            } else {
                pos = endpoint_any.find (any_v6);
                if (pos != std::string::npos)
                    endpoint_any.replace (pos, any_v6.size (), "://127.0.0.1:");
            }
        }

        apply_debug_timeouts (server_, transport_);
        return endpoint_any;
    }

    std::string endpoint = make_fixed_endpoint (transport_, bind_port);
    if (zlink_bind (server_, endpoint.c_str ()) != 0) {
        std::cerr << "bind failed for " << endpoint << ": "
                  << zlink_strerror (zlink_errno ()) << std::endl;
        return std::string ();
    }

    char last_endpoint[MAX_SOCKET_STRING] = "";
    size_t size = sizeof (last_endpoint);
    if (zlink_getsockopt (server_, ZLINK_LAST_ENDPOINT, last_endpoint, &size)
        == 0)
        endpoint.assign (last_endpoint);
    apply_debug_timeouts (server_, transport_);
    return endpoint;
}

#endif
