#ifndef PERF_MULTI_ENTRY_HPP
#define PERF_MULTI_ENTRY_HPP

#include <cstdlib>
#include "perf_common_multi.hpp"

inline void set_perf_multi_pattern_env (const char *pattern)
{
    if (!pattern || !*pattern)
        return;
#if defined(_WIN32)
    _putenv_s ("PERF_MULTI_PATTERN", pattern);
#else
    setenv ("PERF_MULTI_PATTERN", pattern, 1);
#endif
}

typedef bench_settings_t multi_bench_settings_t;

inline bench_settings_t resolve_multi_bench_settings ()
{
    return resolve_bench_settings ();
}

inline int resolve_multi_int_env (const char *env_name,
                                   int default_value,
                                   int min_value)
{
    return resolve_int_env (env_name, default_value, min_value);
}

inline size_t resolve_multi_service_clients (size_t requested_clients)
{
    return resolve_service_clients (requested_clients);
}

#endif
