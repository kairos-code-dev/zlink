#ifndef PERF_COMMON_HPP
#define PERF_COMMON_HPP

#include "../../common/perf_infra.hpp"
#include "perf_single_metric_header.hpp"

#include <chrono>
#include <condition_variable>
#include <vector>
#include <string>
#include <algorithm>
#include <atomic>
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

struct single_callback_metric_event_t
{
    single_callback_metric_event_t () : phase (0), sent_ts_us (0) {}

    uint32_t phase;
    uint64_t sent_ts_us;
};

class single_callback_metric_queue_t
{
  public:
    explicit single_callback_metric_queue_t (size_t capacity_) :
        _events (capacity_ > 1 ? capacity_ + 1 : 2),
        _head (0),
        _tail (0)
    {
    }

    bool push (const single_callback_metric_event_t &event_)
    {
        const size_t head = _head.load (std::memory_order_relaxed);
        const size_t next = advance (head);
        if (next == _tail.load (std::memory_order_acquire))
            return false;
        _events[head] = event_;
        _head.store (next, std::memory_order_release);
        return true;
    }

    bool pop (single_callback_metric_event_t *event_)
    {
        if (!event_)
            return false;
        const size_t tail = _tail.load (std::memory_order_relaxed);
        if (tail == _head.load (std::memory_order_acquire))
            return false;
        *event_ = _events[tail];
        _tail.store (advance (tail), std::memory_order_release);
        return true;
    }

    bool empty () const
    {
        return _tail.load (std::memory_order_acquire)
               == _head.load (std::memory_order_acquire);
    }

  private:
    size_t advance (size_t index_) const
    {
        return index_ + 1 < _events.size () ? index_ + 1 : 0;
    }

    std::vector<single_callback_metric_event_t> _events;
    std::atomic<size_t> _head;
    std::atomic<size_t> _tail;
};

inline void single_increment_counter (std::atomic<unsigned long long> &counter_)
{
    counter_.fetch_add (1, std::memory_order_acq_rel);
}

inline void single_increment_counter (unsigned long long &counter_)
{
    ++counter_;
}

inline unsigned long long single_load_counter (
  const std::atomic<unsigned long long> &counter_)
{
    return counter_.load (std::memory_order_acquire);
}

inline unsigned long long single_load_counter (
  const unsigned long long &counter_)
{
    return counter_;
}

inline uint64_t single_load_deadline (
  const std::atomic<uint64_t> &deadline_)
{
    return deadline_.load (std::memory_order_acquire);
}

inline uint64_t single_load_deadline (const uint64_t &deadline_)
{
    return deadline_;
}

inline bool single_load_flag (const std::atomic<bool> &flag_)
{
    return flag_.load (std::memory_order_acquire);
}

inline bool single_load_flag (const bool &flag_)
{
    return flag_;
}

inline void single_store_flag (std::atomic<bool> &flag_, bool value_)
{
    flag_.store (value_, std::memory_order_release);
}

inline void single_store_flag (bool &flag_, bool value_)
{
    flag_ = value_;
}

template <typename StateT>
inline void single_notify_metric_waiters (StateT *state_)
{
    if (!state_)
        return;
    state_->callback_wait_cv.notify_all ();
    state_->cv.notify_all ();
}

template <typename StateT>
inline void single_mark_callback_fatal (StateT *state_)
{
    if (!state_)
        return;
    single_store_flag (state_->fatal, true);
    single_notify_metric_waiters (state_);
}

template <typename StateT>
inline bool single_enqueue_metric_event (
  StateT *state_,
  const perf_single_metric::header_t &header_)
{
    if (!state_)
        return false;
    if (!state_->callback_queue)
        return false;

    single_callback_metric_event_t event;
    event.phase = header_.phase;
    event.sent_ts_us = header_.sent_ts_us;
    if (!state_->callback_queue->push (event)) {
        single_mark_callback_fatal (state_);
        return false;
    }
    single_increment_counter (state_->callback_enqueued);
    state_->callback_wait_cv.notify_one ();
    return true;
}

template <typename StateT>
inline void single_note_callback_receive (StateT *state_)
{
    if (!state_)
        return;
    if (state_->probe)
        state_->probe->sample_recv_if_due ();
    state_->recv_activity.fetch_add (1, std::memory_order_acq_rel);
    state_->cv.notify_all ();
}

template <typename StateT>
inline void single_account_metric_event (
  StateT *state_,
  const single_callback_metric_event_t &event_)
{
    if (!state_)
        return;

    if (event_.phase
        == static_cast<uint32_t> (perf_single_metric::phase_warmup)) {
        single_increment_counter (state_->warmup_received);
    } else if (event_.phase
               == static_cast<uint32_t> (
                 perf_single_metric::phase_active)) {
        const uint64_t now_us = perf_single_metric::now_us ();
        single_increment_counter (state_->active_received);
        const double latency_us =
          now_us >= event_.sent_ts_us
            ? static_cast<double> (now_us - event_.sent_ts_us)
            : 0.0;
        std::lock_guard<std::mutex> lock (state_->latency_mutex);
        state_->latency.add (latency_us);
    }

    single_increment_counter (state_->callback_processed);
    state_->callback_wait_cv.notify_all ();
    state_->cv.notify_all ();
}

template <typename StateT>
inline bool single_wait_for_receive_quiet (StateT &state_,
                                           int idle_timeout_ms_,
                                           int total_timeout_ms_)
{
    const auto total_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (total_timeout_ms_ > 0 ? total_timeout_ms_
                                                         : 1);
    auto idle_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (idle_timeout_ms_ > 0 ? idle_timeout_ms_ : 1);
    unsigned long long observed =
      state_.recv_activity.load (std::memory_order_acquire);

    std::unique_lock<std::mutex> lock (state_.mutex);
    while (std::chrono::steady_clock::now () < total_deadline) {
        const auto wait_deadline =
          idle_deadline < total_deadline ? idle_deadline : total_deadline;
        const bool changed = state_.cv.wait_until (
          lock, wait_deadline, [&state_, observed] () {
              return state_.recv_activity.load (std::memory_order_acquire)
                       != observed
                     || single_load_flag (state_.fatal)
                     || (state_.callback_queue
                         && !state_.callback_queue->empty ());
          });
        if (single_load_flag (state_.fatal))
            return false;
        if (!changed)
            return true;

        observed = state_.recv_activity.load (std::memory_order_acquire);
        idle_deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (idle_timeout_ms_ > 0 ? idle_timeout_ms_
                                                            : 1);
    }

    return !single_load_flag (state_.fatal);
}

template <typename StateT>
inline bool single_wait_for_metric_worker_idle (StateT &state_,
                                                int timeout_ms_)
{
    if (!state_.callback_queue)
        return true;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);
    std::unique_lock<std::mutex> lock (state_.callback_wait_mutex);
    while (std::chrono::steady_clock::now () < deadline) {
        if (single_load_flag (state_.fatal))
            return false;

        const unsigned long long enqueued =
          single_load_counter (state_.callback_enqueued);
        const unsigned long long processed =
          single_load_counter (state_.callback_processed);
        if (processed >= enqueued && state_.callback_queue->empty ())
            return true;

        const bool woke = state_.callback_wait_cv.wait_until (
          lock, deadline, [&state_]() {
              return single_load_flag (state_.fatal)
                     || (single_load_counter (state_.callback_processed)
                           >= single_load_counter (
                             state_.callback_enqueued)
                         && state_.callback_queue->empty ());
          });
        if (woke)
            return !single_load_flag (state_.fatal);
    }

    return single_load_flag (state_.fatal)
             || (single_load_counter (state_.callback_processed)
                   >= single_load_counter (state_.callback_enqueued)
                 && state_.callback_queue->empty ());
}

template <typename StateT>
struct single_metric_worker_t
{
    single_metric_worker_t () : state (NULL), queue (NULL), stop (false) {}

    StateT *state;
    single_callback_metric_queue_t *queue;
    std::atomic<bool> stop;
    std::thread thread;
};

template <typename StateT>
inline bool start_single_metric_worker (single_metric_worker_t<StateT> *worker_)
{
    if (!worker_ || !worker_->state || !worker_->queue)
        return false;

    worker_->stop.store (false, std::memory_order_release);
    worker_->thread = std::thread ([worker_]() {
        single_callback_metric_event_t event;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock (
                  worker_->state->callback_wait_mutex);
                worker_->state->callback_wait_cv.wait (
                  lock, [worker_]() {
                      return worker_->stop.load (std::memory_order_acquire)
                             || single_load_flag (worker_->state->fatal)
                             || !worker_->queue->empty ();
                  });
            }

            while (worker_->queue->pop (&event))
                single_account_metric_event (worker_->state, event);

            if (worker_->stop.load (std::memory_order_acquire)
                || (single_load_flag (worker_->state->fatal)
                    && worker_->queue->empty ())) {
                break;
            }
        }

        worker_->state->cv.notify_all ();
    });
    return true;
}

template <typename StateT>
inline void stop_single_metric_worker (single_metric_worker_t<StateT> *worker_)
{
    if (!worker_)
        return;
    worker_->stop.store (true, std::memory_order_release);
    if (worker_->state)
        worker_->state->callback_wait_cv.notify_all ();
    if (worker_->thread.joinable ())
        worker_->thread.join ();
}

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

class poller_guard_t {
public:
    poller_guard_t() : _poller(zlink_poller_new()) {}
    ~poller_guard_t() {
        if (_poller)
            (void) zlink_poller_destroy(&_poller);
    }

    bool valid() const { return _poller != NULL; }
    void *get() const { return _poller; }

    bool add(void *socket_, void *user_data_, short events_)
    {
        return _poller
               && zlink_poller_add(_poller, socket_, user_data_, events_) == 0;
    }

    int wait(zlink_poller_event_t *event_, int timeout_ms_)
    {
        if (!_poller)
            return -1;
        return zlink_poller_wait(_poller, event_, timeout_ms_);
    }

private:
    poller_guard_t(const poller_guard_t &);
    poller_guard_t &operator=(const poller_guard_t &);

    void *_poller;
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

struct readiness_monitor_state_t {
    readiness_monitor_state_t() :
        ready(false),
        error_code(0),
        ready_event(0)
    {}

    std::mutex sync;
    std::condition_variable cv;
    bool ready;
    int error_code;
    uint64_t ready_event;
};

struct readiness_monitor_t {
    readiness_monitor_t() : owner(NULL), monitor(NULL), state(NULL) {}

    void *owner;
    void *monitor;
    readiness_monitor_state_t *state;
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

inline void readiness_monitor_handler(const zlink_monitor_event_t *event_,
                                      void *userdata_)
{
    readiness_monitor_state_t *state =
      static_cast<readiness_monitor_state_t *>(userdata_);
    if (!state || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock(state->sync);
        switch (event_->event) {
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
                if (event_->event == state->ready_event)
                    state->ready = event_->value > 0;
                break;
        }
    }
    state->cv.notify_all();
}

inline bool open_socket_readiness_monitor(void *socket_,
                                          uint64_t ready_event_,
                                          readiness_monitor_t &out_)
{
    out_.owner = socket_;
    out_.monitor = NULL;
    out_.state = NULL;
    if (!socket_ || ready_event_ == 0)
        return false;

    readiness_monitor_state_t *state =
      new (std::nothrow) readiness_monitor_state_t();
    if (!state)
        return false;
    state->ready_event = ready_event_;

    zlink_socket_monitor_open_options_t monitor_opts;
    memset(&monitor_opts, 0, sizeof(monitor_opts));
    monitor_opts.events =
      ready_event_ | ZLINK_EVENT_BIND_FAILED | ZLINK_EVENT_ACCEPT_FAILED
      | ZLINK_EVENT_CLOSE_FAILED | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
      | ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
      | ZLINK_EVENT_HANDSHAKE_FAILED_AUTH;
    void *monitor = zlink_socket_monitor_open(socket_, &monitor_opts);
    if (!monitor) {
        delete state;
        return false;
    }
    if (zlink_socket_monitor_handler(monitor, &readiness_monitor_handler, state)
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

inline bool wait_socket_readiness(readiness_monitor_t &monitor_, int timeout_ms_)
{
    if (!monitor_.state)
        return false;

    std::unique_lock<std::mutex> lock(monitor_.state->sync);
    if (monitor_.state->error_code != 0)
        return false;
    if (monitor_.state->ready)
        return true;

    const int bounded_timeout = timeout_ms_ > 0 ? timeout_ms_ : 1;
    const bool signaled = monitor_.state->cv.wait_for(
      lock,
      std::chrono::milliseconds(bounded_timeout),
      [&monitor_]() {
          return monitor_.state->error_code != 0 || monitor_.state->ready;
      });
    return signaled && monitor_.state->error_code == 0
           && monitor_.state->ready;
}

inline void close_socket_readiness_monitor(readiness_monitor_t &monitor_)
{
    readiness_monitor_state_t *state = monitor_.state;
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
        std::cerr << "[perf-single] readiness monitor close failed" << std::endl;
    }
}

struct service_event_probe_state_t {
    service_event_probe_state_t() : error_event(0), error_code(0) {}

    uint32_t error_event;
    int error_code;
    std::mutex sync;
    std::condition_variable cv;
    std::vector<zlink_service_event_t> events;
};

struct service_event_probe_t {
    service_event_probe_t() : owner(NULL), monitor(NULL), state(NULL) {}

    void *owner;
    void *monitor;
    service_event_probe_state_t *state;
};

inline void service_event_probe_handler(const zlink_service_event_t *event_,
                                        void *userdata_)
{
    service_event_probe_state_t *state =
      static_cast<service_event_probe_state_t *>(userdata_);
    if (!state || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock(state->sync);
        if (state->error_event != 0
            && event_->event_type == state->error_event
            && state->error_code == 0) {
            state->error_code =
              event_->error_code != 0 ? event_->error_code : EIO;
        }
        state->events.push_back(*event_);
    }
    state->cv.notify_all();
}

inline bool open_service_event_probe(void *service_,
                                     uint64_t events_,
                                     uint32_t error_event_,
                                     service_event_probe_t &out_)
{
    out_.owner = service_;
    out_.monitor = NULL;
    out_.state = NULL;
    if (!service_ || events_ == 0)
        return false;

    service_event_probe_state_t *state =
      new (std::nothrow) service_event_probe_state_t();
    if (!state)
        return false;
    state->error_event = error_event_;

    zlink_service_monitor_open_options_t monitor_opts;
    memset(&monitor_opts, 0, sizeof(monitor_opts));
    monitor_opts.events = events_;
    void *monitor = zlink_service_monitor_open(service_, &monitor_opts);
    if (!monitor) {
        delete state;
        return false;
    }
    if (zlink_service_monitor_handler(monitor, &service_event_probe_handler, state)
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

inline bool consume_matching_service_event_locked(
  service_event_probe_state_t *state_,
  uint32_t expected_event_type_,
  const char *endpoint_prefix_,
  const char *subject_,
  int min_value_)
{
    if (!state_)
        return false;

    for (std::vector<zlink_service_event_t>::iterator it =
           state_->events.begin();
         it != state_->events.end(); ++it) {
        if (it->event_type != expected_event_type_)
            continue;
        if (endpoint_prefix_ && endpoint_prefix_[0] != '\0') {
            if ((it->detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT) == 0)
                continue;
            if (std::strncmp(it->endpoint, endpoint_prefix_,
                             std::strlen(endpoint_prefix_))
                != 0) {
                continue;
            }
        }
        if (subject_ && subject_[0] != '\0') {
            if ((it->detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) == 0)
                continue;
            if (std::strcmp(it->subject, subject_) != 0)
                continue;
        }
        if (min_value_ >= 0 && static_cast<int>(it->value) < min_value_)
            continue;
        state_->events.erase(it);
        return true;
    }

    return false;
}

inline bool wait_for_service_event(service_event_probe_t &probe_,
                                   uint32_t expected_event_type_,
                                   const char *endpoint_prefix_,
                                   const char *subject_,
                                   int min_value_,
                                   int timeout_ms_)
{
    if (!probe_.state)
        return false;

    std::unique_lock<std::mutex> lock(probe_.state->sync);
    if (probe_.state->error_code != 0)
        return false;
    if (consume_matching_service_event_locked(probe_.state,
                                              expected_event_type_,
                                              endpoint_prefix_,
                                              subject_,
                                              min_value_)) {
        return true;
    }

    const bool signaled = probe_.state->cv.wait_for(
      lock,
      std::chrono::milliseconds(timeout_ms_ > 0 ? timeout_ms_ : 1),
      [&probe_, expected_event_type_, endpoint_prefix_, subject_, min_value_]() {
          return probe_.state->error_code != 0
                 || consume_matching_service_event_locked(
                      probe_.state,
                      expected_event_type_,
                      endpoint_prefix_,
                      subject_,
                      min_value_);
      });
    return signaled && probe_.state->error_code == 0;
}

inline void close_service_event_probe(service_event_probe_t &probe_)
{
    service_event_probe_state_t *state = probe_.state;
    void *owner = probe_.owner;
    void *monitor = probe_.monitor;
    probe_.owner = NULL;
    probe_.monitor = NULL;
    probe_.state = NULL;

    if (!monitor && !state)
        return;

    if (monitor)
        stop_and_close_socket_monitor(owner, &monitor);
    delete state;
}

struct service_flag_monitor_state_t {
    service_flag_monitor_state_t() :
        ready_event(0), error_event(0), ready(false), error_code(0) {}

    uint32_t ready_event;
    uint32_t error_event;
    bool ready;
    int error_code;
    std::mutex sync;
    std::condition_variable cv;
};

struct service_flag_monitor_t {
    service_flag_monitor_t() : owner(NULL), monitor(NULL), state(NULL) {}

    void *owner;
    void *monitor;
    service_flag_monitor_state_t *state;
};

inline void service_flag_monitor_handler(const zlink_service_event_t *event_,
                                         void *userdata_)
{
    service_flag_monitor_state_t *state =
      static_cast<service_flag_monitor_state_t *>(userdata_);
    if (!state || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock(state->sync);
        if (event_->event_type == state->ready_event) {
            state->ready = event_->value > 0;
        } else if (event_->event_type == state->error_event
                   && state->error_code == 0) {
            state->error_code =
              event_->error_code != 0 ? event_->error_code : EIO;
        }
    }
    state->cv.notify_all();
}

inline bool open_service_flag_monitor(void *service_,
                                      uint64_t events_,
                                      uint32_t ready_event_,
                                      uint32_t error_event_,
                                      service_flag_monitor_t &out_)
{
    out_.owner = service_;
    out_.monitor = NULL;
    out_.state = NULL;
    if (!service_ || events_ == 0 || ready_event_ == 0)
        return false;

    service_flag_monitor_state_t *state =
      new (std::nothrow) service_flag_monitor_state_t();
    if (!state)
        return false;
    state->ready_event = ready_event_;
    state->error_event = error_event_;

    zlink_service_monitor_open_options_t monitor_opts;
    memset(&monitor_opts, 0, sizeof(monitor_opts));
    monitor_opts.events = events_;
    void *monitor = zlink_service_monitor_open(service_, &monitor_opts);
    if (!monitor) {
        delete state;
        return false;
    }
    if (zlink_service_monitor_handler(monitor, &service_flag_monitor_handler, state)
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

inline bool wait_service_flag_ready(service_flag_monitor_t &monitor_,
                                    int timeout_ms_)
{
    if (!monitor_.state)
        return false;

    std::unique_lock<std::mutex> lock(monitor_.state->sync);
    if (monitor_.state->error_code != 0)
        return false;
    if (monitor_.state->ready)
        return true;

    const bool signaled = monitor_.state->cv.wait_for(
      lock,
      std::chrono::milliseconds(timeout_ms_ > 0 ? timeout_ms_ : 1),
      [&monitor_]() {
          return monitor_.state->error_code != 0 || monitor_.state->ready;
      });
    return signaled && monitor_.state->error_code == 0
           && monitor_.state->ready;
}

inline bool wait_service_flag_stable(service_flag_monitor_t &monitor_,
                                     int settle_ms_)
{
    if (settle_ms_ <= 0)
        return true;
    if (!monitor_.state)
        return false;

    std::unique_lock<std::mutex> lock(monitor_.state->sync);
    if (monitor_.state->error_code != 0 || !monitor_.state->ready)
        return false;

    const std::chrono::steady_clock::time_point settle_deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(settle_ms_);
    while (std::chrono::steady_clock::now() < settle_deadline) {
        if (monitor_.state->cv.wait_until(
              lock, settle_deadline,
              [&monitor_]() { return monitor_.state->error_code != 0; })) {
            return false;
        }
    }

    return monitor_.state->error_code == 0 && monitor_.state->ready;
}

inline void close_service_flag_monitor(service_flag_monitor_t &monitor_)
{
    service_flag_monitor_state_t *state = monitor_.state;
    void *owner = monitor_.owner;
    void *monitor = monitor_.monitor;
    monitor_.owner = NULL;
    monitor_.monitor = NULL;
    monitor_.state = NULL;

    if (!monitor && !state)
        return;

    if (monitor)
        stop_and_close_socket_monitor(owner, &monitor);
    delete state;
}

inline std::string resolve_single_perf_recv_mode()
{
    const char *env = std::getenv("PERF_RECV_MODE");
    if (!env || !*env)
        return "callback";

    std::string mode(env);
    std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
    if (mode != "recv" && mode != "callback")
        return "callback";
    return mode;
}

inline bool single_perf_callback_mode()
{
    return resolve_single_perf_recv_mode() == "callback";
}

inline bool single_perf_callback_supported_for_pattern(const char *pattern)
{
    return pattern && *pattern;
}

inline bool single_perf_recv_supported_for_pattern(const char *pattern)
{
    return pattern && std::string(pattern) == "SPOT";
}

inline bool single_perf_validate_recv_mode_for_pattern(const char *pattern)
{
    if (!pattern || !*pattern)
        return false;

    const std::string mode = resolve_single_perf_recv_mode();
    if (mode == "callback"
        && !single_perf_callback_supported_for_pattern(pattern)) {
        std::cerr << "policy violation: --recv callback unsupported for "
                  << pattern << std::endl;
        return false;
    }

    if (mode == "recv" && !single_perf_recv_supported_for_pattern(pattern)) {
        std::cerr << "policy violation: --recv recv unsupported for "
                  << pattern << std::endl;
        return false;
    }

    return true;
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
    queue_stats_t snapshot()
    {
        force_sample_send();
        force_sample_recv();
        queue_stats_t out;
        if (_snd_seen.load(std::memory_order_acquire)) {
            out.has_snd_pending = true;
            out.snd_pending_max = static_cast<double>(
              _snd_pending_max.load(std::memory_order_acquire));
        }
        if (_rcv_seen.load(std::memory_order_acquire)) {
            out.has_rcv_pending = true;
            out.rcv_pending_max = static_cast<double>(
              _rcv_pending_max.load(std::memory_order_acquire));
            out.rcv_pending_end = static_cast<double>(
              _rcv_pending_end.load(std::memory_order_acquire));
        }
        return out;
    }

    unsigned long long pending_count() const
    {
        const unsigned long long send_total =
          _send_total.load(std::memory_order_acquire);
        const unsigned long long recv_total =
          _recv_total.load(std::memory_order_acquire);
        return send_total > recv_total ? (send_total - recv_total) : 0ULL;
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
            _send_total.fetch_add(1, std::memory_order_acq_rel);

        if (force_) {
            _send_msgs_since_sample.store(0, std::memory_order_release);
        } else if (_sample_every_msgs > 1) {
            const unsigned int sampled =
              _send_msgs_since_sample.fetch_add(1, std::memory_order_acq_rel)
              + 1;
            if (sampled < _sample_every_msgs)
                return;
            _send_msgs_since_sample.store(0, std::memory_order_release);
        }

        const unsigned long long now = now_ns();
        const unsigned long long last_sample_ns =
          _send_last_sample_ns.load(std::memory_order_acquire);
        if (!force_ && last_sample_ns > 0
            && now - last_sample_ns < _sample_interval_ns) {
            return;
        }
        _send_last_sample_ns.store(now, std::memory_order_release);

        const unsigned long long pending = pending_count();
        unsigned long long current_max =
          _snd_pending_max.load(std::memory_order_acquire);
        while (pending > current_max
               && !_snd_pending_max.compare_exchange_weak(
                 current_max, pending, std::memory_order_acq_rel)) {
        }
        _snd_seen.store(true, std::memory_order_release);
    }

    void maybe_sample_recv(bool force_)
    {
        if (!_recv_socket)
            return;

        if (!force_)
            _recv_total.fetch_add(1, std::memory_order_acq_rel);

        if (force_) {
            _recv_msgs_since_sample.store(0, std::memory_order_release);
        } else if (_sample_every_msgs > 1) {
            const unsigned int sampled =
              _recv_msgs_since_sample.fetch_add(1, std::memory_order_acq_rel)
              + 1;
            if (sampled < _sample_every_msgs)
                return;
            _recv_msgs_since_sample.store(0, std::memory_order_release);
        }

        const unsigned long long now = now_ns();
        const unsigned long long last_sample_ns =
          _recv_last_sample_ns.load(std::memory_order_acquire);
        if (!force_ && last_sample_ns > 0
            && now - last_sample_ns < _sample_interval_ns) {
            return;
        }
        _recv_last_sample_ns.store(now, std::memory_order_release);

        const unsigned long long pending = pending_count();
        unsigned long long current_max =
          _rcv_pending_max.load(std::memory_order_acquire);
        while (pending > current_max
               && !_rcv_pending_max.compare_exchange_weak(
                 current_max, pending, std::memory_order_acq_rel)) {
        }
        _rcv_pending_end.store(pending, std::memory_order_release);
        _rcv_seen.store(true, std::memory_order_release);
    }

    void *_send_socket;
    void *_recv_socket;
    unsigned long long _sample_interval_ns;
    unsigned int _sample_every_msgs;
    std::atomic<unsigned long long> _send_last_sample_ns;
    std::atomic<unsigned long long> _recv_last_sample_ns;
    std::atomic<unsigned int> _send_msgs_since_sample;
    std::atomic<unsigned int> _recv_msgs_since_sample;
    std::atomic<unsigned long long> _send_total;
    std::atomic<unsigned long long> _recv_total;
    std::atomic<unsigned long long> _snd_pending_max;
    std::atomic<unsigned long long> _rcv_pending_max;
    std::atomic<unsigned long long> _rcv_pending_end;
    std::atomic<bool> _snd_seen;
    std::atomic<bool> _rcv_seen;

    queue_probe_t(const queue_probe_t &);
    queue_probe_t &operator=(const queue_probe_t &);
};

inline queue_stats_t sample_queue_stats(queue_probe_t *queue_probe_)
{
    if (!queue_probe_)
        return queue_stats_t();
    return queue_probe_->snapshot();
}

inline void print_failure_diagnostics(const std::string &lib_type,
                                      const std::string &pattern,
                                      const std::string &transport,
                                      size_t size,
                                      const char *detail_ = NULL)
{
    std::cerr << "FAIL," << lib_type << "," << pattern << "," << transport << ","
              << size;
    if (detail_ && *detail_)
        std::cerr << "," << detail_;
    std::cerr << std::endl;
}

inline void print_fail_result(const std::string &lib_type,
                              const std::string &pattern,
                              const std::string &transport,
                              size_t size,
                              queue_probe_t *queue_probe_ = NULL)
{
    print_failure_diagnostics(lib_type, pattern, transport, size);
    if (!queue_probe_)
        return;
    const queue_stats_t queue_stats = sample_queue_stats(queue_probe_);
    print_queue_metrics(lib_type, pattern, transport, size, queue_stats);
}

inline bool wait_socket_event(void *socket_,
                              short events_,
                              long timeout_ms_,
                              short *revents_out_ = NULL)
{
    if (!socket_)
        return false;

    zlink_pollitem_t item;
    item.socket = socket_;
    item.fd = 0;
    item.events = events_;
    item.revents = 0;
    const int rc = perf_socket_poll(&item, 1, timeout_ms_);
    if (rc < 0)
        return false;
    if (revents_out_)
        *revents_out_ = item.revents;
    return rc > 0 && (item.revents & events_) != 0;
}

inline int poll_socket_event(void *socket_,
                             short events_,
                             long timeout_ms_,
                             short *revents_out_ = NULL)
{
    if (!socket_)
        return -1;

    zlink_pollitem_t item;
    item.socket = socket_;
    item.fd = 0;
    item.events = events_;
    item.revents = 0;
    const int rc = perf_socket_poll(&item, 1, timeout_ms_);
    if (revents_out_)
        *revents_out_ = item.revents;
    return rc;
}

inline long remaining_timeout_ms(
  const std::chrono::steady_clock::time_point &deadline_,
  long minimum_ms_ = 1)
{
    const long long remaining_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline_ - std::chrono::steady_clock::now())
        .count();
    return remaining_ms > 0 ? static_cast<long>(remaining_ms)
                            : (minimum_ms_ > 0 ? minimum_ms_ : 0);
}

inline bool wait_socket_event_until(
  void *socket_,
  short events_,
  const std::chrono::steady_clock::time_point &deadline_,
  short *revents_out_ = NULL)
{
    if (!socket_)
        return false;
    if (std::chrono::steady_clock::now() >= deadline_)
        return false;
    return wait_socket_event(socket_, events_,
                             remaining_timeout_ms(deadline_, 1),
                             revents_out_);
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
    endpoint = perf_bind_endpoint_once(socket_, endpoint, transport,
                                       &perf_bind_socket_endpoint, true);
    if (endpoint.empty())
        return std::string();
    if (transport == "inproc")
        return endpoint;
    if (bench_debug_enabled()) {
        std::cerr << "Resolved endpoint (" << transport << "): " << endpoint
                  << std::endl;
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
inline int run_standard_bench_main(int argc_,
                                   char **argv_,
                                   const char *pattern_,
                                   RunFn run_) {
    if (argc_ < 4)
        return 1;
    if (!single_perf_validate_recv_mode_for_pattern(pattern_))
        return 1;
    std::string lib_name = argv_[1];
    std::string transport = argv_[2];
    size_t size = std::stoul(argv_[3]);
    run_(transport, size, lib_name);
    return 0;
}

#endif
