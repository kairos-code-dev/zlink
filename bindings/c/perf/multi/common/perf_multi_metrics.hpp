#ifndef PERF_MULTI_METRICS_HPP
#define PERF_MULTI_METRICS_HPP

#include "perf_common_multi.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

struct bench_latency_stats_t {
    bench_latency_stats_t() : mean_ns(0.0), p95_ns(0.0), p99_ns(0.0) {}
    union {
        double mean_ns;
        double mean_us;
    };
    union {
        double p95_ns;
        double p95_us;
    };
    union {
        double p99_ns;
        double p99_us;
    };
};

class bench_latency_sampler_t {
public:
    bench_latency_sampler_t() :
      _count(0),
      _sum_ns(0.0)
    {
    }

    void add(double latency_ns_)
    {
        const double sample = latency_ns_ >= 0.0 ? latency_ns_ : 0.0;
        ++_count;
        _sum_ns += sample;
        _samples.push_back(sample);
    }

    void reset()
    {
        _count = 0;
        _sum_ns = 0.0;
        _samples.clear();
    }

    void merge_from(const bench_latency_sampler_t &other_)
    {
        if (other_._count == 0)
            return;

        _count += other_._count;
        _sum_ns += other_._sum_ns;
        _samples.insert(
          _samples.end(), other_._samples.begin(), other_._samples.end());
    }

    unsigned long long count() const { return _count; }
    double sum_ns() const { return _sum_ns; }

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

        out.mean_ns = _sum_ns / static_cast<double>(_count);
        if (_samples.empty()) {
            out.p95_ns = out.mean_ns;
            out.p99_ns = out.mean_ns;
            return out;
        }

        std::sort(_samples.begin(), _samples.end());
        out.p95_ns = percentile_from_sorted(_samples, 0.95);
        out.p99_ns = percentile_from_sorted(_samples, 0.99);
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

    unsigned long long _count;
    double _sum_ns;
    std::vector<double> _samples;
};

inline void print_result(const std::string &lib_type,
                         const std::string &pattern,
                         const std::string &transport,
                         size_t size,
                         double throughput,
                         double latency_ns,
                         double latency_p95_ns,
                         double latency_p99_ns)
{
    const bool is_echo_pattern =
      pattern == "DEALER_ROUTER"
      || pattern == "ROUTER_ROUTER"
      || pattern == "STREAM"
      || pattern == "MULTI_SPOT_REQREP"
      || pattern == "MULTI_SPOT_SENDSEND"
      || pattern == "MULTI_DEALER_ROUTER"
      || pattern == "MULTI_ROUTER_ROUTER"
      || pattern == "MULTI_STREAM";
    const double direction_factor = is_echo_pattern ? 2.0 : 1.0;
    const double bandwidth_mb_s =
      (throughput * static_cast<double>(size) * direction_factor) / 1000000.0;
    const double latency_ms = latency_ns / 1000000.0;
    const double latency_p95_ms = latency_p95_ns / 1000000.0;
    const double latency_p99_ms = latency_p99_ns / 1000000.0;
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
              << std::fixed << std::setprecision(3) << latency_ms
              << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",latency_p95,"
              << std::fixed << std::setprecision(3) << latency_p95_ms
              << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",latency_p99,"
              << std::fixed << std::setprecision(3) << latency_p99_ms
              << std::endl;
}

template<typename MetricsT>
inline void print_server_metrics_for_sizes(
  const std::string &lib_type,
  const std::string &pattern,
  const std::string &transport,
  const std::vector<size_t> &sizes,
  const MetricsT &metrics)
{
    (void) lib_type;
    (void) pattern;
    (void) transport;
    (void) sizes;
    (void) metrics;
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
