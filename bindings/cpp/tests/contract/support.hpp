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

inline std::string unique_ipc (const char *base_)
{
    static unsigned counter = 0;
    const unsigned pid =
#if defined(ZLINK_HAVE_WINDOWS)
      static_cast<unsigned> (_getpid ());
#else
      static_cast<unsigned> (getpid ());
#endif
    std::ostringstream stream;
    stream << "ipc:///tmp/" << (base_ ? base_ : "cpp-contract") << "-"
           << pid << "-" << ++counter << ".ipc";
    return stream.str ();
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
    return zlink::message_t::from (text_);
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

template<typename MonitorLike>
inline bool wait_for_monitor_readable (MonitorLike &monitor_, int timeout_ms_)
{
    zlink::poller_t poller;
    if (!poller.valid ())
        return false;
    try {
        poller.add (monitor_, zlink::poll_event_flag_t::pollin, 1);
    }
    catch (const zlink::binding_error_t &) {
        return false;
    }

    zlink::poll_event_t event;
    return poller.wait (&event, 1, std::chrono::milliseconds (timeout_ms_)) == 1;
}

inline bool wait_for_socket_monitor_event (zlink::socket_monitor_t &monitor_,
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
        if (!wait_for_monitor_readable (monitor_, remaining_ms))
            continue;

        const std::optional<zlink::monitor_event_t> event =
          monitor_.recv (zlink::recv_flags_t::dontwait);
        if (!event)
            continue;
        if (static_cast<uint64_t> (event->event) != event_type_)
            continue;
        if (value_ >= 0 && static_cast<int64_t> (event->value) != value_)
            continue;
        return true;
    }

    return false;
}

} // namespace zlink_cpp_contract

#endif
