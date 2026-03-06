#ifndef PERF_ENTRY_HPP
#define PERF_ENTRY_HPP

#include <cstdlib>

inline void set_perf_pattern_env (const char *pattern)
{
    if (!pattern || !*pattern)
        return;
#if defined(_WIN32)
    _putenv_s ("PERF_PATTERN", pattern);
#else
    setenv ("PERF_PATTERN", pattern, 1);
#endif
}

#endif
