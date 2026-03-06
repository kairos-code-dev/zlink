#ifndef PERF_SETTINGS_COMMON_HPP
#define PERF_SETTINGS_COMMON_HPP

#include <algorithm>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <string>

struct bench_settings_t
{
    size_t clients;
    int hwm;
    int warmup_seconds;
    int active_warmup;
    int duration_seconds;
    int settle_ms;
    int client_poll_timeout_ms;
    int connect_ready_timeout_ms;
};

inline int resolve_int_env (const char *env_name,
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

inline int resolve_default_hwm (const char *pattern, int clients)
{
    (void) clients;

    if (pattern && *pattern) {
        const bool is_stream_variant =
          std::strcmp (pattern, "STREAM") == 0
          || std::strcmp (pattern, "STREAM_CALLBACK") == 0
          || std::strcmp (pattern, "STREAM_LEN32BE") == 0;
        if (is_stream_variant)
            return 10;
    }

    return 100;
}

inline int resolve_default_clients (const char *pattern)
{
    if (pattern && *pattern) {
        const bool is_stream_variant =
          std::strcmp (pattern, "STREAM") == 0
          || std::strcmp (pattern, "STREAM_CALLBACK") == 0
          || std::strcmp (pattern, "STREAM_LEN32BE") == 0;
        if (is_stream_variant)
            return 10000;
    }

    return 100;
}

inline size_t resolve_service_clients (size_t requested_clients)
{
    const int override_clients = resolve_int_env (
      "PERF_SERVICE_CLIENTS",
      0,
      1);
    if (override_clients <= 0)
        return requested_clients;

    return std::min (
      requested_clients,
      static_cast<size_t> (override_clients));
}

inline bench_settings_t resolve_bench_settings ()
{
    bench_settings_t settings;
    const char *pattern = std::getenv ("PERF_PATTERN");
    const int resolved_clients = resolve_int_env (
      "PERF_CLIENTS", resolve_default_clients (pattern), 1);
    settings.clients = static_cast<size_t> (resolved_clients);
    settings.hwm = resolve_int_env (
      "PERF_HWM", resolve_default_hwm (pattern, resolved_clients), 1);
    settings.warmup_seconds =
      resolve_int_env ("PERF_WARMUP_SECONDS", 2, 0);
    settings.active_warmup =
      resolve_int_env ("PERF_ACTIVE_WARMUP", 0, 0);
    settings.duration_seconds =
      resolve_int_env ("PERF_DURATION_SECONDS", 5, 1);
    settings.settle_ms = resolve_int_env ("PERF_SETTLE_MS", 500, 0);
    settings.client_poll_timeout_ms = resolve_int_env (
      "PERF_CLIENT_POLL_TIMEOUT_MS", 0, 0);
    settings.connect_ready_timeout_ms =
      resolve_int_env ("PERF_CONNECT_READY_TIMEOUT_MS", 5000, 0);
    return settings;
}

inline bool parse_queue_probe_command (const std::string &line,
                                       size_t *msg_size_out)
{
    if (!msg_size_out)
        return false;

    static const char k_prefix[] = "QUEUE,";
    if (line.compare (0, sizeof (k_prefix) - 1, k_prefix) != 0)
        return false;

    const char *value = line.c_str () + (sizeof (k_prefix) - 1);
    if (!value || *value == '\0')
        return false;

    errno = 0;
    char *end = NULL;
    const unsigned long parsed = std::strtoul (value, &end, 10);
    if (errno != 0 || end == value || !end || *end != '\0' || parsed == 0)
        return false;

    *msg_size_out = static_cast<size_t> (parsed);
    return true;
}


#endif
