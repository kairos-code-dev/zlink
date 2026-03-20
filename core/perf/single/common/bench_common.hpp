#ifndef PERF_COMMON_HPP
#define PERF_COMMON_HPP

#include "../../common/perf_infra.hpp"

#include <chrono>
#include <condition_variable>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <new>
#include <thread>
#include <fstream>
#include <climits>
#include <zlink.h>

inline int zlink_gateway_send_rid (void *gateway_,
                                   const zlink_routing_id_t *routing_id_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_,
                                   int flags_)
{
    return ::zlink_send_rid (gateway_, routing_id_, parts_, part_count_, flags_);
}

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <dlfcn.h>
#include <poll.h>
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

#ifndef ZLINK_HAVE_POLLER
typedef struct zlink_pollitem_t
{
    void *socket;
    zlink_fd_t fd;
    short events;
    short revents;
} zlink_pollitem_t;
#endif

inline int perf_idle_wait_ms(long timeout_)
{
    if (timeout_ <= 0)
        return 0;

#if defined(_WIN32)
    const DWORD wait_ms = static_cast<DWORD>(
      timeout_ > static_cast<long>(DWORD(-1)) ? DWORD(-1) : timeout_);
    ::Sleep(wait_ms);
    return 0;
#else
    const int wait_ms =
      timeout_ > static_cast<long>(INT_MAX) ? INT_MAX : static_cast<int>(timeout_);
    int rc = 0;
    do {
        rc = ::poll(NULL, 0, wait_ms);
    } while (rc < 0 && errno == EINTR);
    return rc < 0 ? -1 : 0;
#endif
}

inline int perf_socket_poll(zlink_pollitem_t *items_, int nitems_, long timeout_)
{
    if (nitems_ < 0) {
        errno = EINVAL;
        return -1;
    }

    if (nitems_ == 0 || !items_)
        return perf_idle_wait_ms(timeout_);

    const auto start = std::chrono::steady_clock::now();
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
            if (zlink_get_option(items_[i].socket, ZLINK_OPT_EVENTS, &events,
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
            const long elapsed_ms = static_cast<long>(
              std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start)
                .count());
            if (elapsed_ms >= timeout_)
                return 0;
        }

        if (perf_idle_wait_ms(1) != 0)
            return -1;
    }
}

// --- Configuration ---
static const std::vector<size_t> MSG_SIZES = {64, 256, 1024, 65536, 131072, 262144};
static const std::vector<std::string> TRANSPORTS = {"tcp", "inproc", "ipc"};
static const std::vector<std::string> STREAM_TRANSPORTS = {"tcp", "tls", "ws", "wss"};
static const int SETTLE_TIME_MS = 100;

inline int resolve_single_duration_seconds()
{
    return parse_positive_env("PERF_SINGLE_DURATION_SECONDS", 5);
}

inline int resolve_single_warmup_seconds()
{
    return parse_positive_env("PERF_SINGLE_WARMUP_SECONDS", 2);
}

inline size_t resolve_single_latency_sample_cap()
{
    const int cap =
      parse_positive_env("PERF_SINGLE_LATENCY_SAMPLE_CAP", 200000);
    return cap > 0 ? static_cast<size_t>(cap) : static_cast<size_t>(200000);
}

struct latency_stats_t {
    latency_stats_t() : mean_us(0.0), p95_us(0.0), p99_us(0.0) {}
    double mean_us;
    double p95_us;
    double p99_us;
};

struct queue_stats_t {
    queue_stats_t() :
        snd_pending_max(0.0),
        rcv_pending_max(0.0),
        rcv_pending_end(0.0),
        has_snd_pending(false),
        has_rcv_pending(false)
    {}

    double snd_pending_max;
    double rcv_pending_max;
    double rcv_pending_end;
    bool has_snd_pending;
    bool has_rcv_pending;
};

class latency_stats_builder_t {
public:
    explicit latency_stats_builder_t(
      size_t sample_cap_ = resolve_single_latency_sample_cap()) :
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
        if (slot < static_cast<unsigned long long>(_sample_cap)) {
            _samples[static_cast<size_t>(slot)] = sample;
        }
    }

    unsigned long long count() const { return _count; }

    latency_stats_t snapshot()
    {
        latency_stats_t out;
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
    return parse_positive_env("PERF_IO_THREADS", 2);
}

inline int bench_max_sockets()
{
    return parse_positive_env("PERF_MAX_SOCKETS", 0);
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
            zlink_ctx_term(_ctx);
        }
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
    socket_guard_t(void *ctx_, int type_) :
        _socket(zlink_socket(ctx_, static_cast<zlink_socket_type_t>(type_)))
    {}
    socket_guard_t(void *ctx_, int type_,
                   zlink_socket_msg_handler_fn handler_, void *userdata_ = NULL) :
        _socket(zlink_socket(ctx_, static_cast<zlink_socket_type_t>(type_)))
    {
        if (_socket && handler_
            && zlink_recv_handler(_socket, handler_, userdata_) != 0) {
            zlink_close(_socket);
            _socket = NULL;
        }
    }
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

inline void print_result(const std::string& lib_type,
                         const std::string& pattern,
                         const std::string& transport,
                         size_t size,
                         double throughput,
                         double latency,
                         double latency_p95,
                         double latency_p99) {
    const double bandwidth_mb_s =
      (throughput * static_cast<double>(size)) / 1000000.0;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport << "," << size
              << ",throughput," << std::fixed << std::setprecision(2) << throughput << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport << "," << size
              << ",bandwidth," << std::fixed << std::setprecision(2) << bandwidth_mb_s << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport << "," << size
              << ",latency," << std::fixed << std::setprecision(2) << latency << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport << "," << size
              << ",latency_p95," << std::fixed << std::setprecision(2) << latency_p95 << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport << "," << size
              << ",latency_p99," << std::fixed << std::setprecision(2) << latency_p99 << std::endl;
}

inline void print_queue_metrics(const std::string &lib_type,
                                const std::string &pattern,
                                const std::string &transport,
                                size_t size,
                                const queue_stats_t &queue_stats)
{
    if (queue_stats.has_snd_pending) {
        std::cout << "RESULT," << lib_type << "," << pattern << ","
                  << transport << "," << size << ",snd_pending_max,"
                  << std::fixed << std::setprecision(2)
                  << queue_stats.snd_pending_max << std::endl;
    }
    if (queue_stats.has_rcv_pending) {
        std::cout << "RESULT," << lib_type << "," << pattern << ","
                  << transport << "," << size << ",rcv_pending_max,"
                  << std::fixed << std::setprecision(2)
                  << queue_stats.rcv_pending_max << std::endl;
        std::cout << "RESULT," << lib_type << "," << pattern << ","
                  << transport << "," << size << ",rcv_pending_end,"
                  << std::fixed << std::setprecision(2)
                  << queue_stats.rcv_pending_end << std::endl;
    }
}

inline void print_result(const std::string& lib_type,
                         const std::string& pattern,
                         const std::string& transport,
                         size_t size,
                         double throughput,
                         double latency,
                         double latency_p95,
                         double latency_p99,
                         const queue_stats_t &queue_stats) {
    print_result(
      lib_type, pattern, transport, size, throughput, latency, latency_p95,
      latency_p99);
    print_queue_metrics(lib_type, pattern, transport, size, queue_stats);
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

struct connect_monitor_state_t {
    connect_monitor_state_t() :
        connection_ready_count(0),
        error_code(0)
    {}

    std::mutex sync;
    std::condition_variable cv;
    size_t connection_ready_count;
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
    return state_->connection_ready_count;
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

    zlink_socket_monitor_open_options_t monitor_opts;
    memset(&monitor_opts, 0, sizeof(monitor_opts));
    monitor_opts.events =
      ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_BIND_FAILED
      | ZLINK_EVENT_ACCEPT_FAILED | ZLINK_EVENT_CLOSE_FAILED
      | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
      | ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
      | ZLINK_EVENT_HANDSHAKE_FAILED_AUTH;
    void *monitor = zlink_socket_monitor_open(socket_, &monitor_opts);
    if (!monitor) {
        delete state;
        return false;
    }
    if (zlink_socket_monitor_handler(monitor, &connect_monitor_handler, state)
        != 0) {
        zlink_monitor_close(&monitor);
        delete state;
        return false;
    }

    const int monitor_hwm = parse_positive_env("PERF_MONITOR_HWM", 1000);
    set_sockopt_int(monitor, ZLINK_OPT_LINGER, 0, "ZLINK_OPT_LINGER");
    if (monitor_hwm > 0) {
        set_sockopt_int(monitor, ZLINK_OPT_SNDHWM, monitor_hwm,
                        "ZLINK_OPT_SNDHWM");
        set_sockopt_int(monitor, ZLINK_OPT_RCVHWM, monitor_hwm,
                        "ZLINK_OPT_RCVHWM");
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
    (void) zlink_monitor_close(&monitor);
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
      std::chrono::milliseconds(bounded_timeout),
      [&monitor_, expected_ready_]() {
          return monitor_.state->error_code != 0
                 || connect_ready_count(monitor_.state) >= expected_ready_;
      });
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
        std::cerr << "[perf-single] monitor close failed";
        if (monitor)
            std::cerr << ": " << zlink_strerror(zlink_errno());
        std::cerr << std::endl;
    }
}

inline std::string resolve_single_perf_recv_mode()
{
    const char *env = std::getenv("PERF_RECV_MODE");
    if (!env || !*env)
        return "recv";

    std::string mode(env);
    std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
    if (mode != "recv" && mode != "callback")
        return "recv";
    return mode;
}

inline bool single_perf_callback_mode()
{
    return resolve_single_perf_recv_mode() == "callback";
}

inline int resolve_single_send_timeout_ms()
{
    return parse_positive_env("PERF_SINGLE_SNDTIMEO_MS", 200);
}

inline int resolve_single_recv_timeout_ms()
{
    return parse_positive_env("PERF_SINGLE_RCVTIMEO_MS", 200);
}

inline int resolve_single_pubsub_recv_timeout_ms()
{
    return parse_positive_env("PERF_SINGLE_PUBSUB_RCVTIMEO_MS",
                              resolve_single_recv_timeout_ms());
}

inline int resolve_single_socket_hwm(bool send_)
{
    const int base_hwm = parse_positive_env("PERF_SINGLE_HWM", 1000);
    return send_ ? parse_positive_env("PERF_SINGLE_SNDHWM", base_hwm)
                 : parse_positive_env("PERF_SINGLE_RCVHWM", base_hwm);
}

inline int resolve_single_queue_sample_ms()
{
    return parse_positive_env("PERF_SINGLE_QUEUE_SAMPLE_MS", 100);
}

inline int resolve_single_queue_sample_every_msgs()
{
    return parse_positive_env("PERF_SINGLE_QUEUE_SAMPLE_EVERY_MSGS", 64);
}

class queue_probe_t {
public:
    queue_probe_t(void *send_socket_, void *recv_socket_) :
        _send_socket(send_socket_),
        _recv_socket(recv_socket_),
        _sample_interval_ns(resolve_sample_interval_ns()),
        _sample_every_msgs(resolve_sample_every_msgs()),
        _send_last_sample_ns(0),
        _recv_last_sample_ns(0),
        _send_msgs_since_sample(0),
        _recv_msgs_since_sample(0),
        _send_total(0),
        _recv_total(0),
        _snd_pending_max(0),
        _rcv_pending_max(0),
        _rcv_pending_end(0),
        _snd_seen(false),
        _rcv_seen(false)
    {}

    void sample_send_if_due() { maybe_sample_send(false); }
    void sample_recv_if_due() { maybe_sample_recv(false); }
    void force_sample_send() { maybe_sample_send(true); }
    void force_sample_recv() { maybe_sample_recv(true); }
    queue_stats_t snapshot() const
    {
        queue_stats_t out;
        if (_snd_seen) {
            out.has_snd_pending = true;
            out.snd_pending_max = static_cast<double>(_snd_pending_max);
        }
        if (_rcv_seen) {
            out.has_rcv_pending = true;
            out.rcv_pending_max = static_cast<double>(_rcv_pending_max);
            out.rcv_pending_end = static_cast<double>(_rcv_pending_end);
        }
        return out;
    }

private:
    static unsigned long long resolve_sample_interval_ns()
    {
        const int sample_ms = resolve_single_queue_sample_ms();
        const unsigned long long clamped_ms =
          static_cast<unsigned long long>(sample_ms > 0 ? sample_ms : 100);
        return clamped_ms * 1000000ULL;
    }

    static unsigned int resolve_sample_every_msgs()
    {
        const int value = resolve_single_queue_sample_every_msgs();
        return static_cast<unsigned int>(value > 0 ? value : 64);
    }

    static unsigned long long now_ns()
    {
        return static_cast<unsigned long long>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    }

    void maybe_sample_send(bool force_)
    {
        if (!_send_socket)
            return;

        if (!force_)
            ++_send_total;

        if (force_) {
            _send_msgs_since_sample = 0;
        } else if (_sample_every_msgs > 1) {
            ++_send_msgs_since_sample;
            if (_send_msgs_since_sample < _sample_every_msgs)
                return;
            _send_msgs_since_sample = 0;
        }

        const unsigned long long now = now_ns();
        if (!force_ && _send_last_sample_ns > 0
            && now - _send_last_sample_ns < _sample_interval_ns) {
            return;
        }
        _send_last_sample_ns = now;

        const unsigned long long pending =
          _send_total > _recv_total ? (_send_total - _recv_total) : 0ULL;
        if (!_snd_seen || pending > _snd_pending_max)
            _snd_pending_max = pending;
        _snd_seen = true;
    }

    void maybe_sample_recv(bool force_)
    {
        if (!_recv_socket)
            return;

        if (!force_)
            ++_recv_total;

        if (force_) {
            _recv_msgs_since_sample = 0;
        } else if (_sample_every_msgs > 1) {
            ++_recv_msgs_since_sample;
            if (_recv_msgs_since_sample < _sample_every_msgs)
                return;
            _recv_msgs_since_sample = 0;
        }

        const unsigned long long now = now_ns();
        if (!force_ && _recv_last_sample_ns > 0
            && now - _recv_last_sample_ns < _sample_interval_ns) {
            return;
        }
        _recv_last_sample_ns = now;

        const unsigned long long pending =
          _send_total > _recv_total ? (_send_total - _recv_total) : 0ULL;
        if (!_rcv_seen || pending > _rcv_pending_max)
            _rcv_pending_max = pending;
        _rcv_pending_end = pending;
        _rcv_seen = true;
    }

    void *_send_socket;
    void *_recv_socket;
    unsigned long long _sample_interval_ns;
    unsigned int _sample_every_msgs;
    unsigned long long _send_last_sample_ns;
    unsigned long long _recv_last_sample_ns;
    unsigned int _send_msgs_since_sample;
    unsigned int _recv_msgs_since_sample;
    unsigned long long _send_total;
    unsigned long long _recv_total;
    unsigned long long _snd_pending_max;
    unsigned long long _rcv_pending_max;
    unsigned long long _rcv_pending_end;
    bool _snd_seen;
    bool _rcv_seen;

    queue_probe_t(const queue_probe_t &);
    queue_probe_t &operator=(const queue_probe_t &);
};

inline queue_stats_t sample_queue_stats(queue_probe_t *queue_probe_)
{
    if (!queue_probe_)
        return queue_stats_t();
    queue_probe_->force_sample_send();
    queue_probe_->force_sample_recv();
    return queue_probe_->snapshot();
}

inline void print_fail_result(const std::string &lib_type,
                              const std::string &pattern,
                              const std::string &transport,
                              size_t size,
                              queue_probe_t *queue_probe_ = NULL)
{
    if (!queue_probe_)
        return;
    const queue_stats_t queue_stats = sample_queue_stats(queue_probe_);
    print_queue_metrics(lib_type, pattern, transport, size, queue_stats);
}

inline void apply_single_hwm(void *socket_)
{
    if (!socket_)
        return;

    const int sndhwm = resolve_single_socket_hwm(true);
    const int rcvhwm = resolve_single_socket_hwm(false);
    set_sockopt_int(socket_, ZLINK_OPT_SNDHWM, sndhwm, "ZLINK_OPT_SNDHWM");
    set_sockopt_int(socket_, ZLINK_OPT_RCVHWM, rcvhwm, "ZLINK_OPT_RCVHWM");
}

inline void apply_single_benchmark_socket_options(void *socket_,
                                                  const std::string &transport_)
{
    if (!socket_)
        return;
    if (transport_ == "pgm" || transport_ == "epgm")
        return;

    const int linger_ms = 0;
    const int sndtimeo_ms = resolve_single_send_timeout_ms();
    const int rcvtimeo_ms = resolve_single_recv_timeout_ms();
    set_sockopt_int(socket_, ZLINK_OPT_LINGER, linger_ms, "ZLINK_OPT_LINGER");
    set_sockopt_int(socket_, ZLINK_OPT_SNDTIMEO, sndtimeo_ms,
                    "ZLINK_OPT_SNDTIMEO");
    set_sockopt_int(socket_, ZLINK_OPT_RCVTIMEO, rcvtimeo_ms,
                    "ZLINK_OPT_RCVTIMEO");
}

inline void apply_debug_timeouts(void *socket_, const std::string &transport) {
    if (!bench_debug_enabled())
        return;
    if (transport == "tcp" || transport == "ws") {
        const int timeout_ms = 2000;
        set_sockopt_int(socket_, ZLINK_OPT_SNDTIMEO, timeout_ms,
                        "ZLINK_OPT_SNDTIMEO");
        set_sockopt_int(socket_, ZLINK_OPT_RCVTIMEO, timeout_ms,
                        "ZLINK_OPT_RCVTIMEO");
    }
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
        if (zlink_get_option(socket_, ZLINK_OPT_LAST_ENDPOINT, last_endpoint,
                             &size)
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
                if (pos != std::string::npos) {
                    endpoint.replace(pos, tcp_ipv6_any.size(), "://127.0.0.1:");
                }
            }
        }
        if (bench_debug_enabled()) {
            std::cerr << "Resolved endpoint (" << transport << "): " << endpoint << std::endl;
        }
    }
    return endpoint;
}

inline bool transport_available(const std::string& transport) {
    if (transport == "pgm" || transport == "epgm") return false;
    if (transport == "ipc") return zlink_has("ipc") != 0;
    if (transport == "tls") return zlink_has("tls") != 0;
    if (transport == "ws") return zlink_has("ws") != 0;
    if (transport == "wss") return zlink_has("wss") != 0;
    return true;
}

inline bool connect_checked(void *socket_, const std::string& endpoint) {
    if (zlink_connect(socket_, endpoint.c_str()) != 0) {
        std::cerr << "connect failed for " << endpoint << ": "
                  << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }
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

    apply_single_hwm(bind_socket_);
    apply_single_hwm(connect_socket_);

    std::string endpoint =
      bind_and_resolve_endpoint(bind_socket_, transport_, id_);
    if (endpoint.empty())
        return false;

    connect_monitor_t bind_monitor;
    connect_monitor_t connect_monitor;
    if (!open_connect_monitor(bind_socket_, bind_monitor))
        return false;
    if (!open_connect_monitor(connect_socket_, connect_monitor)) {
        close_connect_monitor(bind_monitor);
        return false;
    }

    if (!connect_checked(connect_socket_, endpoint))
    {
        close_connect_monitor(connect_monitor);
        close_connect_monitor(bind_monitor);
        return false;
    }

    apply_single_benchmark_socket_options(bind_socket_, transport_);
    apply_single_benchmark_socket_options(connect_socket_, transport_);

    const int timeout_ms = parse_positive_env("PERF_CONNECT_READY_TIMEOUT_MS",
                                              3000);
    const bool bind_ready =
      wait_connect_ready_count(bind_monitor, 1, timeout_ms);
    const bool connect_ready =
      wait_connect_ready_count(connect_monitor, 1, timeout_ms);

    close_connect_monitor(connect_monitor);
    close_connect_monitor(bind_monitor);
    if (bench_debug_enabled() && !(bind_ready && connect_ready)) {
        std::cerr << "[perf-single] setup_connected_pair failed:"
                  << " bind_ready=" << (bind_ready ? 1 : 0)
                  << " connect_ready=" << (connect_ready ? 1 : 0)
                  << std::endl;
    }
    return bind_ready && connect_ready;
}

template <typename RunFn>
inline int run_standard_bench_main(int argc_, char **argv_, RunFn run_) {
    if (argc_ < 4)
        return 1;
    std::string lib_name = argv_[1];
    std::string transport = argv_[2];
    size_t size = std::stoul(argv_[3]);
    run_(transport, size, lib_name);
    return 0;
}

#endif
