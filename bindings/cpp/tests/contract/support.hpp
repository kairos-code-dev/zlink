/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_TESTS_CONTRACT_SUPPORT_HPP_INCLUDED
#define ZLINK_CPP_TESTS_CONTRACT_SUPPORT_HPP_INCLUDED

#include <zlink.hpp>

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>

#if defined(ZLINK_HAVE_WINDOWS)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace zlink_cpp_contract
{

inline std::string unique_name (const char *base_)
{
    static unsigned counter = 0;
    std::ostringstream stream;
    stream << (base_ ? base_ : "cpp-contract") << "-" << ++counter;
    return stream.str ();
}

inline std::string unique_inproc (const char *base_)
{
    return std::string ("inproc://") + unique_name (base_);
}

inline std::string unique_tcp (const char *base_)
{
    static unsigned counter = 0;
    const unsigned pid =
#if defined(ZLINK_HAVE_WINDOWS)
      static_cast<unsigned> (_getpid ());
#else
      static_cast<unsigned> (getpid ());
#endif
    const unsigned port = 20000u + ((pid % 1000u) * 20u) + (++counter);
    std::ostringstream stream;
    (void) base_;
    stream << "tcp://127.0.0.1:" << port;
    return stream.str ();
}

inline zlink::message_t make_message (const std::string &text_)
{
    return zlink::message_t::from_string (text_);
}

inline bool wait_until (std::condition_variable &cv_,
                        std::unique_lock<std::mutex> &lock_,
                        bool &ready_,
                        int timeout_ms_)
{
    return cv_.wait_for (
      lock_, std::chrono::milliseconds (timeout_ms_), [&ready_] {
          return ready_;
      });
}

inline bool wait_for_monitor_readable (void *monitor_handle_, int timeout_ms_)
{
    zlink_pollitem_t item;
    item.socket = monitor_handle_;
    item.fd = 0;
    item.events = ZLINK_POLLIN;
    item.revents = 0;

    const int rc = zlink_poll (&item, 1, timeout_ms_);
    return rc > 0 && (item.revents & ZLINK_POLLIN) != 0;
}

inline bool wait_for_socket_monitor_event (zlink::monitor_handle_t &monitor_,
                                           uint64_t event_type_,
                                           int timeout_ms_,
                                           int64_t value_ = -1)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        const std::chrono::steady_clock::duration remaining =
          deadline - std::chrono::steady_clock::now ();
        const int remaining_ms = static_cast<int> (
          std::chrono::duration_cast<std::chrono::milliseconds> (remaining)
            .count ());
        if (!wait_for_monitor_readable (monitor_.handle (), remaining_ms))
            continue;

        const zlink::maybe_t<zlink_socket_monitor_event_t> event =
          monitor_.try_receive ();
        if (!event)
            continue;
        if (event->event != event_type_)
            continue;
        if (value_ >= 0 && static_cast<int64_t> (event->value) != value_)
            continue;
        return true;
    }

    return false;
}

inline bool
wait_for_service_monitor_event (zlink::service_monitor_handle_t &monitor_,
                                uint32_t event_type_,
                                int timeout_ms_,
                                int64_t value_ = -1)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        const std::chrono::steady_clock::duration remaining =
          deadline - std::chrono::steady_clock::now ();
        const int remaining_ms = static_cast<int> (
          std::chrono::duration_cast<std::chrono::milliseconds> (remaining)
            .count ());
        if (!wait_for_monitor_readable (monitor_.handle (), remaining_ms))
            continue;

        const zlink::maybe_t<zlink_service_monitor_event_t> event =
          monitor_.try_receive ();
        if (!event)
            continue;
        if (event->event_type != event_type_)
            continue;
        if (value_ >= 0 && static_cast<int64_t> (event->value) != value_)
            continue;
        return true;
    }

    return false;
}

inline bool
wait_for_service_monitor_event_endpoint (
  zlink::service_monitor_handle_t &monitor_,
  uint32_t event_type_,
  const std::string &endpoint_,
  int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        const std::chrono::steady_clock::duration remaining =
          deadline - std::chrono::steady_clock::now ();
        const int remaining_ms = static_cast<int> (
          std::chrono::duration_cast<std::chrono::milliseconds> (remaining)
            .count ());
        if (!wait_for_monitor_readable (monitor_.handle (), remaining_ms))
            continue;

        const zlink::maybe_t<zlink_service_monitor_event_t> event =
          monitor_.try_receive ();
        if (!event)
            continue;
        if (event->event_type != event_type_)
            continue;
        if ((event->detail_flags & ZLINK_SERVICE_EVENT_DETAIL_ENDPOINT) == 0)
            continue;
        if (endpoint_ != event->endpoint)
            continue;
        return true;
    }

    return false;
}

inline bool
wait_for_service_monitor_state (zlink::service_monitor_handle_t &monitor_,
                                uint32_t state_flags_,
                                int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);

    while (std::chrono::steady_clock::now () < deadline) {
        zlink_monitor_snapshot_t snapshot;
        if (monitor_.snapshot (snapshot) == 0
            && (snapshot.state_flags & state_flags_) == state_flags_) {
            return true;
        }

        const std::chrono::steady_clock::duration remaining =
          deadline - std::chrono::steady_clock::now ();
        int remaining_ms = static_cast<int> (
          std::chrono::duration_cast<std::chrono::milliseconds> (remaining)
            .count ());
        if (remaining_ms <= 0)
            break;
        if (remaining_ms > 200)
            remaining_ms = 200;

        if (!wait_for_monitor_readable (monitor_.handle (), remaining_ms))
            continue;

        (void) monitor_.try_receive ();
    }

    return false;
}

} // namespace zlink_cpp_contract

#endif
