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
    int warmup_seconds;
    int active_warmup;
    int duration_seconds;
    int settle_ms;
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

inline int parse_positive_env_alias (const char *name,
                                     const char *alias,
                                     int default_value,
                                     int min_value)
{
    int value = parse_positive_env (name, INT_MIN);
    if (value == INT_MIN)
        value = parse_positive_env (alias, default_value);
    if (value < min_value)
        return min_value;
    return value;
}

inline bool is_stream_pattern (const char *pattern)
{
    if (!pattern || !*pattern)
        return false;
    return std::strcmp (pattern, "STREAM") == 0
           || std::strcmp (pattern, "MULTI_STREAM") == 0;
}

inline bool callback_supported_for_pattern (const char *pattern)
{
    if (!pattern || !*pattern)
        return false;

    return std::strcmp (pattern, "SPOT") == 0
           || std::strcmp (pattern, "MULTI_SPOT") == 0
           || std::strcmp (pattern, "STREAM") == 0
           || std::strcmp (pattern, "MULTI_STREAM") == 0;
}

inline std::string resolve_multi_perf_recv_mode ()
{
    const char *env = std::getenv ("PERF_RECV_MODE");
    if (!env || !*env)
        return "recv";

    std::string mode (env);
    std::transform (
      mode.begin (), mode.end (), mode.begin (), [](unsigned char c_) {
          return static_cast<char> (std::tolower (c_));
      });

    if (mode != "recv" && mode != "callback") {
        std::cerr << "policy violation: invalid --recv mode " << mode
                  << std::endl;
        std::exit (1);
    }

    return mode;
}

inline bool multi_perf_callback_mode ()
{
    return resolve_multi_perf_recv_mode () == "callback";
}

inline bool multi_perf_validate_recv_mode_for_pattern (const char *pattern)
{
    if (!pattern || !*pattern)
        return false;

    if (multi_perf_callback_mode () && !callback_supported_for_pattern (pattern)) {
        std::cerr << "policy violation: --recv callback unsupported for "
                  << pattern << std::endl;
        return false;
    }

    return true;
}

inline size_t resolve_multi_default_clients (const std::string &pattern)
{
    return is_stream_pattern (pattern.c_str ()) ? static_cast<size_t> (10000)
                                                : static_cast<size_t> (100);
}

inline int resolve_multi_default_hwm (const std::string &pattern, size_t)
{
    return is_stream_pattern (pattern.c_str ()) ? 10 : 100;
}

inline multi_bench_settings_t resolve_multi_bench_settings ()
{
    const char *pattern_env = std::getenv ("PERF_PATTERN");
    const std::string pattern = pattern_env ? pattern_env : "";

    const size_t default_clients = resolve_multi_default_clients (pattern);
    const int clients = parse_positive_env_alias (
      "PERF_CLIENTS",
      "PERF_MULTI_CLIENTS",
      static_cast<int> (default_clients),
      1);

    const int default_hwm =
      resolve_multi_default_hwm (pattern, static_cast<size_t> (clients));

    multi_bench_settings_t out;
    out.clients = static_cast<size_t> (clients);
    out.hwm = parse_positive_env_alias ("PERF_HWM", "PERF_MULTI_HWM", default_hwm, 1);
    out.sndhwm = parse_positive_env_alias (
      "PERF_SNDHWM", "PERF_MULTI_SNDHWM", out.hwm, 1);
    out.rcvhwm = parse_positive_env_alias (
      "PERF_RCVHWM", "PERF_MULTI_RCVHWM", out.hwm, 1);
    out.warmup_seconds = parse_positive_env_alias (
      "PERF_WARMUP_SECONDS", "PERF_MULTI_WARMUP_SECONDS", 2, 0);
    out.active_warmup = parse_positive_env ("PERF_ACTIVE_WARMUP", 0) > 0 ? 1 : 0;
    out.duration_seconds = parse_positive_env_alias (
      "PERF_DURATION_SECONDS", "PERF_MULTI_DURATION_SECONDS", 5, 1);
    out.settle_ms = parse_positive_env ("PERF_SETTLE_MS", 500);
    out.client_poll_timeout_ms = parse_positive_env (
      "PERF_CLIENT_POLL_TIMEOUT_MS", 0);
    out.connect_ready_timeout_ms = parse_positive_env_alias (
      "PERF_CONNECT_READY_TIMEOUT_MS",
      "PERF_MULTI_CONNECT_READY_TIMEOUT_MS",
      5000,
      0);
    out.sndtimeo_ms = parse_positive_env_alias (
      "PERF_SNDTIMEO_MS", "PERF_MULTI_SNDTIMEO_MS", 200, 0);
    out.rcvtimeo_ms = parse_positive_env_alias (
      "PERF_RCVTIMEO_MS", "PERF_MULTI_RCVTIMEO_MS", 200, 0);
    out.monitor_hwm = parse_positive_env_alias (
      "PERF_MONITOR_HWM", "PERF_MULTI_MONITOR_HWM", 1000, 1);
    out.server_bind_port = parse_positive_env_alias (
      "PERF_SERVER_BIND_PORT", "PERF_MULTI_SERVER_BIND_PORT", 0, 0);

    return out;
}

} // namespace multi
} // namespace perf

#endif
