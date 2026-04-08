#ifndef ZLINK_CPP_PERF_MONITOR_WAIT_HPP
#define ZLINK_CPP_PERF_MONITOR_WAIT_HPP

#include <zlink.hpp>

#include <cerrno>
#include <chrono>
#include <vector>

namespace perf {

enum class value_compare
{
    exact,
    min_threshold
};

template<typename MonitorT>
struct monitor_wait_traits;

template<>
struct monitor_wait_traits<zlink::monitor_handle_t>
{
    typedef zlink::monitor_event_t event_t;

    static zlink::maybe_t<event_t> try_recv (zlink::monitor_handle_t &monitor)
    {
        return monitor.try_recv ();
    }

    static uint64_t event_type (const event_t &event)
    {
        return static_cast<uint64_t> (event.event);
    }

    static int64_t value (const event_t &event)
    {
        return static_cast<int64_t> (event.value);
    }
};

template<>
struct monitor_wait_traits<zlink::service_monitor_handle_t>
{
    typedef zlink::service_event_t event_t;

    static zlink::maybe_t<event_t> try_recv (zlink::service_monitor_handle_t &monitor)
    {
        return monitor.try_recv ();
    }

    static uint64_t event_type (const event_t &event)
    {
        return static_cast<uint64_t> (event.event_type);
    }

    static int64_t value (const event_t &event)
    {
        return static_cast<int64_t> (event.value);
    }
};

inline bool monitor_value_matches (int64_t observed,
                                   int64_t expected,
                                   value_compare cmp)
{
    if (expected < 0)
        return true;
    if (cmp == value_compare::min_threshold)
        return observed >= expected;
    return observed == expected;
}

template<typename MonitorT>
inline bool wait_monitor_event (MonitorT &monitor,
                                uint64_t event_type,
                                int64_t value,
                                value_compare cmp,
                                int timeout_ms)
{
    typedef monitor_wait_traits<MonitorT> traits_t;

    for (;;) {
        const zlink::maybe_t<typename traits_t::event_t> event =
          traits_t::try_recv (monitor);
        if (!event)
            break;
        if (traits_t::event_type (*event) != event_type)
            continue;
        if (!monitor_value_matches (traits_t::value (*event), value, cmp))
            continue;
        return true;
    }

    zlink::poller_t poller;
    std::vector<zlink::poll_event_t> events;
    events.reserve (1);
    if (poller.add (monitor, zlink::poll_event::pollin, NULL) != 0)
        return false;

    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (timeout_ms > 0 ? timeout_ms
                                                                      : 1);
    while (std::chrono::steady_clock::now () < deadline) {
        const auto remaining = deadline - std::chrono::steady_clock::now ();
        long wait_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (remaining)
            .count ();
        if (wait_ms < 1)
            wait_ms = 1;

        const int rc = poller.wait_all (events, wait_ms);
        if (rc < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            return false;
        }
        if (rc == 0)
            continue;

        for (;;) {
            const zlink::maybe_t<typename traits_t::event_t> event =
              traits_t::try_recv (monitor);
            if (!event)
                break;
            if (traits_t::event_type (*event) != event_type)
                continue;
            if (!monitor_value_matches (traits_t::value (*event), value, cmp))
                continue;
            return true;
        }
    }

    return false;
}

// Convenience wrappers that default to value_compare::exact.
// A negative expected value matches any observed value.
inline bool wait_socket_monitor_event (zlink::monitor_handle_t &monitor,
                                       uint64_t event_type,
                                       int64_t value,
                                       int timeout_ms)
{
    return wait_monitor_event (monitor, event_type, value,
                               value_compare::exact, timeout_ms);
}

inline bool wait_service_monitor_event (zlink::service_monitor_handle_t &monitor,
                                        uint64_t event_type,
                                        int64_t value,
                                        int timeout_ms)
{
    return wait_monitor_event (monitor, event_type, value,
                               value_compare::exact, timeout_ms);
}

} // namespace perf

#endif
