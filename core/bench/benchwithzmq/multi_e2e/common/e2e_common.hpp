#ifndef BENCH_MULTI_E2E_COMMON_HPP
#define BENCH_MULTI_E2E_COMMON_HPP

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace bench_multi_e2e {

enum pattern_t {
    pattern_unknown = 0,
    pattern_dealer_dealer,
    pattern_dealer_router,
    pattern_router_router,
    pattern_pubsub,
    pattern_stream,
};

inline std::string upper_string(const std::string &s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        if (c >= 'a' && c <= 'z')
            return static_cast<char>(c - 'a' + 'A');
        return static_cast<char>(c);
    });
    return out;
}

inline pattern_t parse_pattern(const std::string &raw)
{
    std::string p = upper_string(raw);
    if (p == "MULTI_DEALER_DEALER" || p == "DEALER_DEALER")
        return pattern_dealer_dealer;
    if (p == "MULTI_DEALER_ROUTER" || p == "DEALER_ROUTER")
        return pattern_dealer_router;
    if (p == "MULTI_ROUTER_ROUTER" || p == "ROUTER_ROUTER")
        return pattern_router_router;
    if (p == "MULTI_PUBSUB" || p == "PUBSUB")
        return pattern_pubsub;
    if (p == "MULTI_STREAM" || p == "STREAM")
        return pattern_stream;
    return pattern_unknown;
}

inline const char *pattern_name(pattern_t p)
{
    switch (p) {
    case pattern_dealer_dealer:
        return "MULTI_DEALER_DEALER";
    case pattern_dealer_router:
        return "MULTI_DEALER_ROUTER";
    case pattern_router_router:
        return "MULTI_ROUTER_ROUTER";
    case pattern_pubsub:
        return "MULTI_PUBSUB";
    case pattern_stream:
        return "MULTI_STREAM";
    default:
        return "UNKNOWN";
    }
}

inline long parse_long_env(const char *name, long fallback, long min_value)
{
    const char *raw = std::getenv(name);
    if (!raw || !*raw)
        return fallback;

    char *end = NULL;
    errno = 0;
    const long value = std::strtol(raw, &end, 10);
    if (errno != 0 || end == raw)
        return fallback;
    if (value < min_value)
        return min_value;
    return value;
}

inline std::string parse_string_env(const char *name, const char *fallback)
{
    const char *raw = std::getenv(name);
    if (!raw || !*raw)
        return std::string(fallback ? fallback : "");
    return std::string(raw);
}

inline uint64_t now_ns()
{
    return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count());
}

inline void store_u64_be(unsigned char *dst, uint64_t value)
{
    dst[0] = static_cast<unsigned char>((value >> 56) & 0xFFu);
    dst[1] = static_cast<unsigned char>((value >> 48) & 0xFFu);
    dst[2] = static_cast<unsigned char>((value >> 40) & 0xFFu);
    dst[3] = static_cast<unsigned char>((value >> 32) & 0xFFu);
    dst[4] = static_cast<unsigned char>((value >> 24) & 0xFFu);
    dst[5] = static_cast<unsigned char>((value >> 16) & 0xFFu);
    dst[6] = static_cast<unsigned char>((value >> 8) & 0xFFu);
    dst[7] = static_cast<unsigned char>(value & 0xFFu);
}

inline uint64_t load_u64_be(const unsigned char *src)
{
    return (static_cast<uint64_t>(src[0]) << 56)
           | (static_cast<uint64_t>(src[1]) << 48)
           | (static_cast<uint64_t>(src[2]) << 40)
           | (static_cast<uint64_t>(src[3]) << 32)
           | (static_cast<uint64_t>(src[4]) << 24)
           | (static_cast<uint64_t>(src[5]) << 16)
           | (static_cast<uint64_t>(src[6]) << 8)
           | static_cast<uint64_t>(src[7]);
}

inline void store_u32_be(unsigned char *dst, uint32_t value)
{
    dst[0] = static_cast<unsigned char>((value >> 24) & 0xFFu);
    dst[1] = static_cast<unsigned char>((value >> 16) & 0xFFu);
    dst[2] = static_cast<unsigned char>((value >> 8) & 0xFFu);
    dst[3] = static_cast<unsigned char>(value & 0xFFu);
}

inline uint32_t load_u32_be(const unsigned char *src)
{
    return (static_cast<uint32_t>(src[0]) << 24)
           | (static_cast<uint32_t>(src[1]) << 16)
           | (static_cast<uint32_t>(src[2]) << 8)
           | static_cast<uint32_t>(src[3]);
}

inline std::string endpoint_from_port(const std::string &transport, int port)
{
    char buf[128];
    if (transport == "tcp") {
        std::snprintf(buf, sizeof(buf), "tcp://127.0.0.1:%d", port);
        return std::string(buf);
    }
    std::snprintf(buf, sizeof(buf), "%s://127.0.0.1:%d", transport.c_str(), port);
    return std::string(buf);
}

inline void print_result(const std::string &lib_name,
                         pattern_t pattern,
                         const std::string &transport,
                         size_t msg_size,
                         double throughput,
                         double latency_us)
{
    std::printf("RESULT,%s,%s,%s,%zu,throughput,%.2f\n", lib_name.c_str(),
                pattern_name(pattern), transport.c_str(), msg_size, throughput);
    std::printf("RESULT,%s,%s,%s,%zu,latency,%.2f\n", lib_name.c_str(),
                pattern_name(pattern), transport.c_str(), msg_size, latency_us);
    std::fflush(stdout);
}

inline bool is_rtt_pattern(pattern_t pattern)
{
    return pattern == pattern_dealer_dealer || pattern == pattern_dealer_router
           || pattern == pattern_router_router || pattern == pattern_stream;
}

inline bool is_oneway_pattern(pattern_t pattern)
{
    return pattern == pattern_pubsub;
}

} // namespace bench_multi_e2e

#endif
