#ifndef PERF_SINGLE_MONITOR_HPP
#define PERF_SINGLE_MONITOR_HPP

#include "../../common/perf_infra.hpp"

#include <chrono>
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <iostream>

#include <zlink.h>

inline void configure_perf_monitor_socket(void *monitor_)
{
    if (!monitor_)
        return;

    const int monitor_hwm = parse_positive_env("PERF_MONITOR_HWM", 1000);
    set_sockopt_int(monitor_, ZLINK_OPT_LINGER, 0, "ZLINK_OPT_LINGER");
    if (monitor_hwm > 0) {
        set_sockopt_int(monitor_, ZLINK_OPT_SNDHWM, monitor_hwm,
                        "ZLINK_OPT_SNDHWM");
        set_sockopt_int(monitor_, ZLINK_OPT_RCVHWM, monitor_hwm,
                        "ZLINK_OPT_RCVHWM");
    }
}

inline bool is_socket_monitor_error_event(uint64_t event_)
{
    switch (event_) {
        case ZLINK_EVENT_BIND_FAILED:
        case ZLINK_EVENT_ACCEPT_FAILED:
        case ZLINK_EVENT_CLOSE_FAILED:
        case ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL:
        case ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL:
        case ZLINK_EVENT_HANDSHAKE_FAILED_AUTH:
            return true;
        default:
            return false;
    }
}

inline bool socket_monitor_event_ready(
  const zlink_socket_monitor_event_t &event_, uint64_t success_event_)
{
    if (event_.event != success_event_)
        return false;
    if (success_event_ == ZLINK_EVENT_CONNECTION_READY)
        return true;
    return event_.value > 0;
}

inline void *open_configured_socket_monitor(void *socket_, uint64_t events_)
{
    if (!socket_ || events_ == 0)
        return NULL;

    zlink_socket_monitor_open_options_t monitor_opts;
    memset(&monitor_opts, 0, sizeof(monitor_opts));
    monitor_opts.events = events_;
    void *monitor = zlink_socket_monitor_open(socket_, &monitor_opts);
    if (!monitor)
        return NULL;
    configure_perf_monitor_socket(monitor);
    return monitor;
}

inline bool wait_for_socket_monitor_event(void *monitor_,
                                          uint64_t success_event_,
                                          int timeout_ms_)
{
    if (!monitor_ || success_event_ == 0)
        return false;

    bool ready = false;
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(timeout_ms_ > 0 ? timeout_ms_ : 1);

    while (std::chrono::steady_clock::now() < deadline && !ready) {
        zlink_pollitem_t item = {monitor_, 0, ZLINK_POLLIN, 0};
        const long timeout_ms = static_cast<long>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now())
            .count());
        const int poll_rc =
          zlink_poll(&item, 1, timeout_ms > 0 ? timeout_ms : 1);
        if (poll_rc < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (poll_rc == 0 || (item.revents & ZLINK_POLLIN) == 0)
            continue;

        for (;;) {
            zlink_socket_monitor_event_t event;
            if (zlink_socket_monitor_recv(monitor_, &event, ZLINK_DONTWAIT)
                != 0) {
                if (errno == EAGAIN || errno == EINTR)
                    break;
                return false;
            }
            if (socket_monitor_event_ready(event, success_event_)) {
                ready = true;
                break;
            }
            if (is_socket_monitor_error_event(event.event)) {
                errno = event.value > 0 ? static_cast<int>(event.value) : EIO;
                return false;
            }
        }
    }
    return ready;
}

inline void print_result(const std::string &lib_type,
                         const std::string &pattern,
                         const std::string &transport,
                         size_t size,
                         double throughput,
                         double latency_ns,
                         double latency_p95_ns,
                         double latency_p99_ns)
{
    const double bandwidth_mb_s =
      (throughput * static_cast<double>(size)) / 1000000.0;
    const double latency_ms = latency_ns / 1000000.0;
    const double latency_p95_ms = latency_p95_ns / 1000000.0;
    const double latency_p99_ms = latency_p99_ns / 1000000.0;
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",throughput," << std::fixed
              << std::setprecision(2) << throughput << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",bandwidth," << std::fixed
              << std::setprecision(2) << bandwidth_mb_s << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",latency," << std::fixed
              << std::setprecision(3) << latency_ms << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",latency_p95," << std::fixed
              << std::setprecision(3) << latency_p95_ms << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << ","
              << transport << "," << size << ",latency_p99," << std::fixed
              << std::setprecision(3) << latency_p99_ms << std::endl;
}

inline void print_result(const std::string &lib_type,
                         const std::string &pattern,
                         const std::string &transport,
                         size_t size,
                         double throughput,
                         double latency)
{
    print_result(lib_type, pattern, transport, size, throughput, latency,
                 latency, latency);
}

inline void print_failure_diagnostics(const std::string &lib_type,
                                      const std::string &pattern,
                                      const std::string &transport,
                                      size_t size,
                                      const char *detail_ = NULL)
{
    std::cerr << "FAIL," << lib_type << "," << pattern << "," << transport
              << "," << size;
    if (detail_ && *detail_)
        std::cerr << "," << detail_;
    std::cerr << std::endl;
}

inline void print_fail_result(const std::string &lib_type,
                              const std::string &pattern,
                              const std::string &transport,
                              size_t size)
{
    print_failure_diagnostics(lib_type, pattern, transport, size);
}

#endif
