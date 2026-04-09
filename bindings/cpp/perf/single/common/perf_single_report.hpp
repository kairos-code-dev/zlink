#ifndef PERF_SINGLE_REPORT_HPP
#define PERF_SINGLE_REPORT_HPP

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>

struct queue_stats_t;
class queue_probe_t;

queue_stats_t sample_queue_stats (queue_probe_t *queue_probe_);

void print_result (const std::string &lib_type,
                   const std::string &pattern,
                   const std::string &transport,
                   size_t size,
                   double throughput,
                   double latency,
                   double latency_p95,
                   double latency_p99);

void print_queue_metrics (const std::string &lib_type,
                          const std::string &pattern,
                          const std::string &transport,
                          size_t size,
                          const queue_stats_t &queue_stats);

void print_result (const std::string &lib_type,
                   const std::string &pattern,
                   const std::string &transport,
                   size_t size,
                   double throughput,
                   double latency,
                   double latency_p95,
                   double latency_p99,
                   const queue_stats_t &queue_stats);

void print_fail_result (const std::string &lib_type,
                        const std::string &pattern,
                        const std::string &transport,
                        size_t size);

void print_fail_result (const std::string &lib_type,
                        const std::string &pattern,
                        const std::string &transport,
                        size_t size,
                        queue_probe_t *queue_probe_);

inline queue_stats_t sample_queue_stats (queue_probe_t *queue_probe_)
{
    if (!queue_probe_)
        return queue_stats_t ();
    queue_probe_->force_sample_send ();
    queue_probe_->force_sample_recv ();
    return queue_probe_->snapshot ();
}

inline void print_result (const std::string &lib_type,
                          const std::string &pattern,
                          const std::string &transport,
                          size_t size,
                          double throughput,
                          double latency,
                          double latency_p95,
                          double latency_p99)
{
    const double latency_ms = latency / 1000000.0;
    const double latency_p95_ms = latency_p95 / 1000000.0;
    const double latency_p99_ms = latency_p99 / 1000000.0;
    const double bandwidth_mb_s =
      (throughput * static_cast<double> (size)) / 1000000.0;

    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",throughput," << std::fixed
              << std::setprecision (2) << throughput << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",bandwidth," << std::fixed
              << std::setprecision (2) << bandwidth_mb_s << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",latency," << std::fixed
              << std::setprecision (3) << latency_ms << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",latency_p95," << std::fixed
              << std::setprecision (3) << latency_p95_ms << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",latency_p99," << std::fixed
              << std::setprecision (3) << latency_p99_ms << std::endl;
}

inline void print_queue_metrics (const std::string &lib_type,
                                 const std::string &pattern,
                                 const std::string &transport,
                                 size_t size,
                                 const queue_stats_t &queue_stats)
{
    if (queue_stats.has_snd_pending) {
        std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
                  << "," << size << ",snd_pending_max," << std::fixed
                  << std::setprecision (2) << queue_stats.snd_pending_max
                  << std::endl;
    }

    if (queue_stats.has_rcv_pending) {
        std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
                  << "," << size << ",rcv_pending_max," << std::fixed
                  << std::setprecision (2) << queue_stats.rcv_pending_max
                  << std::endl;
        std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
                  << "," << size << ",rcv_pending_end," << std::fixed
                  << std::setprecision (2) << queue_stats.rcv_pending_end
                  << std::endl;
    }
}

inline void print_result (const std::string &lib_type,
                          const std::string &pattern,
                          const std::string &transport,
                          size_t size,
                          double throughput,
                          double latency,
                          double latency_p95,
                          double latency_p99,
                          const queue_stats_t &queue_stats)
{
    (void) queue_stats;
    print_result (lib_type,
                  pattern,
                  transport,
                  size,
                  throughput,
                  latency,
                  latency_p95,
                  latency_p99);
}

inline void print_fail_result (const std::string &lib_type,
                               const std::string &pattern,
                               const std::string &transport,
                               size_t size)
{
    std::cout << "FAIL," << lib_type << "," << pattern << "," << transport
              << "," << size << std::endl;
}

inline void print_fail_result (const std::string &lib_type,
                               const std::string &pattern,
                               const std::string &transport,
                               size_t size,
                               queue_probe_t *queue_probe_)
{
    (void) queue_probe_;
    print_fail_result (lib_type, pattern, transport, size);
}

#endif
