#ifndef ZLINK_CPP_PERF_MONITOR_WAIT_HPP
#define ZLINK_CPP_PERF_MONITOR_WAIT_HPP

#include <zlink.hpp>

#include <cerrno>
#include <chrono>
#include <vector>

namespace perf {

inline bool wait_socket_monitor_event (zlink::monitor_handle_t &monitor,
                                       uint64_t event_type,
                                       int timeout_ms)
{
    for (;;) {
        const std::optional<zlink::monitor_event_t> event =
      monitor.recv (zlink::recv_flags_t::dontwait);
        if (!event)
            break;
        if (static_cast<uint64_t> (event->event) != event_type)
            continue;
        return true;
    }

    zlink::poller_t poller;
    std::vector<zlink::poll_event_t> events;
    events.reserve (1);
    try {
        poller.add (monitor, zlink::poll_event::pollin, 0);
    }
    catch (const zlink::zlink_error_t &) {
        return false;
    }

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

        events = poller.wait_all (wait_ms);
        const int rc = static_cast<int> (events.size ());
        if (rc < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            return false;
        }
        if (rc == 0)
            continue;

        for (;;) {
            const std::optional<zlink::monitor_event_t> event =
      monitor.recv (zlink::recv_flags_t::dontwait);
            if (!event)
                break;
            if (static_cast<uint64_t> (event->event) != event_type)
                continue;
            return true;
        }
    }

    return false;
}
} // namespace perf

#endif
