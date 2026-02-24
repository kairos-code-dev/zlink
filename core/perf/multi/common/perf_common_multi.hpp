#ifndef PERF_COMMON_MULTI_HPP
#define PERF_COMMON_MULTI_HPP

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct multi_bench_settings_t
{
    size_t clients;
    int inflight;
    int hwm;
    int warmup_seconds;
    int active_warmup;
    int duration_seconds;
    int settle_ms;
    int drain_ms;
    int size_transition_drain_ms;
    int recv_batch;
    int send_workers;
    int send_backoff_us;
    int connect_ready_timeout_ms;
    int connect_concurrency;
    int server_recv_threads;
};

enum multi_send_result_t
{
    multi_send_ok = 0,
    multi_send_would_block = 1,
    multi_send_error = 2
};

enum multi_bench_phase_t
{
    multi_phase_measure = 0,
    multi_phase_drain = 1,
    multi_phase_stop = 2
};

struct multi_bench_result_t
{
    long warmup_recv;
    long measure_recv;
    long drain_recv;
    long measure_send_ok;
    double connect_ms;
    double ready_wait_ms;
    bool failed;

    multi_bench_result_t ()
        : warmup_recv (0),
          measure_recv (0),
          drain_recv (0),
          measure_send_ok (0),
          connect_ms (0.0),
          ready_wait_ms (0.0),
          failed (false)
    {
    }
};

class simple_counter_semaphore_t
{
public:
    explicit simple_counter_semaphore_t (int max_inflight)
        : _max_count (max_inflight > 0 ? max_inflight : 1), _count (0)
    {
    }

    void acquire ()
    {
        std::unique_lock<std::mutex> lock (_mutex);
        _cv.wait (lock, [this] () { return _count < _max_count; });
        ++_count;
    }

    void release ()
    {
        {
            std::lock_guard<std::mutex> lock (_mutex);
            --_count;
        }
        _cv.notify_one ();
    }

private:
    const int _max_count;
    int _count;
    std::mutex _mutex;
    std::condition_variable _cv;
};

inline bool bench_show_prep ()
{
    static const bool enabled =
      std::getenv ("PERF_SHOW_PREP") != NULL
      && std::strcmp (std::getenv ("PERF_SHOW_PREP"), "0") != 0;
    return enabled;
}

inline void print_prep_result (const std::string &lib_type,
                               const std::string &pattern,
                               const std::string &transport,
                               size_t size,
                               double connect_ms,
                               double ready_wait_ms)
{
    if (!bench_show_prep ())
        return;
    const double printed_connect_ms = ready_wait_ms >= 0.01 ? connect_ms : 0.0;

    std::cout << "PREP," << lib_type << "," << pattern << "," << transport << ","
              << size << ",connect_ms," << std::fixed << std::setprecision (2)
              << printed_connect_ms << ",ready_ms," << ready_wait_ms
              << std::endl;
}

inline int resolve_multi_int_env (const char *env_name,
                                  int default_value,
                                  int min_value)
{
    if (!env_name)
        return default_value;

    const char *value = std::getenv (env_name);
    if (!value || !*value)
        return default_value;

    char *end = NULL;
    errno = 0;
    const long parsed = std::strtol (value, &end, 10);
    if (errno != 0 || end == value)
        return default_value;

    if (parsed < min_value)
        return min_value;
    return static_cast<int> (parsed);
}

inline bool multi_pattern_uses_default_drain (const char *pattern)
{
    if (!pattern || !*pattern)
        return false;

    const bool is_stream_variant =
      std::strcmp (pattern, "MULTI_STREAM") == 0
      || std::strcmp (pattern, "MULTI_STREAM_CALLBACK") == 0
      || std::strcmp (pattern, "MULTI_STREAM_LEN32BE") == 0;

    return std::strcmp (pattern, "MULTI_DEALER_DEALER") == 0
           || std::strcmp (pattern, "MULTI_DEALER_ROUTER") == 0
           || std::strcmp (pattern, "MULTI_ROUTER_ROUTER") == 0
           || std::strcmp (pattern, "MULTI_PUBSUB") == 0
           || is_stream_variant;
}

inline int resolve_multi_drain_ms ()
{
    const int explicit_drain = resolve_multi_int_env ("PERF_MULTI_DRAIN_MS", -1, 0);
    if (explicit_drain >= 0)
        return explicit_drain;

    const char *pattern = std::getenv ("PERF_MULTI_PATTERN");
    if (multi_pattern_uses_default_drain (pattern))
        return 300;
    return 0;
}

inline int resolve_multi_size_transition_drain_ms (int fallback_drain_ms)
{
    (void) fallback_drain_ms;
    return resolve_multi_int_env (
      "PERF_MULTI_SIZE_TRANSITION_DRAIN_MS",
      300,
      0);
}

inline int resolve_multi_connect_concurrency (size_t clients)
{
    const int default_concurrency =
      clients >= static_cast<size_t> (10000) ? 1024 : 128;
    return resolve_multi_int_env (
      "PERF_MULTI_CONNECT_CONCURRENCY", default_concurrency, 1);
}

inline int resolve_multi_default_hwm ()
{
    return 100000;
}

inline bool is_multi_stream_variant_pattern (const char *pattern)
{
    if (!pattern || !*pattern)
        return false;
    return std::strcmp (pattern, "MULTI_STREAM") == 0
           || std::strcmp (pattern, "MULTI_STREAM_CALLBACK") == 0
           || std::strcmp (pattern, "MULTI_STREAM_LEN32BE") == 0;
}

inline int resolve_multi_default_clients (const char *pattern)
{
    return is_multi_stream_variant_pattern (pattern) ? 10000 : 100;
}

inline int resolve_multi_default_inflight ()
{
    return 1;
}

inline int resolve_multi_stream_max_inflight_bytes ()
{
    return resolve_multi_int_env (
      "PERF_MULTI_STREAM_MAX_INFLIGHT_BYTES",
      32 * 1024 * 1024,
      1);
}

inline int resolve_multi_max_inflight_bytes ()
{
    return resolve_multi_int_env (
      "PERF_MULTI_MAX_INFLIGHT_BYTES",
      16 * 1024 * 1024,
      1);
}

inline int resolve_multi_inflight_by_bytes (
  const multi_bench_settings_t &settings,
  size_t msg_size,
  int max_window_bytes)
{
    const int configured_inflight = std::max (1, settings.inflight);
    const size_t safe_msg_size = std::max<size_t> (1, msg_size);
    const size_t safe_clients = std::max<size_t> (1, settings.clients);
    const unsigned long long denom =
      static_cast<unsigned long long> (safe_msg_size)
      * static_cast<unsigned long long> (safe_clients);
    if (denom == 0)
        return configured_inflight;

    const unsigned long long max_window =
      static_cast<unsigned long long> (std::max (1, max_window_bytes));
    unsigned long long inflight_by_bytes = max_window / denom;
    if (inflight_by_bytes == 0)
        inflight_by_bytes = 1;
    if (inflight_by_bytes > static_cast<unsigned long long> (INT_MAX))
        inflight_by_bytes = static_cast<unsigned long long> (INT_MAX);

    return std::max (
      1,
      std::min (
        configured_inflight, static_cast<int> (inflight_by_bytes)));
}

inline int resolve_multi_stream_inflight_for_size (
  const multi_bench_settings_t &settings,
  size_t msg_size)
{
    return resolve_multi_inflight_by_bytes (
      settings, msg_size, resolve_multi_stream_max_inflight_bytes ());
}

inline int resolve_multi_inflight_for_size (
  const multi_bench_settings_t &settings,
  size_t msg_size)
{
    const char *pattern = std::getenv ("PERF_MULTI_PATTERN");
    if (pattern
        && (std::strcmp (pattern, "MULTI_STREAM") == 0
            || std::strcmp (pattern, "MULTI_STREAM_CALLBACK") == 0
            || std::strcmp (pattern, "MULTI_STREAM_LEN32BE") == 0))
        return resolve_multi_stream_inflight_for_size (settings, msg_size);
    return resolve_multi_inflight_by_bytes (
      settings, msg_size, resolve_multi_max_inflight_bytes ());
}

inline multi_bench_settings_t resolve_multi_bench_settings ()
{
    multi_bench_settings_t settings;
    const char *pattern = std::getenv ("PERF_MULTI_PATTERN");
    settings.clients = static_cast<size_t> (
      resolve_multi_int_env (
        "PERF_MULTI_CLIENTS",
        resolve_multi_default_clients (pattern),
        1));
    settings.inflight = resolve_multi_int_env (
      "PERF_MULTI_INFLIGHT",
      resolve_multi_default_inflight (),
      1);
    settings.hwm = resolve_multi_int_env (
      "PERF_MULTI_HWM", resolve_multi_default_hwm (), 1);
    settings.warmup_seconds =
      resolve_multi_int_env ("PERF_MULTI_WARMUP_SECONDS", 3, 0);
    settings.active_warmup =
      resolve_multi_int_env ("PERF_MULTI_ACTIVE_WARMUP", 0, 0);
    settings.duration_seconds =
      resolve_multi_int_env ("PERF_MULTI_DURATION_SECONDS", 5, 1);
    settings.settle_ms = resolve_multi_int_env ("PERF_MULTI_SETTLE_MS", 500, 0);
    settings.drain_ms = resolve_multi_drain_ms ();
    settings.size_transition_drain_ms =
      resolve_multi_size_transition_drain_ms (settings.drain_ms);
    settings.recv_batch = resolve_multi_int_env ("PERF_MULTI_RECV_BATCH", 64, 1);
    const int default_workers =
      settings.clients >= static_cast<size_t> (10000) ? 4 : 1;
    settings.send_workers =
      resolve_multi_int_env (
        "PERF_MULTI_SEND_WORKERS",
        default_workers,
        1);
    settings.send_backoff_us =
      resolve_multi_int_env ("PERF_MULTI_SEND_BACKOFF_US", 20, 0);
    settings.connect_concurrency =
      resolve_multi_connect_concurrency (settings.clients);
    settings.connect_ready_timeout_ms =
      resolve_multi_int_env ("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000, 0);
    settings.server_recv_threads =
      resolve_multi_int_env ("PERF_SERVER_RECV_THREADS", 1, 1);
    return settings;
}

inline void run_size_transition_drain_stage (
  const multi_bench_settings_t &settings,
  bool has_next_size)
{
    (void) has_next_size;

    const int drain_ms = std::max (0, settings.size_transition_drain_ms);
    if (drain_ms <= 0)
        return;

    std::this_thread::sleep_for (std::chrono::milliseconds (drain_ms));
}

inline long run_multi_warmup_stage (
  const multi_bench_settings_t &settings,
  std::atomic<int> &phase,
  std::atomic<bool> &fatal_error,
  std::atomic<long> &send_total,
  std::atomic<long> &recv_total)
{
    const int warmup_seconds = std::max (0, settings.warmup_seconds);
    if (warmup_seconds <= 0
        || fatal_error.load (std::memory_order_acquire)) {
        return 0;
    }

    if (settings.active_warmup <= 0) {
        const auto warmup_end = std::chrono::steady_clock::now ()
                                + std::chrono::seconds (warmup_seconds);
        while (std::chrono::steady_clock::now () < warmup_end
               && !fatal_error.load (std::memory_order_acquire)) {
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        return 0;
    }

    phase.store (multi_phase_measure, std::memory_order_release);
    const long warmup_recv_start = recv_total.load (std::memory_order_acquire);
    const auto warmup_end = std::chrono::steady_clock::now ()
                            + std::chrono::seconds (warmup_seconds);
    while (std::chrono::steady_clock::now () < warmup_end
           && !fatal_error.load (std::memory_order_acquire)) {
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    const long warmup_recv_end = recv_total.load (std::memory_order_acquire);
    phase.store (multi_phase_drain, std::memory_order_release);

    const int warmup_drain_ms = resolve_multi_int_env (
      "PERF_MULTI_WARMUP_DRAIN_MS",
      std::max (settings.drain_ms, 1000),
      0);
    const auto drain_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (warmup_drain_ms);
    while (std::chrono::steady_clock::now () < drain_deadline
           && !fatal_error.load (std::memory_order_acquire)) {
        const long inflight =
          send_total.load (std::memory_order_relaxed)
          - recv_total.load (std::memory_order_relaxed);
        if (inflight <= 0)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    return std::max<long> (0, warmup_recv_end - warmup_recv_start);
}

template <typename ConnectFn>
inline bool connect_clients_concurrently (const std::vector<void *> &sockets,
                                          const std::string &endpoint,
                                          ConnectFn connect_fn)
{
    if (sockets.empty ())
        return true;

    for (size_t i = 0; i < sockets.size (); ++i) {
        if (!connect_fn (sockets[i], endpoint))
            return false;
    }

    return true;
}

template <typename ConnectFn>
inline bool connect_clients_concurrently (const std::vector<void *> &sockets,
                                          const std::string &endpoint,
                                          ConnectFn connect_fn,
                                          int max_concurrency)
{
    if (sockets.empty ())
        return true;

    const size_t chunk =
      static_cast<size_t> (std::max (1, max_concurrency));
    size_t begin = 0;
    while (begin < sockets.size ()) {
        const size_t end = std::min (sockets.size (), begin + chunk);
        std::atomic<bool> ok (true);
        std::vector<std::thread> workers;
        for (size_t i = begin; i < end; ++i) {
            workers.push_back (
              std::thread ([&, i] () {
                  if (!connect_fn (sockets[i], endpoint))
                      ok.store (false, std::memory_order_relaxed);
              }));
        }
        for (size_t i = 0; i < workers.size (); ++i)
            workers[i].join ();
        if (!ok.load (std::memory_order_relaxed))
            return false;
        begin = end;
    }
    return true;
}

template <typename SendFn, typename RecvBatchFn>
inline multi_bench_result_t run_multi_phase_benchmark (
  const std::vector<void *> &senders,
  const multi_bench_settings_t &settings,
  SendFn send_fn,
  RecvBatchFn recv_batch_fn)
{
    multi_bench_result_t result;
    if (senders.empty ()) {
        result.failed = true;
        return result;
    }

    std::atomic<int> phase (multi_phase_drain);
    std::atomic<bool> fatal_error (false);

    std::atomic<long> warmup_recv (0);
    std::atomic<long> measure_recv (0);
    std::atomic<long> drain_recv (0);
    std::atomic<long> recv_total (0);
    std::atomic<long> send_total (0);

    const long inflight_limit =
      static_cast<long> (std::max<size_t> (1, senders.size ()))
      * static_cast<long> (std::max (1, settings.inflight));

    auto window_exhausted = [&] () -> bool {
        const long in_flight = send_total.load (std::memory_order_relaxed)
                               - recv_total.load (std::memory_order_relaxed);
        return in_flight >= inflight_limit;
    };

    auto send_backoff = [&] () {
        if (settings.send_backoff_us > 0) {
            std::this_thread::sleep_for (
              std::chrono::microseconds (settings.send_backoff_us));
        } else {
            std::this_thread::yield ();
        }
    };

    std::thread receiver ([&] () {
        while (!fatal_error.load (std::memory_order_acquire)) {
            const int current_phase = phase.load (std::memory_order_acquire);
            if (current_phase == multi_phase_stop)
                break;

            const int count =
              recv_batch_fn (static_cast<multi_bench_phase_t> (current_phase));
            if (count < 0) {
                std::this_thread::sleep_for (std::chrono::microseconds (50));
                continue;
            }
            if (count <= 0)
                continue;
            recv_total.fetch_add (count, std::memory_order_relaxed);

            if (current_phase == multi_phase_measure)
                measure_recv.fetch_add (count, std::memory_order_relaxed);
            else
                drain_recv.fetch_add (count, std::memory_order_relaxed);
        }
    });

    const int duration_seconds = std::max (1, settings.duration_seconds);
    std::atomic<bool> send_start (false);
    std::atomic<bool> send_running (true);
    std::atomic<int> senders_ready (0);
    std::atomic<long> send_success (0);

    const size_t sender_count = senders.size ();
    const size_t worker_count =
      std::max<size_t> (
        1, std::min<size_t> (sender_count, static_cast<size_t> (settings.send_workers)));
    std::vector<char> sender_ready (sender_count, 0);
    std::atomic<long> ready_total (0);

    std::vector<std::vector<size_t> > worker_assign (worker_count);
    for (size_t i = 0; i < sender_count; ++i) {
        const size_t w = i % worker_count;
        worker_assign[w].push_back (i);
    }

    std::vector<std::thread> sender_threads;
    sender_threads.reserve (worker_count);
    for (size_t w = 0; w < worker_count; ++w) {
        sender_threads.push_back (std::thread ([&, w] () {
            const std::vector<size_t> &owned = worker_assign[w];
            senders_ready.fetch_add (1, std::memory_order_release);
            while (!send_start.load (std::memory_order_acquire)
                   && !fatal_error.load (std::memory_order_acquire)) {
                std::this_thread::yield ();
            }

            size_t rr = 0;
            size_t blocked_streak = 0;
            while (send_running.load (std::memory_order_acquire)
                   && !fatal_error.load (std::memory_order_acquire)) {
                const int current_phase = phase.load (std::memory_order_acquire);
                if (current_phase != multi_phase_measure) {
                    blocked_streak = 0;
                    send_backoff ();
                    continue;
                }
                if (window_exhausted ()) {
                    blocked_streak = 0;
                    send_backoff ();
                    continue;
                }

                const size_t idx = owned[rr % owned.size ()];
                ++rr;
                const multi_send_result_t rc = send_fn (idx);
                if (rc == multi_send_ok) {
                    blocked_streak = 0;
                    if (!sender_ready[idx]) {
                        sender_ready[idx] = 1;
                        ready_total.fetch_add (1, std::memory_order_relaxed);
                    }
                    if (current_phase == multi_phase_measure)
                        send_success.fetch_add (1, std::memory_order_relaxed);
                    send_total.fetch_add (1, std::memory_order_relaxed);
                    continue;
                }
                if (rc == multi_send_error) {
                    fatal_error.store (true, std::memory_order_release);
                    break;
                }

                ++blocked_streak;
                if (blocked_streak >= owned.size ()) {
                    blocked_streak = 0;
                    send_backoff ();
                }
            }
        }));
    }

    while (senders_ready.load (std::memory_order_acquire)
             < static_cast<int> (worker_count)
           && !fatal_error.load (std::memory_order_acquire)) {
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    send_start.store (true, std::memory_order_release);
    if (ready_total.load (std::memory_order_acquire)
        < static_cast<long> (sender_count)) {
        const auto ready_deadline =
          std::chrono::steady_clock::now () + std::chrono::seconds (2);
        while (std::chrono::steady_clock::now () < ready_deadline
               && !fatal_error.load (std::memory_order_acquire)
               && ready_total.load (std::memory_order_acquire)
                    < static_cast<long> (sender_count)) {
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }

    if (!fatal_error.load (std::memory_order_acquire)
        && settings.settle_ms > 0) {
        const auto settle_end =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (settings.settle_ms);
        while (std::chrono::steady_clock::now () < settle_end
               && !fatal_error.load (std::memory_order_acquire)) {
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }

    if (!fatal_error.load (std::memory_order_acquire)) {
        const long warmup_delta = run_multi_warmup_stage (
          settings, phase, fatal_error, send_total, recv_total);
        warmup_recv.store (warmup_delta, std::memory_order_relaxed);
        if (!fatal_error.load (std::memory_order_acquire))
            phase.store (multi_phase_measure, std::memory_order_release);
    }

    const long measure_recv_start = recv_total.load (std::memory_order_acquire);
    const auto measure_end =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (duration_seconds);
    while (std::chrono::steady_clock::now () < measure_end
           && !fatal_error.load (std::memory_order_acquire)) {
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    const long measure_recv_end = recv_total.load (std::memory_order_acquire);
    phase.store (multi_phase_drain, std::memory_order_release);

    send_running.store (false, std::memory_order_release);
    for (size_t i = 0; i < sender_threads.size (); ++i) {
        if (sender_threads[i].joinable ())
            sender_threads[i].join ();
    }

    const auto drain_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (std::max (0, settings.drain_ms));
    while (std::chrono::steady_clock::now () < drain_deadline
           && !fatal_error.load (std::memory_order_acquire)) {
        const long inflight =
          send_total.load (std::memory_order_relaxed)
          - recv_total.load (std::memory_order_relaxed);
        if (inflight <= 0)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    phase.store (multi_phase_stop, std::memory_order_release);
    if (receiver.joinable ())
        receiver.join ();

    result.warmup_recv = warmup_recv.load (std::memory_order_relaxed);
    result.measure_recv =
      std::max<long> (0, measure_recv_end - measure_recv_start);
    result.drain_recv = drain_recv.load (std::memory_order_relaxed);
    result.measure_send_ok = send_success.load (std::memory_order_relaxed);
    result.failed = fatal_error.load (std::memory_order_acquire);
    return result;
}

template <typename SetupSenderFn,
          typename SendFn,
          typename RecvBatchFn,
          typename CleanupSenderFn,
          typename PreStartFn>
inline multi_bench_result_t run_multi_phase_benchmark_with_sender_lifecycle (
  size_t sender_count,
  const multi_bench_settings_t &settings,
  SetupSenderFn setup_sender_fn,
  SendFn send_fn,
  RecvBatchFn recv_batch_fn,
  CleanupSenderFn cleanup_sender_fn,
  PreStartFn pre_start_fn,
  bool require_ready_barrier)
{
    multi_bench_result_t result;
    if (sender_count == 0) {
        result.failed = true;
        return result;
    }

    std::atomic<int> phase (multi_phase_drain);
    std::atomic<bool> fatal_error (false);

    std::atomic<long> warmup_recv (0);
    std::atomic<long> measure_recv (0);
    std::atomic<long> drain_recv (0);
    std::atomic<long> recv_total (0);
    std::atomic<long> send_total (0);
    std::atomic<long> send_success (0);

    const long inflight_limit =
      static_cast<long> (std::max<size_t> (1, sender_count))
      * static_cast<long> (std::max (1, settings.inflight));

    auto window_exhausted = [&] () -> bool {
        const long in_flight = send_total.load (std::memory_order_relaxed)
                               - recv_total.load (std::memory_order_relaxed);
        return in_flight >= inflight_limit;
    };

    auto send_backoff = [&] () {
        if (settings.send_backoff_us > 0) {
            std::this_thread::sleep_for (
              std::chrono::microseconds (settings.send_backoff_us));
        } else {
            std::this_thread::yield ();
        }
    };

    std::thread receiver;
    auto start_receiver = [&] () {
        receiver = std::thread ([&] () {
            while (!fatal_error.load (std::memory_order_acquire)) {
                const int current_phase = phase.load (std::memory_order_acquire);
                if (current_phase == multi_phase_stop)
                    break;

                const int count =
                  recv_batch_fn (static_cast<multi_bench_phase_t> (current_phase));
                if (count < 0) {
                    std::this_thread::sleep_for (std::chrono::microseconds (50));
                    continue;
                }
                if (count <= 0)
                    continue;

                recv_total.fetch_add (count, std::memory_order_relaxed);
                if (current_phase == multi_phase_measure)
                    measure_recv.fetch_add (count, std::memory_order_relaxed);
                else
                    drain_recv.fetch_add (count, std::memory_order_relaxed);
            }
        });
    };

    const size_t worker_count =
      std::max<size_t> (
        1, std::min<size_t> (sender_count, static_cast<size_t> (settings.send_workers)));
    std::vector<std::vector<size_t> > worker_assign (worker_count);
    for (size_t i = 0; i < sender_count; ++i) {
        const size_t w = i % worker_count;
        worker_assign[w].push_back (i);
    }

    std::vector<char> sender_ready (sender_count, 0);
    std::atomic<long> ready_total (0);
    std::atomic<bool> send_start (false);
    std::atomic<bool> send_running (true);
    std::atomic<int> workers_ready (0);

    std::vector<std::thread> sender_threads;
    sender_threads.reserve (worker_count);
    for (size_t w = 0; w < worker_count; ++w) {
        sender_threads.push_back (std::thread ([&, w] () {
            const std::vector<size_t> &owned = worker_assign[w];
            size_t initialized = 0;
            for (size_t i = 0; i < owned.size (); ++i) {
                if (!setup_sender_fn (owned[i])) {
                    fatal_error.store (true, std::memory_order_release);
                    break;
                }
                ++initialized;
            }

            workers_ready.fetch_add (1, std::memory_order_release);
            while (!send_start.load (std::memory_order_acquire)
                   && !fatal_error.load (std::memory_order_acquire)) {
                std::this_thread::yield ();
            }

            if (!fatal_error.load (std::memory_order_acquire)) {
                size_t rr = 0;
                size_t blocked_streak = 0;
                while (send_running.load (std::memory_order_acquire)
                       && !fatal_error.load (std::memory_order_acquire)) {
                    const int current_phase = phase.load (std::memory_order_acquire);
                    if (current_phase != multi_phase_measure) {
                        blocked_streak = 0;
                        send_backoff ();
                        continue;
                    }
                    if (window_exhausted ()) {
                        blocked_streak = 0;
                        send_backoff ();
                        continue;
                    }

                    const size_t idx = owned[rr % owned.size ()];
                    ++rr;
                    const multi_send_result_t rc = send_fn (idx);
                    if (rc == multi_send_ok) {
                        blocked_streak = 0;
                        if (!sender_ready[idx]) {
                            sender_ready[idx] = 1;
                            ready_total.fetch_add (1, std::memory_order_relaxed);
                        }
                        if (current_phase == multi_phase_measure)
                            send_success.fetch_add (1, std::memory_order_relaxed);
                        send_total.fetch_add (1, std::memory_order_relaxed);
                        continue;
                    }
                    if (rc == multi_send_error) {
                        fatal_error.store (true, std::memory_order_release);
                        break;
                    }

                    ++blocked_streak;
                    if (blocked_streak >= owned.size ()) {
                        blocked_streak = 0;
                        send_backoff ();
                    }
                }
            }

            for (size_t i = 0; i < initialized; ++i)
                cleanup_sender_fn (owned[i]);
        }));
    }

    const auto connect_start = std::chrono::steady_clock::now ();
    while (workers_ready.load (std::memory_order_acquire)
             < static_cast<int> (worker_count)
           && !fatal_error.load (std::memory_order_acquire)) {
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    const auto connect_end = std::chrono::steady_clock::now ();
    const double connect_ms = std::chrono::duration<double, std::milli> (
                                connect_end - connect_start)
                                .count ();

    const auto ready_wait_start = std::chrono::steady_clock::now ();
    if (!fatal_error.load (std::memory_order_acquire) && !pre_start_fn ())
        fatal_error.store (true, std::memory_order_release);
    const auto ready_wait_end = std::chrono::steady_clock::now ();
    const double ready_wait_ms = std::chrono::duration<double, std::milli> (
                                   ready_wait_end - ready_wait_start)
                                   .count ();

    if (!fatal_error.load (std::memory_order_acquire))
        start_receiver ();

    send_start.store (true, std::memory_order_release);

    if (!fatal_error.load (std::memory_order_acquire) && require_ready_barrier) {
        phase.store (multi_phase_measure, std::memory_order_release);
        const auto ready_deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (
            std::max (0, settings.connect_ready_timeout_ms));
        while (std::chrono::steady_clock::now () < ready_deadline
               && !fatal_error.load (std::memory_order_acquire)
               && ready_total.load (std::memory_order_acquire)
                    < static_cast<long> (sender_count)) {
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        if (ready_total.load (std::memory_order_acquire)
            < static_cast<long> (sender_count)) {
            fatal_error.store (true, std::memory_order_release);
        }
        if (settings.settle_ms > 0) {
            phase.store (multi_phase_drain, std::memory_order_release);
        }
    }

    if (!fatal_error.load (std::memory_order_acquire)
        && settings.settle_ms > 0) {
        const auto settle_end =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (settings.settle_ms);
        while (std::chrono::steady_clock::now () < settle_end
               && !fatal_error.load (std::memory_order_acquire)) {
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }

    if (!fatal_error.load (std::memory_order_acquire)) {
        const long warmup_delta = run_multi_warmup_stage (
          settings, phase, fatal_error, send_total, recv_total);
        warmup_recv.store (warmup_delta, std::memory_order_relaxed);
        if (!fatal_error.load (std::memory_order_acquire))
            phase.store (multi_phase_measure, std::memory_order_release);
    }

    const int duration_seconds = std::max (1, settings.duration_seconds);
    const long measure_recv_start = recv_total.load (std::memory_order_acquire);
    const auto measure_end =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (duration_seconds);
    while (std::chrono::steady_clock::now () < measure_end
           && !fatal_error.load (std::memory_order_acquire)) {
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    const long measure_recv_end = recv_total.load (std::memory_order_acquire);
    phase.store (multi_phase_drain, std::memory_order_release);

    send_running.store (false, std::memory_order_release);
    for (size_t i = 0; i < sender_threads.size (); ++i) {
        if (sender_threads[i].joinable ())
            sender_threads[i].join ();
    }

    const auto drain_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (std::max (0, settings.drain_ms));
    while (std::chrono::steady_clock::now () < drain_deadline
           && !fatal_error.load (std::memory_order_acquire)) {
        const long inflight =
          send_total.load (std::memory_order_relaxed)
          - recv_total.load (std::memory_order_relaxed);
        if (inflight <= 0)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    phase.store (multi_phase_stop, std::memory_order_release);
    if (receiver.joinable ())
        receiver.join ();

    result.warmup_recv = warmup_recv.load (std::memory_order_relaxed);
    result.measure_recv =
      std::max<long> (0, measure_recv_end - measure_recv_start);
    result.drain_recv = drain_recv.load (std::memory_order_relaxed);
    result.measure_send_ok = send_success.load (std::memory_order_relaxed);
    result.connect_ms = connect_ms;
    result.ready_wait_ms = ready_wait_ms;
    result.failed = fatal_error.load (std::memory_order_acquire);
    return result;
}

template <typename SetupSenderFn,
          typename SendFn,
          typename RecvBatchFn,
          typename CleanupSenderFn,
          typename PreStartFn>
inline multi_bench_result_t
run_multi_phase_benchmark_with_sender_lifecycle_batched (
  size_t sender_count,
  const multi_bench_settings_t &settings,
  int send_batch,
  SetupSenderFn setup_sender_fn,
  SendFn send_fn,
  RecvBatchFn recv_batch_fn,
  CleanupSenderFn cleanup_sender_fn,
  PreStartFn pre_start_fn,
  bool require_ready_barrier)
{
    multi_bench_result_t result;
    if (sender_count == 0) {
        result.failed = true;
        return result;
    }

    std::atomic<int> phase (multi_phase_drain);
    std::atomic<bool> fatal_error (false);

    std::atomic<long> warmup_recv (0);
    std::atomic<long> measure_recv (0);
    std::atomic<long> drain_recv (0);
    std::atomic<long> recv_total (0);
    std::atomic<long> send_total (0);
    std::atomic<long> send_success (0);

    const int effective_send_batch = std::max (1, send_batch);
    const long inflight_limit =
      static_cast<long> (std::max<size_t> (1, sender_count))
      * static_cast<long> (std::max (1, settings.inflight));

    auto window_exhausted = [&] () -> bool {
        const long in_flight = send_total.load (std::memory_order_relaxed)
                               - recv_total.load (std::memory_order_relaxed);
        return in_flight >= inflight_limit;
    };

    auto send_backoff = [&] () {
        if (settings.send_backoff_us > 0) {
            std::this_thread::sleep_for (
              std::chrono::microseconds (settings.send_backoff_us));
        } else {
            std::this_thread::yield ();
        }
    };

    std::thread receiver;
    auto start_receiver = [&] () {
        receiver = std::thread ([&] () {
            while (!fatal_error.load (std::memory_order_acquire)) {
                const int current_phase = phase.load (std::memory_order_acquire);
                if (current_phase == multi_phase_stop)
                    break;

                const int count =
                  recv_batch_fn (static_cast<multi_bench_phase_t> (current_phase));
                if (count < 0) {
                    std::this_thread::sleep_for (std::chrono::microseconds (50));
                    continue;
                }
                if (count <= 0)
                    continue;

                recv_total.fetch_add (count, std::memory_order_relaxed);
                if (current_phase == multi_phase_measure)
                    measure_recv.fetch_add (count, std::memory_order_relaxed);
                else
                    drain_recv.fetch_add (count, std::memory_order_relaxed);
            }
        });
    };

    const size_t worker_count =
      std::max<size_t> (
        1, std::min<size_t> (sender_count, static_cast<size_t> (settings.send_workers)));
    std::vector<std::vector<size_t> > worker_assign (worker_count);
    for (size_t i = 0; i < sender_count; ++i) {
        const size_t w = i % worker_count;
        worker_assign[w].push_back (i);
    }

    std::vector<char> sender_ready (sender_count, 0);
    std::atomic<long> ready_total (0);
    std::atomic<bool> send_start (false);
    std::atomic<bool> send_running (true);
    std::atomic<int> workers_ready (0);

    std::vector<std::thread> sender_threads;
    sender_threads.reserve (worker_count);
    for (size_t w = 0; w < worker_count; ++w) {
        sender_threads.push_back (std::thread ([&, w] () {
            const std::vector<size_t> &owned = worker_assign[w];
            size_t initialized = 0;
            for (size_t i = 0; i < owned.size (); ++i) {
                if (!setup_sender_fn (owned[i])) {
                    fatal_error.store (true, std::memory_order_release);
                    break;
                }
                ++initialized;
            }

            workers_ready.fetch_add (1, std::memory_order_release);
            while (!send_start.load (std::memory_order_acquire)
                   && !fatal_error.load (std::memory_order_acquire)) {
                std::this_thread::yield ();
            }

            if (!fatal_error.load (std::memory_order_acquire)) {
                size_t rr = 0;
                while (send_running.load (std::memory_order_acquire)
                       && !fatal_error.load (std::memory_order_acquire)) {
                    if (phase.load (std::memory_order_acquire)
                        != multi_phase_measure) {
                        send_backoff ();
                        continue;
                    }
                    if (window_exhausted ()) {
                        send_backoff ();
                        continue;
                    }

                    bool sent_any = false;
                    size_t blocked_streak = 0;
                    for (int b = 0; b < effective_send_batch; ++b) {
                        if (!send_running.load (std::memory_order_acquire)
                            || fatal_error.load (std::memory_order_acquire)
                            || phase.load (std::memory_order_acquire)
                                 != multi_phase_measure
                            || window_exhausted ()) {
                            break;
                        }

                        const size_t idx = owned[rr % owned.size ()];
                        ++rr;
                        const multi_send_result_t rc = send_fn (idx);
                        if (rc == multi_send_ok) {
                            sent_any = true;
                            blocked_streak = 0;
                            if (!sender_ready[idx]) {
                                sender_ready[idx] = 1;
                                ready_total.fetch_add (
                                  1, std::memory_order_relaxed);
                            }
                            send_success.fetch_add (1, std::memory_order_relaxed);
                            send_total.fetch_add (1, std::memory_order_relaxed);
                            continue;
                        }
                        if (rc == multi_send_error) {
                            fatal_error.store (true, std::memory_order_release);
                            break;
                        }

                        ++blocked_streak;
                        if (blocked_streak >= owned.size ())
                            break;
                    }

                    if (!sent_any)
                        send_backoff ();
                }
            }

            for (size_t i = 0; i < initialized; ++i)
                cleanup_sender_fn (owned[i]);
        }));
    }

    const auto connect_start = std::chrono::steady_clock::now ();
    while (workers_ready.load (std::memory_order_acquire)
             < static_cast<int> (worker_count)
           && !fatal_error.load (std::memory_order_acquire)) {
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    const auto connect_end = std::chrono::steady_clock::now ();
    const double connect_ms = std::chrono::duration<double, std::milli> (
                                connect_end - connect_start)
                                .count ();

    const auto ready_wait_start = std::chrono::steady_clock::now ();
    if (!fatal_error.load (std::memory_order_acquire) && !pre_start_fn ())
        fatal_error.store (true, std::memory_order_release);
    const auto ready_wait_end = std::chrono::steady_clock::now ();
    const double ready_wait_ms = std::chrono::duration<double, std::milli> (
                                   ready_wait_end - ready_wait_start)
                                   .count ();

    if (!fatal_error.load (std::memory_order_acquire))
        start_receiver ();

    send_start.store (true, std::memory_order_release);

    if (!fatal_error.load (std::memory_order_acquire) && require_ready_barrier) {
        phase.store (multi_phase_measure, std::memory_order_release);
        const auto ready_deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (
            std::max (0, settings.connect_ready_timeout_ms));
        while (std::chrono::steady_clock::now () < ready_deadline
               && !fatal_error.load (std::memory_order_acquire)
               && ready_total.load (std::memory_order_acquire)
                    < static_cast<long> (sender_count)) {
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        if (ready_total.load (std::memory_order_acquire)
            < static_cast<long> (sender_count)) {
            fatal_error.store (true, std::memory_order_release);
        }
        phase.store (multi_phase_drain, std::memory_order_release);
    }

    if (!fatal_error.load (std::memory_order_acquire)
        && settings.settle_ms > 0) {
        const auto settle_end =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (settings.settle_ms);
        while (std::chrono::steady_clock::now () < settle_end
               && !fatal_error.load (std::memory_order_acquire)) {
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }

    if (!fatal_error.load (std::memory_order_acquire)) {
        const long warmup_delta = run_multi_warmup_stage (
          settings, phase, fatal_error, send_total, recv_total);
        warmup_recv.store (warmup_delta, std::memory_order_relaxed);
        if (!fatal_error.load (std::memory_order_acquire))
            phase.store (multi_phase_measure, std::memory_order_release);
    }

    const int duration_seconds = std::max (1, settings.duration_seconds);
    const long measure_recv_start = recv_total.load (std::memory_order_acquire);
    const auto measure_end =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (duration_seconds);
    while (std::chrono::steady_clock::now () < measure_end
           && !fatal_error.load (std::memory_order_acquire)) {
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    const long measure_recv_end = recv_total.load (std::memory_order_acquire);
    phase.store (multi_phase_drain, std::memory_order_release);

    send_running.store (false, std::memory_order_release);
    for (size_t i = 0; i < sender_threads.size (); ++i) {
        if (sender_threads[i].joinable ())
            sender_threads[i].join ();
    }

    const auto drain_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (std::max (0, settings.drain_ms));
    while (std::chrono::steady_clock::now () < drain_deadline
           && !fatal_error.load (std::memory_order_acquire)) {
        const long inflight =
          send_total.load (std::memory_order_relaxed)
          - recv_total.load (std::memory_order_relaxed);
        if (inflight <= 0)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    phase.store (multi_phase_stop, std::memory_order_release);
    if (receiver.joinable ())
        receiver.join ();

    result.warmup_recv = warmup_recv.load (std::memory_order_relaxed);
    result.measure_recv =
      std::max<long> (0, measure_recv_end - measure_recv_start);
    result.drain_recv = drain_recv.load (std::memory_order_relaxed);
    result.measure_send_ok = send_success.load (std::memory_order_relaxed);
    result.connect_ms = connect_ms;
    result.ready_wait_ms = ready_wait_ms;
    result.failed = fatal_error.load (std::memory_order_acquire);
    return result;
}

template <typename SendFn, typename RecvBatchFn>
inline multi_bench_result_t run_multi_phase_oneway_benchmark (
  const std::vector<void *> &senders,
  const multi_bench_settings_t &settings,
  SendFn send_fn,
  RecvBatchFn recv_batch_fn)
{
    return run_multi_phase_benchmark (
      senders, settings, send_fn, recv_batch_fn);
}

template <typename SetupSenderFn,
          typename SendFn,
          typename RecvBatchFn,
          typename CleanupSenderFn,
          typename PreStartFn>
inline multi_bench_result_t run_multi_phase_rtt_benchmark (
  size_t sender_count,
  const multi_bench_settings_t &settings,
  SetupSenderFn setup_sender_fn,
  SendFn send_fn,
  RecvBatchFn recv_batch_fn,
  CleanupSenderFn cleanup_sender_fn,
  PreStartFn pre_start_fn,
  bool require_ready_barrier)
{
    return run_multi_phase_benchmark_with_sender_lifecycle (
      sender_count, settings, setup_sender_fn, send_fn, recv_batch_fn,
      cleanup_sender_fn, pre_start_fn, require_ready_barrier);
}

template <typename SetupSenderFn,
          typename SendFn,
          typename RecvBatchFn,
          typename CleanupSenderFn,
          typename PreStartFn>
inline multi_bench_result_t run_multi_phase_rtt_benchmark_batched (
  size_t sender_count,
  const multi_bench_settings_t &settings,
  int send_batch,
  SetupSenderFn setup_sender_fn,
  SendFn send_fn,
  RecvBatchFn recv_batch_fn,
  CleanupSenderFn cleanup_sender_fn,
  PreStartFn pre_start_fn,
  bool require_ready_barrier)
{
    return run_multi_phase_benchmark_with_sender_lifecycle_batched (
      sender_count, settings, send_batch, setup_sender_fn, send_fn, recv_batch_fn,
      cleanup_sender_fn, pre_start_fn, require_ready_barrier);
}

template <typename SetupSenderFn,
          typename SendFn,
          typename RelayBatchFn,
          typename RecvClientBatchFn,
          typename CleanupSenderFn,
          typename PreStartFn>
inline multi_bench_result_t run_multi_phase_socket_owned_rtt_benchmark (
  size_t sender_count,
  const multi_bench_settings_t &settings,
  SetupSenderFn setup_sender_fn,
  SendFn send_fn,
  RelayBatchFn relay_batch_fn,
  RecvClientBatchFn recv_client_batch_fn,
  CleanupSenderFn cleanup_sender_fn,
  PreStartFn pre_start_fn,
  bool require_ready_barrier)
{
    multi_bench_result_t result;
    if (sender_count == 0) {
        result.failed = true;
        return result;
    }

    std::atomic<int> phase (multi_phase_drain);
    std::atomic<bool> fatal_error (false);
    std::atomic<bool> send_start (false);
    std::atomic<bool> send_running (true);

    std::atomic<long> warmup_recv (0);
    std::atomic<long> measure_recv (0);
    std::atomic<long> drain_recv (0);
    std::atomic<long> recv_total (0);
    std::atomic<long> send_total (0);
    std::atomic<long> send_success (0);

    const long inflight_limit =
      static_cast<long> (std::max<size_t> (1, sender_count))
      * static_cast<long> (std::max (1, settings.inflight));

    auto window_exhausted = [&] () -> bool {
        const long in_flight = send_total.load (std::memory_order_relaxed)
                               - recv_total.load (std::memory_order_relaxed);
        return in_flight >= inflight_limit;
    };

    auto send_backoff = [&] () {
        if (settings.send_backoff_us > 0) {
            std::this_thread::sleep_for (
              std::chrono::microseconds (settings.send_backoff_us));
        } else {
            std::this_thread::yield ();
        }
    };

    const size_t worker_count =
      std::max<size_t> (
        1, std::min<size_t> (sender_count,
                             static_cast<size_t> (std::max (1, settings.send_workers))));
    std::vector<std::vector<size_t> > worker_assign (worker_count);
    for (size_t i = 0; i < sender_count; ++i) {
        const size_t w = i % worker_count;
        worker_assign[w].push_back (i);
    }

    std::vector<char> sender_ready (sender_count, 0);
    std::atomic<long> ready_total (0);
    std::atomic<size_t> setup_done_workers (0);

    std::vector<std::thread> sender_threads;
    sender_threads.reserve (worker_count);
    for (size_t w = 0; w < worker_count; ++w) {
        sender_threads.push_back (std::thread ([&, w] () {
            const std::vector<size_t> &owned = worker_assign[w];
            for (size_t i = 0; i < owned.size (); ++i) {
                const size_t idx = owned[i];
                if (!setup_sender_fn (idx)) {
                    fatal_error.store (true, std::memory_order_release);
                    break;
                }
            }

            setup_done_workers.fetch_add (1, std::memory_order_release);

            while (!send_start.load (std::memory_order_acquire)
                   && !fatal_error.load (std::memory_order_acquire)) {
                std::this_thread::yield ();
            }

            size_t rr = 0;
            while (!fatal_error.load (std::memory_order_acquire)) {
                const int current_phase = phase.load (std::memory_order_acquire);
                if (current_phase == multi_phase_stop)
                    break;

                bool progressed = false;
                const int recv_budget =
                  std::max (1, settings.recv_batch
                                 / static_cast<int> (std::max<size_t> (1, owned.size ())));

                for (size_t i = 0; i < owned.size (); ++i) {
                    const size_t idx = owned[i];
                    const int rc = recv_client_batch_fn (idx, recv_budget);
                    if (rc < 0) {
                        fatal_error.store (true, std::memory_order_release);
                        break;
                    }
                    if (rc > 0) {
                        progressed = true;
                        recv_total.fetch_add (rc, std::memory_order_relaxed);
                        if (current_phase == multi_phase_measure)
                            measure_recv.fetch_add (rc, std::memory_order_relaxed);
                        else if (current_phase == multi_phase_drain)
                            drain_recv.fetch_add (rc, std::memory_order_relaxed);
                    }
                }
                if (fatal_error.load (std::memory_order_acquire))
                    break;

                if (current_phase == multi_phase_measure
                    && send_running.load (std::memory_order_acquire)) {
                    for (size_t i = 0; i < owned.size (); ++i) {
                        if (window_exhausted ())
                            break;

                        const size_t idx = owned[rr % owned.size ()];
                        ++rr;
                        const multi_send_result_t rc = send_fn (idx);
                        if (rc == multi_send_ok) {
                            progressed = true;
                            send_total.fetch_add (1, std::memory_order_relaxed);
                            send_success.fetch_add (1, std::memory_order_relaxed);
                            if (!sender_ready[idx]) {
                                sender_ready[idx] = 1;
                                ready_total.fetch_add (1, std::memory_order_relaxed);
                            }
                            continue;
                        }
                        if (rc == multi_send_error) {
                            fatal_error.store (true, std::memory_order_release);
                            break;
                        }
                    }
                }

                if (!progressed)
                    send_backoff ();
            }

            for (size_t i = 0; i < owned.size (); ++i)
                cleanup_sender_fn (owned[i]);
        }));
    }

    std::thread relay_thread ([&] () {
        while (!fatal_error.load (std::memory_order_acquire)) {
            const int current_phase = phase.load (std::memory_order_acquire);
            if (current_phase == multi_phase_stop)
                break;

            const int rc = relay_batch_fn (
              static_cast<multi_bench_phase_t> (current_phase), settings.recv_batch,
              10);
            if (rc < 0) {
                fatal_error.store (true, std::memory_order_release);
                break;
            }
            if (rc == 0)
                std::this_thread::sleep_for (std::chrono::microseconds (50));
        }
    });

    const auto connect_start = std::chrono::steady_clock::now ();
    while (setup_done_workers.load (std::memory_order_acquire) < worker_count
           && !fatal_error.load (std::memory_order_acquire)) {
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    const auto connect_end = std::chrono::steady_clock::now ();
    const double connect_ms =
      std::chrono::duration_cast<std::chrono::duration<double, std::milli> > (
        connect_end - connect_start)
        .count ();

    const auto ready_wait_start = std::chrono::steady_clock::now ();
    if (!fatal_error.load (std::memory_order_acquire) && !pre_start_fn ())
        fatal_error.store (true, std::memory_order_release);
    const auto ready_wait_end = std::chrono::steady_clock::now ();
    const double ready_wait_ms =
      std::chrono::duration_cast<std::chrono::duration<double, std::milli> > (
        ready_wait_end - ready_wait_start)
        .count ();

    if (require_ready_barrier && !fatal_error.load (std::memory_order_acquire)) {
        const auto ready_deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (
            std::max (0, settings.connect_ready_timeout_ms));
        while (ready_total.load (std::memory_order_relaxed)
                 < static_cast<long> (sender_count)
               && !fatal_error.load (std::memory_order_acquire)
               && std::chrono::steady_clock::now () < ready_deadline) {
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        if (ready_total.load (std::memory_order_relaxed)
              < static_cast<long> (sender_count)) {
            fatal_error.store (true, std::memory_order_release);
        }
    }

    if (settings.settle_ms > 0)
        std::this_thread::sleep_for (std::chrono::milliseconds (settings.settle_ms));

    phase.store (multi_phase_measure, std::memory_order_release);
    send_start.store (true, std::memory_order_release);
    if (!fatal_error.load (std::memory_order_acquire)) {
        const long warmup_delta = run_multi_warmup_stage (
          settings, phase, fatal_error, send_total, recv_total);
        warmup_recv.store (warmup_delta, std::memory_order_relaxed);
        if (!fatal_error.load (std::memory_order_acquire))
            phase.store (multi_phase_measure, std::memory_order_release);
    }

    const long measure_recv_start = recv_total.load (std::memory_order_acquire);
    const auto measure_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (std::max (1, settings.duration_seconds));
    while (!fatal_error.load (std::memory_order_acquire)
           && std::chrono::steady_clock::now () < measure_deadline) {
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    const long measure_recv_end = recv_total.load (std::memory_order_acquire);

    phase.store (multi_phase_drain, std::memory_order_release);
    send_running.store (false, std::memory_order_release);

    const auto drain_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (std::max (0, settings.drain_ms));
    while (!fatal_error.load (std::memory_order_acquire)
           && std::chrono::steady_clock::now () < drain_deadline) {
        const long inflight =
          send_total.load (std::memory_order_relaxed)
          - recv_total.load (std::memory_order_relaxed);
        if (inflight <= 0)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    phase.store (multi_phase_stop, std::memory_order_release);
    if (relay_thread.joinable ())
        relay_thread.join ();
    for (size_t i = 0; i < sender_threads.size (); ++i) {
        if (sender_threads[i].joinable ())
            sender_threads[i].join ();
    }

    result.warmup_recv = warmup_recv.load (std::memory_order_relaxed);
    result.measure_recv =
      std::max<long> (0, measure_recv_end - measure_recv_start);
    result.drain_recv = drain_recv.load (std::memory_order_relaxed);
    result.measure_send_ok = send_success.load (std::memory_order_relaxed);
    result.connect_ms = connect_ms;
    result.ready_wait_ms = ready_wait_ms;
    result.failed = fatal_error.load (std::memory_order_acquire);
    return result;
}

#endif
