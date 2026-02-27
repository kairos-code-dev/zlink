#ifndef PERF_COMMON_MULTI_HPP
#define PERF_COMMON_MULTI_HPP

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

struct multi_bench_settings_t
{
    size_t clients;
    int hwm;
    int warmup_seconds;
    int active_warmup;
    int duration_seconds;
    int settle_ms;
    int drain_ms;
    int size_transition_drain_ms;
    int client_workers;
    int client_poll_timeout_ms;
    int client_idle_sleep_us;
    int send_backoff_us;
    int connect_ready_timeout_ms;
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

inline size_t resolve_multi_send_attempts (size_t owned_size, size_t payload_size)
{
    const size_t safe_owned = std::max<size_t> (1, owned_size);
    const int explicit_batch =
      resolve_multi_int_env ("PERF_MULTI_SEND_BATCH", 0, 0);
    if (explicit_batch > 0) {
        return std::max<size_t> (
          1,
          std::min<size_t> (safe_owned, static_cast<size_t> (explicit_batch)));
    }

    size_t capped = safe_owned;
    if (payload_size >= 262144)
        capped = std::min<size_t> (safe_owned, 8);
    else if (payload_size >= 131072)
        capped = std::min<size_t> (safe_owned, 16);
    else if (payload_size >= 65536)
        capped = std::min<size_t> (safe_owned, 32);

    return std::max<size_t> (1, capped);
}

inline int resolve_multi_default_hwm (const char *pattern, int clients)
{
    (void) pattern;
    (void) clients;
    return 1000;
}

inline int resolve_multi_default_client_workers ()
{
    return 4;
}

inline int resolve_multi_default_clients (const char *pattern)
{
    (void) pattern;
    return 1000;
}

inline multi_bench_settings_t resolve_multi_bench_settings ()
{
    multi_bench_settings_t settings;
    const char *pattern = std::getenv ("PERF_MULTI_PATTERN");
    const int resolved_clients = resolve_multi_int_env (
      "PERF_MULTI_CLIENTS", resolve_multi_default_clients (pattern), 1);
    settings.clients = static_cast<size_t> (resolved_clients);
    settings.hwm = resolve_multi_int_env (
      "PERF_MULTI_HWM", resolve_multi_default_hwm (pattern, resolved_clients), 1);
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
    settings.client_workers = resolve_multi_int_env (
      "PERF_MULTI_CLIENT_WORKERS", resolve_multi_default_client_workers (), 1);
    settings.client_poll_timeout_ms = resolve_multi_int_env (
      "PERF_MULTI_CLIENT_POLL_TIMEOUT_MS", 1, 0);
    settings.client_idle_sleep_us = resolve_multi_int_env (
      "PERF_MULTI_CLIENT_IDLE_SLEEP_US", 0, 0);
    settings.send_backoff_us =
      resolve_multi_int_env ("PERF_MULTI_SEND_BACKOFF_US", 20, 0);
    settings.connect_ready_timeout_ms =
      resolve_multi_int_env ("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000, 0);
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


#endif
