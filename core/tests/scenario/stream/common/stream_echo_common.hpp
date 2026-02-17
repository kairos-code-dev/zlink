#ifndef STREAM_ECHO_COMMON_HPP_INCLUDED
#define STREAM_ECHO_COMMON_HPP_INCLUDED

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace stream_echo {

inline uint32_t load_u32_be (const unsigned char *p)
{
    return (static_cast<uint32_t> (p[0]) << 24)
           | (static_cast<uint32_t> (p[1]) << 16)
           | (static_cast<uint32_t> (p[2]) << 8)
           | static_cast<uint32_t> (p[3]);
}

inline void store_u32_be (unsigned char *p, uint32_t v)
{
    p[0] = static_cast<unsigned char> ((v >> 24) & 0xFF);
    p[1] = static_cast<unsigned char> ((v >> 16) & 0xFF);
    p[2] = static_cast<unsigned char> ((v >> 8) & 0xFF);
    p[3] = static_cast<unsigned char> (v & 0xFF);
}

inline uint64_t load_u64_be (const unsigned char *p)
{
    return (static_cast<uint64_t> (p[0]) << 56)
           | (static_cast<uint64_t> (p[1]) << 48)
           | (static_cast<uint64_t> (p[2]) << 40)
           | (static_cast<uint64_t> (p[3]) << 32)
           | (static_cast<uint64_t> (p[4]) << 24)
           | (static_cast<uint64_t> (p[5]) << 16)
           | (static_cast<uint64_t> (p[6]) << 8)
           | static_cast<uint64_t> (p[7]);
}

inline void store_u64_be (unsigned char *p, uint64_t v)
{
    p[0] = static_cast<unsigned char> ((v >> 56) & 0xFF);
    p[1] = static_cast<unsigned char> ((v >> 48) & 0xFF);
    p[2] = static_cast<unsigned char> ((v >> 40) & 0xFF);
    p[3] = static_cast<unsigned char> ((v >> 32) & 0xFF);
    p[4] = static_cast<unsigned char> ((v >> 24) & 0xFF);
    p[5] = static_cast<unsigned char> ((v >> 16) & 0xFF);
    p[6] = static_cast<unsigned char> ((v >> 8) & 0xFF);
    p[7] = static_cast<unsigned char> (v & 0xFF);
}

inline uint64_t now_ns ()
{
    const std::chrono::steady_clock::time_point now =
      std::chrono::steady_clock::now ();
    return static_cast<uint64_t> (
      std::chrono::duration_cast<std::chrono::nanoseconds> (
        now.time_since_epoch ())
        .count ());
}

inline double percentile_us (std::vector<double> samples, double q)
{
    if (samples.empty ())
        return 0.0;

    if (q <= 0.0)
        q = 0.0;
    if (q >= 1.0)
        q = 1.0;

    std::sort (samples.begin (), samples.end ());
    const size_t last = samples.size () - 1;
    const double idx = q * static_cast<double> (last);
    const size_t lo = static_cast<size_t> (idx);
    const size_t hi = std::min<size_t> (last, lo + 1);
    const double frac = idx - static_cast<double> (lo);
    if (lo == hi)
        return samples[lo];
    return samples[lo] + (samples[hi] - samples[lo]) * frac;
}

struct summary_stats_t
{
    double p50_us;
    double p95_us;
    double p99_us;

    summary_stats_t () : p50_us (0.0), p95_us (0.0), p99_us (0.0) {}
};

inline summary_stats_t make_summary_stats (const std::vector<double> &samples)
{
    summary_stats_t out;
    if (samples.empty ())
        return out;

    out.p50_us = percentile_us (samples, 0.50);
    out.p95_us = percentile_us (samples, 0.95);
    out.p99_us = percentile_us (samples, 0.99);
    return out;
}

class arg_reader_t
{
  public:
    arg_reader_t (int argc_, char **argv_) : argc (argc_), argv (argv_) {}

    bool has (const char *key) const
    {
        if (!key)
            return false;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp (argv[i], key) == 0)
                return true;
        }
        return false;
    }

    std::string get_string (const char *key, const char *fallback) const
    {
        if (!key)
            return fallback ? std::string (fallback) : std::string ();

        for (int i = 1; i + 1 < argc; ++i) {
            if (std::strcmp (argv[i], key) == 0)
                return std::string (argv[i + 1]);
        }
        return fallback ? std::string (fallback) : std::string ();
    }

    int get_int (const char *key, int fallback, int min_value) const
    {
        const std::string v = get_string (key, "");
        if (v.empty ())
            return fallback;

        char *end = NULL;
        errno = 0;
        const long parsed = std::strtol (v.c_str (), &end, 10);
        if (errno != 0 || end == v.c_str ())
            return fallback;
        if (parsed < static_cast<long> (min_value))
            return min_value;
        if (parsed > static_cast<long> (INT_MAX))
            return INT_MAX;
        return static_cast<int> (parsed);
    }

    size_t get_size (const char *key, size_t fallback, size_t min_value) const
    {
        const std::string v = get_string (key, "");
        if (v.empty ())
            return fallback;

        char *end = NULL;
        errno = 0;
        const unsigned long parsed = std::strtoul (v.c_str (), &end, 10);
        if (errno != 0 || end == v.c_str ())
            return fallback;
        if (parsed < static_cast<unsigned long> (min_value))
            return min_value;
        return static_cast<size_t> (parsed);
    }

  private:
    int argc;
    char **argv;
};

inline std::string make_tcp_endpoint (const std::string &host, int port)
{
    char buf[256];
    std::snprintf (buf, sizeof (buf), "%s:%d", host.c_str (), port);
    return std::string (buf);
}

inline std::string make_result_line (
  const std::string &stack,
  const std::string &scenario_id,
  size_t size,
  int ccu,
  int inflight,
  int duration,
  double throughput_msg_s,
  double throughput_mib,
  const summary_stats_t &stats,
  long connect_success,
  long connect_fail,
  double connect_ms,
  long parse_error,
  long timeout_error,
  double incomplete_ratio,
  bool pass)
{
    char buf[1024];
    std::snprintf (
      buf, sizeof (buf),
      "RESULT stack=%s scenario_id=%s mode=echo size=%zu ccu=%d inflight=%d "
      "duration=%d throughput_msg_s=%.2f throughput_mib_s=%.2f p50_us=%.2f "
      "p95_us=%.2f p99_us=%.2f connect_success=%ld connect_fail=%ld "
      "connect_ms=%.2f parse_error=%ld timeout_error=%ld "
      "incomplete_ratio=%.6f pass_fail=%s",
      stack.c_str (), scenario_id.c_str (), size, ccu, inflight, duration,
      throughput_msg_s, throughput_mib, stats.p50_us, stats.p95_us, stats.p99_us,
      connect_success, connect_fail, connect_ms, parse_error, timeout_error,
      incomplete_ratio, pass ? "PASS" : "FAIL");
    return std::string (buf);
}

inline std::string make_metric_line (const std::string &stack,
                                     size_t size,
                                     long recv_msgs,
                                     long parse_error,
                                     long protocol_error,
                                     long send_error,
                                     long connections)
{
    char buf[768];
    std::snprintf (
      buf, sizeof (buf),
      "METRIC stack=%s mode=echo size=%zu recv_msgs=%ld parse_error=%ld "
      "protocol_error=%ld send_error=%ld connections=%ld",
      stack.c_str (), size, recv_msgs, parse_error, protocol_error, send_error,
      connections);
    return std::string (buf);
}

} // namespace stream_echo

#endif
