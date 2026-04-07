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

struct client_queue_stats_t {
    client_queue_stats_t() :
        rcv_pending_max(0.0),
        rcv_pending_end(0.0),
        has_rcv_pending(false)
    {}

    double rcv_pending_max;
    double rcv_pending_end;
    bool has_rcv_pending;
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

inline int resolve_multi_queue_sample_ms();
inline void configure_multi_queue_monitor(void *monitor_);
inline void *open_multi_queue_probe_monitor(void *target_);
inline bool multi_read_monitor_pending_msgs(void *monitor_,
                                            bool send_,
                                            unsigned long long *pending_out_);

class multi_client_queue_probe_t {
  public:
    explicit multi_client_queue_probe_t(const std::vector<void *> &recv_sockets_) :
        _sample_ms(resolve_multi_queue_sample_ms()),
        _running(false),
        _rcv_pending_max(0),
        _rcv_pending_end(0),
        _rcv_seen(false)
    {
        _recv_monitors.reserve(recv_sockets_.size());
        for (size_t i = 0; i < recv_sockets_.size(); ++i) {
            void *monitor = open_multi_queue_probe_monitor(recv_sockets_[i]);
            if (monitor)
                _recv_monitors.push_back(monitor);
        }
    }

    ~multi_client_queue_probe_t()
    {
        stop();
        for (size_t i = 0; i < _recv_monitors.size(); ++i) {
            if (_recv_monitors[i])
                zlink_monitor_close(&_recv_monitors[i]);
        }
    }

    void start()
    {
        stop();
        reset();
        if (_recv_monitors.empty())
            return;
        _running.store(true, std::memory_order_release);
        _thread = std::thread(&multi_client_queue_probe_t::run, this);
    }

    void stop()
    {
        _running.store(false, std::memory_order_release);
        if (_thread.joinable())
            _thread.join();
    }

    client_queue_stats_t snapshot()
    {
        sample_once();
        client_queue_stats_t out;
        out.has_rcv_pending = true;
        if (_rcv_seen.load(std::memory_order_acquire)) {
            out.rcv_pending_max = static_cast<double>(
              _rcv_pending_max.load(std::memory_order_acquire));
            out.rcv_pending_end = static_cast<double>(
              _rcv_pending_end.load(std::memory_order_acquire));
        }
        return out;
    }

    void observe_rcv_pending(unsigned long long pending)
    {
        unsigned long long current_max =
          _rcv_pending_max.load(std::memory_order_acquire);
        while (pending > current_max
               && !_rcv_pending_max.compare_exchange_weak(
                 current_max, pending, std::memory_order_acq_rel)) {
        }
        _rcv_pending_end.store(pending, std::memory_order_release);
        _rcv_seen.store(true, std::memory_order_release);
    }

  private:
    void reset()
    {
        _rcv_pending_max.store(0, std::memory_order_release);
        _rcv_pending_end.store(0, std::memory_order_release);
        _rcv_seen.store(false, std::memory_order_release);
    }

    void run()
    {
        while (_running.load(std::memory_order_acquire)) {
            sample_once();
            std::this_thread::sleep_for(
              std::chrono::milliseconds(_sample_ms > 0 ? _sample_ms : 10));
        }
    }

    void sample_once()
    {
        for (size_t i = 0; i < _recv_monitors.size(); ++i) {
            unsigned long long pending = 0;
            if (!multi_read_monitor_pending_msgs(_recv_monitors[i], false,
                                                 &pending)) {
                continue;
            }

            unsigned long long current_max =
              _rcv_pending_max.load(std::memory_order_acquire);
            while (pending > current_max
                   && !_rcv_pending_max.compare_exchange_weak(
                     current_max, pending, std::memory_order_acq_rel)) {
            }
            _rcv_pending_end.store(pending, std::memory_order_release);
            _rcv_seen.store(true, std::memory_order_release);
        }
    }

    int _sample_ms;
    std::atomic<bool> _running;
    std::thread _thread;
    std::vector<void *> _recv_monitors;
    std::atomic<unsigned long long> _rcv_pending_max;
    std::atomic<unsigned long long> _rcv_pending_end;
    std::atomic<bool> _rcv_seen;

    multi_client_queue_probe_t(const multi_client_queue_probe_t &);
    multi_client_queue_probe_t &operator=(const multi_client_queue_probe_t &);
};

template<typename MetricsT>
inline void print_server_resource_result_lines(
  const std::string &lib_type,
  const std::string &pattern,
  const std::string &transport,
  size_t size,
  const MetricsT &metrics)
{
    (void) lib_type;
    (void) pattern;
    (void) transport;
    (void) size;
    (void) metrics;
}

template<typename MetricsT>
inline void print_server_metrics_for_sizes(
  const std::string &lib_type,
  const std::string &pattern,
  const std::string &transport,
  const std::vector<size_t> &sizes,
  const MetricsT &metrics)
{
    for (size_t i = 0; i < sizes.size(); ++i) {
        print_server_resource_result_lines(
          lib_type, pattern, transport, sizes[i], metrics);
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
    (void) lib_type;
    (void) pattern;
    (void) transport;
    (void) size;
    (void) metrics;
}

inline server_queue_stats_t sample_server_queue_stats(void *send_socket_,
                                                      void *recv_socket_)
{
    server_queue_stats_t out;
    (void) send_socket_;
    (void) recv_socket_;
    return out;
}

inline int resolve_multi_queue_sample_ms()
{
    return resolve_multi_int_env_with_fallback(
      "PERF_MULTI_QUEUE_SAMPLE_MS", "PERF_QUEUE_SAMPLE_MS", 10, 1);
}

inline void configure_multi_queue_monitor(void *monitor_)
{
    if (!monitor_)
        return;

    const int monitor_hwm = resolve_multi_int_env_with_fallback(
      "PERF_MONITOR_HWM", "PERF_MONITOR_HWM", 1000, 1);
    set_sockopt_int(monitor_, ZLINK_OPT_LINGER, 0, "ZLINK_OPT_LINGER");
    if (monitor_hwm > 0) {
        set_sockopt_int(
          monitor_, ZLINK_OPT_SNDHWM, monitor_hwm, "ZLINK_OPT_SNDHWM");
        set_sockopt_int(
          monitor_, ZLINK_OPT_RCVHWM, monitor_hwm, "ZLINK_OPT_RCVHWM");
    }
}

inline void *open_multi_queue_probe_monitor(void *target_)
{
    if (!target_)
        return NULL;

    zlink_service_monitor_open_options_t service_opts;
    std::memset(&service_opts, 0, sizeof(service_opts));
    service_opts.events = ZLINK_SERVICE_MONITOR_EVENT_CLOSED;
    void *monitor = zlink_service_monitor_open(target_, &service_opts);
    if (monitor) {
        configure_multi_queue_monitor(monitor);
        return monitor;
    }

    zlink_socket_monitor_open_options_t socket_opts;
    std::memset(&socket_opts, 0, sizeof(socket_opts));
    socket_opts.events = ZLINK_EVENT_CLOSED;
    monitor = zlink_socket_monitor_open(target_, &socket_opts);
    if (monitor) {
        configure_multi_queue_monitor(monitor);
        return monitor;
    }
    return monitor;
}

inline bool multi_read_monitor_pending_msgs(void *monitor_,
                                            bool send_,
                                            unsigned long long *pending_out_)
{
    if (!monitor_ || !pending_out_)
        return false;

    zlink_monitor_snapshot_t snapshot;
    std::memset(&snapshot, 0, sizeof(snapshot));
    if (zlink_monitor_snapshot(monitor_, &snapshot) != 0)
        return false;

    const zlink_monitor_snapshot_detail_mask_t detail =
      send_ ? ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS
            : ZLINK_MONITOR_SNAPSHOT_DETAIL_RCV_PENDING_MSGS;
    if ((snapshot.detail_flags & detail) == 0)
        return false;

    *pending_out_ = static_cast<unsigned long long>(
      send_ ? snapshot.snd_pending_msgs : snapshot.rcv_pending_msgs);
    return true;
}

class multi_queue_probe_t {
  public:
    multi_queue_probe_t(void *send_socket_, void *recv_socket_) :
        _send_monitor(open_multi_queue_probe_monitor(send_socket_)),
        _recv_monitor(open_multi_queue_probe_monitor(recv_socket_)),
        _sample_ms(resolve_multi_queue_sample_ms()),
        _running(false),
        _snd_pending_max(0),
        _rcv_pending_max(0),
        _rcv_pending_end(0),
        _snd_seen(false),
        _rcv_seen(false)
    {
    }

    ~multi_queue_probe_t()
    {
        stop();
        if (_send_monitor)
            zlink_monitor_close(&_send_monitor);
        if (_recv_monitor)
            zlink_monitor_close(&_recv_monitor);
    }

    void start()
    {
        stop();
        reset();
        _running.store(true, std::memory_order_release);
        _thread = std::thread(&multi_queue_probe_t::run, this);
    }

    void stop()
    {
        _running.store(false, std::memory_order_release);
        if (_thread.joinable())
            _thread.join();
    }

    server_queue_stats_t snapshot()
    {
        server_queue_stats_t out;
        if (_snd_seen.load(std::memory_order_acquire)) {
            out.snd_pending_max = static_cast<double>(
              _snd_pending_max.load(std::memory_order_acquire));
        }
        if (_rcv_seen.load(std::memory_order_acquire)) {
            out.rcv_pending_max = static_cast<double>(
              _rcv_pending_max.load(std::memory_order_acquire));
            out.rcv_pending_end = static_cast<double>(
              _rcv_pending_end.load(std::memory_order_acquire));
        }
        return out;
    }

  private:
    void reset()
    {
        _snd_pending_max.store(0, std::memory_order_release);
        _rcv_pending_max.store(0, std::memory_order_release);
        _rcv_pending_end.store(0, std::memory_order_release);
        _snd_seen.store(false, std::memory_order_release);
        _rcv_seen.store(false, std::memory_order_release);
    }

    void run()
    {
        while (_running.load(std::memory_order_acquire)) {
            sample_once();
            std::this_thread::sleep_for(
              std::chrono::milliseconds(_sample_ms > 0 ? _sample_ms : 10));
        }
    }

    void sample_once()
    {
        unsigned long long pending = 0;
        if (multi_read_monitor_pending_msgs(_send_monitor, true, &pending)) {
            unsigned long long current_max =
              _snd_pending_max.load(std::memory_order_acquire);
            while (pending > current_max
                   && !_snd_pending_max.compare_exchange_weak(
                     current_max, pending, std::memory_order_acq_rel)) {
            }
            _snd_seen.store(true, std::memory_order_release);
        }

        if (multi_read_monitor_pending_msgs(_recv_monitor, false, &pending)) {
            unsigned long long current_max =
              _rcv_pending_max.load(std::memory_order_acquire);
            while (pending > current_max
                   && !_rcv_pending_max.compare_exchange_weak(
                     current_max, pending, std::memory_order_acq_rel)) {
            }
            _rcv_pending_end.store(pending, std::memory_order_release);
            _rcv_seen.store(true, std::memory_order_release);
        }
    }

    void *_send_monitor;
    void *_recv_monitor;
    int _sample_ms;
    std::atomic<bool> _running;
    std::thread _thread;
    std::atomic<unsigned long long> _snd_pending_max;
    std::atomic<unsigned long long> _rcv_pending_max;
    std::atomic<unsigned long long> _rcv_pending_end;
    std::atomic<bool> _snd_seen;
    std::atomic<bool> _rcv_seen;

    multi_queue_probe_t(const multi_queue_probe_t &);
    multi_queue_probe_t &operator=(const multi_queue_probe_t &);
};

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
