#pragma once

#include "../common/bench_common.hpp"
#include <algorithm>
#include <string>

namespace stream_single_common {

struct stream_metrics_t {
    int sent;
    int received;
    double elapsed_ms;

    stream_metrics_t() : sent(0), received(0), elapsed_ms(0.0) {}
    stream_metrics_t(int sent_, int received_, double elapsed_ms_) :
        sent(sent_),
        received(received_),
        elapsed_ms(elapsed_ms_)
    {
    }
};

inline int effective_message_count(const stream_metrics_t &metrics)
{
    return std::max(0, std::min(metrics.sent, metrics.received));
}

inline double compute_throughput(const stream_metrics_t &metrics)
{
    const int effective = effective_message_count(metrics);
    if (effective <= 0 || metrics.elapsed_ms <= 0.0)
        return 0.0;
    return static_cast<double>(effective) / (metrics.elapsed_ms / 1000.0);
}

inline void print_stream_metrics(const std::string &lib_name,
                                 const std::string &pattern,
                                 const std::string &transport,
                                 size_t msg_size,
                                 const stream_metrics_t &metrics,
                                 double latency_us)
{
    print_result(lib_name, pattern, transport, msg_size,
                 compute_throughput(metrics), latency_us);
}

} // namespace stream_single_common
