#ifndef BENCH_COMMON_MULTI_HPP
#define BENCH_COMMON_MULTI_HPP

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

struct multi_bench_settings_t
{
    size_t clients;
    int hwm;
    int warmup_seconds;
    int active_warmup;
    int duration_seconds;
    int settle_ms;
    int client_poll_timeout_ms;
    int connect_ready_timeout_ms;
    int inflight;
    int recv_batch;
    int drain_ms;
    int send_workers;
    int send_backoff_us;
};

enum multi_send_result_t
{
    multi_send_ok = 0,
    multi_send_would_block = 1,
    multi_send_error = 2
};

enum multi_bench_phase_t
{
    multi_phase_warmup = 0,
    multi_phase_measure = 1,
    multi_phase_drain = 2
};

struct multi_bench_result_t
{
    multi_bench_result_t () :
        failed (false),
        connect_ms (0.0),
        ready_wait_ms (0.0),
        warmup_recv (0),
        measure_recv (0),
        measure_send_ok (0),
        drain_recv (0)
    {
    }

    bool failed;
    double connect_ms;
    double ready_wait_ms;
    long warmup_recv;
    long measure_recv;
    long measure_send_ok;
    long drain_recv;
};

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
    if (parsed > static_cast<long> (INT_MAX))
        return INT_MAX;
    return static_cast<int> (parsed);
}

inline bool is_stream_multi_pattern (const char *pattern)
{
    if (!pattern)
        return false;
    return std::strcmp (pattern, "MULTI_STREAM") == 0
           || std::strcmp (pattern, "MULTI_STREAM_CALLBACK") == 0
           || std::strcmp (pattern, "MULTI_STREAM_LEN32BE") == 0;
}

inline multi_bench_settings_t resolve_multi_bench_settings ()
{
    multi_bench_settings_t settings;
    const char *pattern = std::getenv ("BENCH_MULTI_PATTERN");
    const bool is_stream = is_stream_multi_pattern (pattern);

    const int default_clients = is_stream ? 1000 : 1000;
    settings.clients = static_cast<size_t> (
      resolve_multi_int_env ("BENCH_MULTI_CLIENTS", default_clients, 1));
    settings.hwm = resolve_multi_int_env ("BENCH_MULTI_HWM", 100000, 1);
    settings.warmup_seconds =
      resolve_multi_int_env ("BENCH_MULTI_WARMUP_SECONDS", 3, 0);
    settings.active_warmup =
      resolve_multi_int_env ("BENCH_MULTI_ACTIVE_WARMUP", 0, 0);
    settings.duration_seconds =
      resolve_multi_int_env ("BENCH_MULTI_DURATION_SECONDS", 5, 1);
    settings.settle_ms = resolve_multi_int_env ("BENCH_MULTI_SETTLE_MS", 500, 0);
    settings.client_poll_timeout_ms = resolve_multi_int_env (
      "BENCH_MULTI_CLIENT_POLL_TIMEOUT_MS", 0, 0);
    settings.connect_ready_timeout_ms =
      resolve_multi_int_env ("BENCH_MULTI_CONNECT_READY_TIMEOUT_MS", 5000, 0);
    settings.inflight = resolve_multi_int_env ("BENCH_MULTI_INFLIGHT", 30, 1);
    settings.recv_batch = resolve_multi_int_env ("BENCH_MULTI_RECV_BATCH", 64, 1);
    settings.drain_ms = resolve_multi_int_env ("BENCH_MULTI_DRAIN_MS", 300, 0);
    settings.send_workers = resolve_multi_int_env ("BENCH_MULTI_SEND_WORKERS", 1, 1);
    settings.send_backoff_us =
      resolve_multi_int_env ("BENCH_MULTI_SEND_BACKOFF_US", 0, 0);
    return settings;
}

inline int resolve_multi_inflight_for_size (const multi_bench_settings_t &settings,
                                            size_t msg_size)
{
    int inflight = std::max (1, settings.inflight);
    if (msg_size >= 262144)
        inflight = std::max (1, inflight / 4);
    else if (msg_size >= 65536)
        inflight = std::max (1, inflight / 2);
    return inflight;
}

inline int resolve_multi_stream_inflight_for_size (
  const multi_bench_settings_t &settings,
  size_t msg_size)
{
    int inflight = std::max (1, settings.inflight);
    if (msg_size >= 65536)
        inflight = std::max (1, inflight / 2);
    return inflight;
}

inline void print_prep_result (const std::string &lib_type,
                               const std::string &pattern,
                               const std::string &transport,
                               size_t size,
                               double connect_ms,
                               double ready_wait_ms)
{
    std::cout << "PREP," << lib_type << "," << pattern << "," << transport << ","
              << size << ",connect_ms," << std::fixed << std::setprecision (2)
              << connect_ms << ",ready_wait_ms," << std::fixed
              << std::setprecision (2) << ready_wait_ms << std::endl;
}

template <typename ConnectFn>
inline bool connect_clients_concurrently (const std::vector<void *> &sockets,
                                          const std::string &endpoint,
                                          ConnectFn connect_fn,
                                          int max_concurrency)
{
    if (sockets.empty ())
        return true;

    std::atomic<bool> ok (true);
    const size_t total = sockets.size ();
    size_t start = 0;
    const size_t chunk =
      max_concurrency > 0 ? static_cast<size_t> (max_concurrency) : 1;

    while (start < total) {
        const size_t end = start + chunk < total ? start + chunk : total;
        std::vector<std::thread> workers;
        workers.reserve (end - start);
        for (size_t i = start; i < end; ++i) {
            workers.push_back (std::thread ([&, i] () {
                if (!connect_fn (sockets[i], endpoint))
                    ok.store (false, std::memory_order_relaxed);
            }));
        }
        for (size_t i = 0; i < workers.size (); ++i)
            workers[i].join ();

        if (!ok.load (std::memory_order_relaxed))
            return false;
        start = end;
    }

    return true;
}

template <typename ConnectFn>
inline bool connect_clients_concurrently (const std::vector<void *> &sockets,
                                          const std::string &endpoint,
                                          ConnectFn connect_fn)
{
    return connect_clients_concurrently (
      sockets, endpoint, connect_fn, resolve_multi_int_env ("BENCH_MULTI_CONNECT_CONCURRENCY", 128, 1));
}

template <typename SetupFn,
          typename SendFn,
          typename RelayFn,
          typename RecvClientFn,
          typename CleanupFn,
          typename ReadyFn>
inline multi_bench_result_t run_multi_phase_socket_owned_rtt_benchmark (
  size_t client_count,
  const multi_bench_settings_t &settings,
  SetupFn setup_client_fn,
  SendFn send_fn,
  RelayFn relay_fn,
  RecvClientFn recv_client_fn,
  CleanupFn cleanup_client_fn,
  ReadyFn ready_gate_fn,
  bool /*reserved*/)
{
    multi_bench_result_t result;

    const auto connect_start = std::chrono::steady_clock::now ();
    size_t connected = 0;
    for (; connected < client_count; ++connected) {
        if (!setup_client_fn (connected)) {
            result.failed = true;
            for (size_t i = 0; i < connected; ++i)
                cleanup_client_fn (i);
            return result;
        }
    }
    const auto connect_end = std::chrono::steady_clock::now ();
    result.connect_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli> > (
      connect_end - connect_start)
                          .count ();

    const auto ready_start = std::chrono::steady_clock::now ();
    if (!ready_gate_fn ()) {
        result.failed = true;
        for (size_t i = 0; i < client_count; ++i)
            cleanup_client_fn (i);
        return result;
    }
    const auto ready_end = std::chrono::steady_clock::now ();
    result.ready_wait_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli> > (
      ready_end - ready_start)
                             .count ();

    if (settings.settle_ms > 0)
        std::this_thread::sleep_for (std::chrono::milliseconds (settings.settle_ms));

    std::vector<int> pending (client_count, 0);
    size_t rr = 0;
    const int recv_batch = std::max (1, settings.recv_batch);
    const int poll_timeout_ms = std::max (0, settings.client_poll_timeout_ms);

    const auto run_phase = [&] (multi_bench_phase_t phase, double seconds) -> bool {
        if (seconds <= 0.0)
            return true;
        const auto deadline =
          std::chrono::steady_clock::now () + std::chrono::duration<double> (seconds);
        while (std::chrono::steady_clock::now () < deadline) {
            for (size_t n = 0; n < client_count; ++n) {
                const size_t idx = rr % client_count;
                ++rr;
                if (pending[idx] >= std::max (1, settings.inflight))
                    continue;
                const multi_send_result_t send_status = send_fn (idx);
                if (send_status == multi_send_error)
                    return false;
                if (send_status == multi_send_ok) {
                    ++pending[idx];
                    if (phase == multi_phase_measure)
                        ++result.measure_send_ok;
                }
            }

            const int relayed = relay_fn (phase, recv_batch, poll_timeout_ms);
            if (relayed < 0)
                return false;
            if (phase == multi_phase_warmup)
                result.warmup_recv += relayed;
            else if (phase == multi_phase_measure)
                result.measure_recv += relayed;
            else
                result.drain_recv += relayed;

            for (size_t i = 0; i < client_count; ++i) {
                const int got = recv_client_fn (i, recv_batch);
                if (got < 0)
                    return false;
                pending[i] = std::max (0, pending[i] - got);
            }

            if (settings.send_backoff_us > 0) {
                std::this_thread::sleep_for (
                  std::chrono::microseconds (settings.send_backoff_us));
            } else {
                std::this_thread::yield ();
            }
        }
        return true;
    };

    const bool warmup_ok = run_phase (
      multi_phase_warmup,
      static_cast<double> (std::max (0, settings.warmup_seconds)));
    const bool measure_ok = warmup_ok
                            && run_phase (
                              multi_phase_measure,
                              static_cast<double> (
                                std::max (1, settings.duration_seconds)));
    bool drain_ok = true;
    if (measure_ok && settings.drain_ms > 0) {
        drain_ok = run_phase (
          multi_phase_drain,
          static_cast<double> (settings.drain_ms) / 1000.0);
    }

    for (size_t i = 0; i < client_count; ++i)
        cleanup_client_fn (i);

    result.failed = !(warmup_ok && measure_ok && drain_ok);
    return result;
}

template <typename SetupFn,
          typename SendFn,
          typename RelayFn,
          typename CleanupFn,
          typename ReadyFn>
inline multi_bench_result_t run_multi_phase_benchmark_with_sender_lifecycle_batched (
  size_t sender_count,
  const multi_bench_settings_t &settings,
  int send_batch,
  SetupFn setup_sender_fn,
  SendFn send_fn,
  RelayFn relay_fn,
  CleanupFn cleanup_sender_fn,
  ReadyFn ready_gate_fn,
  bool /*reserved*/)
{
    multi_bench_result_t result;

    const auto connect_start = std::chrono::steady_clock::now ();
    size_t connected = 0;
    for (; connected < sender_count; ++connected) {
        if (!setup_sender_fn (connected)) {
            result.failed = true;
            for (size_t i = 0; i < connected; ++i)
                cleanup_sender_fn (i);
            return result;
        }
    }
    const auto connect_end = std::chrono::steady_clock::now ();
    result.connect_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli> > (
      connect_end - connect_start)
                          .count ();

    const auto ready_start = std::chrono::steady_clock::now ();
    if (!ready_gate_fn ()) {
        result.failed = true;
        for (size_t i = 0; i < sender_count; ++i)
            cleanup_sender_fn (i);
        return result;
    }
    const auto ready_end = std::chrono::steady_clock::now ();
    result.ready_wait_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli> > (
      ready_end - ready_start)
                             .count ();

    if (settings.settle_ms > 0)
        std::this_thread::sleep_for (std::chrono::milliseconds (settings.settle_ms));

    const int batch = std::max (1, send_batch);
    size_t rr = 0;

    const auto run_phase = [&] (multi_bench_phase_t phase, double seconds) -> bool {
        if (seconds <= 0.0)
            return true;
        const auto deadline =
          std::chrono::steady_clock::now () + std::chrono::duration<double> (seconds);
        while (std::chrono::steady_clock::now () < deadline) {
            for (size_t n = 0; n < sender_count; ++n) {
                const size_t idx = rr % sender_count;
                ++rr;
                for (int b = 0; b < batch; ++b) {
                    const multi_send_result_t send_status = send_fn (idx);
                    if (send_status == multi_send_error)
                        return false;
                    if (send_status == multi_send_ok && phase == multi_phase_measure)
                        ++result.measure_send_ok;
                    if (send_status == multi_send_would_block)
                        break;
                }
            }

            const int relayed = relay_fn (phase);
            if (relayed < 0)
                return false;
            if (phase == multi_phase_warmup)
                result.warmup_recv += relayed;
            else if (phase == multi_phase_measure)
                result.measure_recv += relayed;
            else
                result.drain_recv += relayed;

            if (settings.send_backoff_us > 0) {
                std::this_thread::sleep_for (
                  std::chrono::microseconds (settings.send_backoff_us));
            } else {
                std::this_thread::yield ();
            }
        }
        return true;
    };

    const bool warmup_ok = run_phase (
      multi_phase_warmup,
      static_cast<double> (std::max (0, settings.warmup_seconds)));
    const bool measure_ok = warmup_ok
                            && run_phase (
                              multi_phase_measure,
                              static_cast<double> (
                                std::max (1, settings.duration_seconds)));
    bool drain_ok = true;
    if (measure_ok && settings.drain_ms > 0) {
        drain_ok = run_phase (
          multi_phase_drain,
          static_cast<double> (settings.drain_ms) / 1000.0);
    }

    for (size_t i = 0; i < sender_count; ++i)
        cleanup_sender_fn (i);

    result.failed = !(warmup_ok && measure_ok && drain_ok);
    return result;
}

#endif
