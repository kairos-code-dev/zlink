/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SAMPLES_COMMON_SAMPLE_COMMON_HPP_INCLUDED
#define ZLINK_CPP_SAMPLES_COMMON_SAMPLE_COMMON_HPP_INCLUDED

#include <zlink.hpp>

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>

#if defined(ZLINK_HAVE_WINDOWS)
#include <process.h>
#endif

#if !defined(ZLINK_HAVE_WINDOWS)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace detail
{

inline std::string unique_name (const char *base_)
{
    static unsigned counter = 0;
    std::ostringstream stream;
    stream << (base_ ? base_ : "cpp-sample") << "-" << ++counter;
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
    const unsigned port = 30000u + ((pid % 1000u) * 20u) + (++counter);
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

        zlink_socket_monitor_event_t event;
        if (monitor_.recv (event) != 0)
            continue;
        if (event.event != event_type_)
            continue;
        if (value_ >= 0 && static_cast<int64_t> (event.value) != value_)
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

        zlink_service_monitor_event_t event;
        if (monitor_.recv (event) != 0)
            continue;
        if (event.event_type != event_type_)
            continue;
        if (value_ >= 0 && static_cast<int64_t> (event.value) != value_)
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

        zlink_service_monitor_event_t ignored;
        (void) monitor_.recv (ignored);
    }

    return false;
}

#if !defined(ZLINK_HAVE_WINDOWS)
inline int connect_raw_tcp (const std::string &endpoint_)
{
    char proto[8] = {0};
    char host[64] = {0};
    int port = 0;
    if (std::sscanf (
          endpoint_.c_str (), "%7[^:]://%63[^:]:%d", proto, host, &port)
        != 3)
        return -1;

    if (std::strcmp (proto, "tcp") != 0)
        return -1;

    const int fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return -1;

    struct sockaddr_in addr;
    std::memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons (static_cast<uint16_t> (port));

    if (inet_pton (AF_INET, host, &addr.sin_addr) != 1) {
        close (fd);
        return -1;
    }

    if (connect (fd, reinterpret_cast<const struct sockaddr *> (&addr),
                 sizeof (addr))
        != 0) {
        close (fd);
        return -1;
    }

    return fd;
}

inline int send_raw_tcp (int fd_, const char *data_, size_t size_)
{
    return static_cast<int> (send (fd_, data_, size_, 0));
}

inline int recv_raw_tcp (int fd_, char *buffer_, size_t size_)
{
    return static_cast<int> (recv (fd_, buffer_, size_, 0));
}

inline void close_raw_tcp (int fd_)
{
    if (fd_ >= 0)
        close (fd_);
}
#endif

} // namespace detail

#endif
