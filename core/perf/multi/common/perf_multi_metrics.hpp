#ifndef PERF_MULTI_METRICS_HPP
#define PERF_MULTI_METRICS_HPP

#include "perf_common_multi.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

inline size_t resolve_latency_sample_cap()
{
    const int cap = resolve_multi_int_env_with_fallback(
      "PERF_MULTI_LATENCY_SAMPLE_CAP",
      "PERF_LATENCY_SAMPLE_CAP",
      200000,
      1);
    return cap > 0 ? static_cast<size_t>(cap) : static_cast<size_t>(200000);
}

struct bench_latency_stats_t {
    bench_latency_stats_t() : mean_us(0.0), p95_us(0.0), p99_us(0.0) {}
    double mean_us;
    double p95_us;
    double p99_us;
};

struct server_queue_stats_t {
    server_queue_stats_t() :
        snd_pending_max(0.0),
        rcv_pending_max(0.0),
        rcv_pending_end(0.0)
    {}

    double snd_pending_max;
    double rcv_pending_max;
    double rcv_pending_end;
};

class bench_latency_sampler_t {
public:
    explicit bench_latency_sampler_t(
      size_t sample_cap_ = resolve_latency_sample_cap()) :
      _sample_cap(sample_cap_ > 0 ? sample_cap_ : 1),
      _count(0),
      _sum_us(0.0),
      _rng_state(0x9e3779b97f4a7c15ULL)
    {
        _samples.reserve(_sample_cap);
    }

    void add(double latency_us_)
    {
        const double sample = latency_us_ >= 0.0 ? latency_us_ : 0.0;
        ++_count;
        _sum_us += sample;

        if (_samples.size() < _sample_cap) {
            _samples.push_back(sample);
            return;
        }

        const unsigned long long slot = next_rand_u64() % _count;
        if (slot < static_cast<unsigned long long>(_sample_cap))
            _samples[static_cast<size_t>(slot)] = sample;
    }

    void reset()
    {
        _count = 0;
        _sum_us = 0.0;
        _rng_state = 0x9e3779b97f4a7c15ULL;
        _samples.clear();
    }

    void merge_from(const bench_latency_sampler_t &other_)
    {
        if (other_._count == 0)
            return;

        _count += other_._count;
        _sum_us += other_._sum_us;
        for (size_t i = 0; i < other_._samples.size(); ++i) {
            if (_samples.size() < _sample_cap) {
                _samples.push_back(other_._samples[i]);
                continue;
            }

            const unsigned long long slot = next_rand_u64() % _count;
            if (slot < static_cast<unsigned long long>(_sample_cap))
                _samples[static_cast<size_t>(slot)] = other_._samples[i];
        }
    }

    unsigned long long count() const { return _count; }
    double sum_us() const { return _sum_us; }

    void append_samples(std::vector<double> *out_) const
    {
        if (!out_ || _samples.empty())
            return;
        out_->insert(out_->end(), _samples.begin(), _samples.end());
    }

    bench_latency_stats_t snapshot()
    {
        bench_latency_stats_t out;
        if (_count == 0)
            return out;

        out.mean_us = _sum_us / static_cast<double>(_count);
        if (_samples.empty()) {
            out.p95_us = out.mean_us;
            out.p99_us = out.mean_us;
            return out;
        }

        std::sort(_samples.begin(), _samples.end());
        out.p95_us = percentile_from_sorted(_samples, 0.95);
        out.p99_us = percentile_from_sorted(_samples, 0.99);
        return out;
    }

private:
    static double percentile_from_sorted(const std::vector<double> &sorted_,
                                         double q_)
    {
        if (sorted_.empty())
            return 0.0;
        if (q_ <= 0.0)
            return sorted_.front();
        if (q_ >= 1.0)
            return sorted_.back();

        const double pos = (sorted_.size() - 1) * q_;
        const size_t lo = static_cast<size_t>(pos);
        const size_t hi = lo + 1 < sorted_.size() ? lo + 1 : lo;
        const double frac = pos - static_cast<double>(lo);
        return sorted_[lo] + (sorted_[hi] - sorted_[lo]) * frac;
    }

    unsigned long long next_rand_u64()
    {
        if (_rng_state == 0)
            _rng_state = 0x9e3779b97f4a7c15ULL;
        unsigned long long x = _rng_state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        _rng_state = x;
        return x;
    }

    size_t _sample_cap;
    unsigned long long _count;
    double _sum_us;
    unsigned long long _rng_state;
    std::vector<double> _samples;
};

inline void print_result(const std::string &lib_type,
                         const std::string &pattern,
                         const std::string &transport,
                         size_t size,
                         double throughput,
                         double latency,
                         double latency_p95,
                         double latency_p99)
{
    const bool is_echo_pattern =
      pattern == "DEALER_ROUTER"
      || pattern == "ROUTER_ROUTER"
      || pattern == "STREAM"
      || pattern == "MULTI_DEALER_ROUTER"
      || pattern == "MULTI_ROUTER_ROUTER"
      || pattern == "MULTI_STREAM";
    const double direction_factor = is_echo_pattern ? 2.0 : 1.0;
    const double bandwidth_mb_s =
      (throughput * static_cast<double>(size) * direction_factor) / 1000000.0;
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",throughput,"
              << std::fixed << std::setprecision(3) << throughput
              << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",bandwidth,"
              << std::fixed << std::setprecision(3) << bandwidth_mb_s
              << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",latency,"
              << std::fixed << std::setprecision(3) << latency
              << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",latency_p95,"
              << std::fixed << std::setprecision(3) << latency_p95
              << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",latency_p99,"
              << std::fixed << std::setprecision(3) << latency_p99
              << std::endl;
}

inline void print_server_queue_metrics(const std::string &lib_type,
                                       const std::string &pattern,
                                       const std::string &transport,
                                       size_t size,
                                       const server_queue_stats_t &queue_stats)
{
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",server_snd_pending_max,"
              << std::fixed << std::setprecision(3) << queue_stats.snd_pending_max
              << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",server_rcv_pending_max,"
              << std::fixed << std::setprecision(3) << queue_stats.rcv_pending_max
              << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",server_rcv_pending_end,"
              << std::fixed << std::setprecision(3) << queue_stats.rcv_pending_end
              << std::endl;
}

inline void emit_result_metric_line(const std::string &lib_type,
                                    const std::string &pattern,
                                    const std::string &transport,
                                    size_t size,
                                    const char *metric_name,
                                    double value,
                                    int precision)
{
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << "," << metric_name << ","
              << std::fixed << std::setprecision(precision) << value
              << std::endl;
}

template<typename MetricsT>
inline void print_server_resource_result_lines(
  const std::string &lib_type,
  const std::string &pattern,
  const std::string &transport,
  size_t size,
  const MetricsT &metrics)
{
    if (metrics.has_cpu_pct) {
        emit_result_metric_line(
          lib_type, pattern, transport, size, "server_cpu_pct",
          metrics.cpu_pct, 2);
    }
    if (metrics.has_mem_mb) {
        emit_result_metric_line(
          lib_type, pattern, transport, size, "server_mem_mb",
          metrics.mem_mb, 2);
    }
}

template<typename MetricsT>
inline void print_server_metrics_for_sizes(
  const std::string &lib_type,
  const std::string &pattern,
  const std::string &transport,
  const std::vector<size_t> &sizes,
  const MetricsT &metrics,
  const server_queue_stats_t *queue_stats = NULL)
{
    for (size_t i = 0; i < sizes.size(); ++i) {
        print_server_resource_result_lines(
          lib_type, pattern, transport, sizes[i], metrics);
        if (queue_stats) {
            print_server_queue_metrics(
              lib_type, pattern, transport, sizes[i], *queue_stats);
        }
    }
}

template<typename MetricsT>
inline void print_client_resource_result_lines_common(
  const std::string &lib_type,
  const std::string &pattern,
  const std::string &transport,
  size_t size,
  const MetricsT &metrics)
{
    if (metrics.has_cpu_pct) {
        emit_result_metric_line(
          lib_type, pattern, transport, size, "client_cpu_pct",
          metrics.cpu_pct, 2);
    }
    if (metrics.has_mem_mb) {
        emit_result_metric_line(
          lib_type, pattern, transport, size, "client_mem_mb",
          metrics.mem_mb, 2);
    }
}

inline server_queue_stats_t sample_server_queue_stats(void *send_socket_,
                                                      void *recv_socket_)
{
    server_queue_stats_t out;
    (void) send_socket_;
    (void) recv_socket_;
    return out;
}

inline void print_result(const std::string &lib_type,
                         const std::string &pattern,
                         const std::string &transport,
                         size_t size,
                         double throughput,
                         double latency)
{
    print_result(
      lib_type, pattern, transport, size, throughput, latency, latency, latency);
}

#endif
