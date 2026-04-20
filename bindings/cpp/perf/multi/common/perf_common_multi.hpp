#ifndef PERF_MULTI_COMMON_MULTI_HPP
#define PERF_MULTI_COMMON_MULTI_HPP

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <climits>
#include <cstring>
#include <iostream>
#include <string>

namespace perf {
namespace multi {

struct multi_bench_settings_t
{
    size_t clients;
    int hwm;
    int sndhwm;
    int rcvhwm;
    int duration_seconds;
    int client_poll_timeout_ms;
    int connect_ready_timeout_ms;
    int sndtimeo_ms;
    int rcvtimeo_ms;
    int monitor_hwm;
    int server_bind_port;
};

inline int parse_positive_env (const char *name, int default_value)
{
    if (!name)
        return default_value;

    const char *value = std::getenv (name);
    if (!value || !*value)
        return default_value;

    errno = 0;
    char *end = NULL;
    const long parsed = std::strtol (value, &end, 10);
    if (errno != 0 || end == value)
        return default_value;
    if (parsed > INT_MAX)
        return INT_MAX;
    if (parsed < INT_MIN)
        return INT_MIN;
    return static_cast<int> (parsed);
}

inline bool is_stream_pattern (const char *pattern)
{
    if (!pattern || !*pattern)
        return false;
    return std::strcmp (pattern, "STREAM") == 0
           || std::strcmp (pattern, "MULTI_STREAM") == 0;
}

inline std::string resolve_multi_perf_recv_mode ()
{
    return "recv";
}

inline bool multi_perf_callback_mode ()
{
    return false;
}

inline bool multi_perf_validate_recv_mode_for_pattern (const char *pattern)
{
    return pattern && *pattern;
}

inline size_t resolve_multi_default_clients (const std::string &pattern)
{
    return is_stream_pattern (pattern.c_str ()) ? static_cast<size_t> (10000)
                                                : static_cast<size_t> (100);
}

inline int resolve_multi_default_hwm (const std::string &pattern, size_t)
{
    (void) pattern;
    return 1000;
}

inline multi_bench_settings_t resolve_multi_bench_settings ()
{
    const char *pattern_env = std::getenv ("PERF_PATTERN");
    const std::string pattern = pattern_env ? pattern_env : "";

    const size_t default_clients = resolve_multi_default_clients (pattern);
    const int clients = std::max (
      1,
      parse_positive_env (
        "PERF_MULTI_CLIENTS", static_cast<int> (default_clients)));

    const int default_hwm =
      resolve_multi_default_hwm (pattern, static_cast<size_t> (clients));

    multi_bench_settings_t out;
    out.clients = static_cast<size_t> (clients);
    out.hwm = std::max (1, parse_positive_env ("PERF_MULTI_HWM", default_hwm));
    out.sndhwm = std::max (
      1, parse_positive_env ("PERF_MULTI_SNDHWM", out.hwm));
    out.rcvhwm = std::max (
      1, parse_positive_env ("PERF_MULTI_RCVHWM", out.hwm));
    out.duration_seconds = std::max (
      1, parse_positive_env ("PERF_MULTI_DURATION_SECONDS", 5));
    out.client_poll_timeout_ms = 0;
    out.connect_ready_timeout_ms =
      std::max (0, parse_positive_env ("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000));
    out.sndtimeo_ms =
      std::max (0, parse_positive_env ("PERF_MULTI_SNDTIMEO_MS", 200));
    out.rcvtimeo_ms =
      std::max (0, parse_positive_env ("PERF_MULTI_RCVTIMEO_MS", 200));
    out.monitor_hwm =
      std::max (1, parse_positive_env ("PERF_MULTI_MONITOR_HWM", 1000));
    out.server_bind_port =
      std::max (0, parse_positive_env ("PERF_MULTI_SERVER_BIND_PORT", 0));

    return out;
}

} // namespace multi
} // namespace perf

#endif
