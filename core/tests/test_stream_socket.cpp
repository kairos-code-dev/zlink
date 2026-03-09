/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include "utils/config.hpp"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <atomic>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <vector>

#if defined(ZLINK_HAVE_WINDOWS)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

SETUP_TEARDOWN_TESTCONTEXT

static const size_t stream_routing_id_size = 4;

static bool wait_monitor_event (void *monitor_,
                                void *activity_socket_,
                                uint64_t expected_event_,
                                unsigned char routing_id_[stream_routing_id_size],
                                int timeout_ms_)
{
    const int poll_slice_ms = 200;
    const int poll_timeout = timeout_ms_ > 0 ? timeout_ms_ : 10000;
    const int attempts = poll_timeout / poll_slice_ms + 1;
    for (int i = 0; i < attempts; ++i) {
        zlink_pollitem_t items[] = {
          {monitor_, 0, ZLINK_POLLIN, 0},
          {activity_socket_, 0, ZLINK_POLLIN, 0},
        };
        const int count = activity_socket_ ? 2 : 1;
        const int rc = zlink_poll (items, count, poll_slice_ms);
        if (rc <= 0 || (items[0].revents & ZLINK_POLLIN) == 0)
            continue;

        for (;;) {
            zlink_monitor_event_t event;
            if (zlink_monitor_recv (monitor_, &event, ZLINK_DONTWAIT) != 0)
                break;
            if (event.event != expected_event_)
                continue;
            if (event.routing_id.size != stream_routing_id_size)
                continue;
            memcpy (routing_id_, event.routing_id.data, stream_routing_id_size);
            return true;
        }
    }

    return false;
}

static void send_stream_msg (void *socket_,
                             const unsigned char routing_id_[stream_routing_id_size],
                             const void *data_,
                             size_t size_)
{
    TEST_ASSERT_EQUAL_INT (
      static_cast<int> (stream_routing_id_size),
      TEST_ASSERT_SUCCESS_ERRNO (zlink_send (socket_, routing_id_,
                                             stream_routing_id_size,
                                             ZLINK_SNDMORE)));
    TEST_ASSERT_EQUAL_INT ((int) size_,
                           TEST_ASSERT_SUCCESS_ERRNO (
                             zlink_send (socket_, data_, size_, 0)));
}

static int recv_stream_msg (void *socket_,
                            unsigned char routing_id_[stream_routing_id_size],
                            void *buf_,
                            size_t buf_size_)
{
    int rc = zlink_recv (socket_, routing_id_, stream_routing_id_size, 0);
    if (rc != static_cast<int> (stream_routing_id_size))
        return -1;

    int more = 0;
    size_t more_size = sizeof (more);
    zlink_getsockopt (socket_, ZLINK_RCVMORE, &more, &more_size);
    if (!more)
        return -1;

    return zlink_recv (socket_, buf_, buf_size_, 0);
}

static bool parse_tcp_endpoint (const char *endpoint_,
                                char host_[64],
                                int *port_)
{
    if (!endpoint_ || !host_ || !port_)
        return false;

    char proto[8] = {0};
    int port = 0;
    if (sscanf (endpoint_, "%7[^:]://%63[^:]:%d", proto, host_, &port) != 3)
        return false;

    if (strcmp (proto, "tcp") != 0 || port <= 0 || port > 65535)
        return false;

    *port_ = port;
    return true;
}

#if defined(ZLINK_HAVE_WINDOWS)
static int connect_raw_tcp (const char *endpoint_)
{
    LIBZLINK_UNUSED (endpoint_);
    errno = EOPNOTSUPP;
    return -1;
}

static int send_stream_packet (int fd_, const void *data_, size_t size_)
{
    LIBZLINK_UNUSED (fd_);
    LIBZLINK_UNUSED (data_);
    LIBZLINK_UNUSED (size_);
    return EOPNOTSUPP;
}

static int recv_stream_packet (int fd_, void *buf_, size_t cap_)
{
    LIBZLINK_UNUSED (fd_);
    LIBZLINK_UNUSED (buf_);
    LIBZLINK_UNUSED (cap_);
    return -1;
}

static void close_raw_fd (int fd_)
{
    LIBZLINK_UNUSED (fd_);
}

#else
static int connect_raw_tcp (const char *endpoint_)
{
    char host[64];
    int port = 0;
    if (!parse_tcp_endpoint (endpoint_, host, &port)) {
        errno = EINVAL;
        return -1;
    }

    const int fd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return -1;

    struct sockaddr_in addr;
    memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons (static_cast<uint16_t> (port));
    if (inet_pton (AF_INET, host, &addr.sin_addr) != 1) {
        close (fd);
        errno = EINVAL;
        return -1;
    }

    if (connect (fd, reinterpret_cast<const struct sockaddr *> (&addr),
                 sizeof (addr))
        != 0) {
        const int err = errno;
        close (fd);
        errno = err;
        return -1;
    }

    return fd;
}

static int send_all (int fd_, const unsigned char *buf_, size_t size_)
{
    size_t off = 0;
    while (off < size_) {
        const ssize_t n = send (fd_, buf_ + off, size_ - off, 0);
        if (n > 0) {
            off += static_cast<size_t> (n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

static int send_stream_packet (int fd_, const void *data_, size_t size_)
{
    return send_all (fd_, static_cast<const unsigned char *> (data_), size_);
}

static int recv_stream_packet (int fd_, void *buf_, size_t cap_)
{
    const ssize_t n = recv (fd_, static_cast<unsigned char *> (buf_), cap_, 0);
    if (n <= 0)
        return -1;
    return static_cast<int> (n);
}

static void close_raw_fd (int fd_)
{
    if (fd_ >= 0)
        close (fd_);
}

static int set_raw_fd_timeout (int fd_, int timeout_ms_)
{
    struct timeval tv;
    tv.tv_sec = timeout_ms_ / 1000;
    tv.tv_usec = (timeout_ms_ % 1000) * 1000;
    if (setsockopt (fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv)) != 0)
        return -1;
    if (setsockopt (fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof (tv)) != 0)
        return -1;
    return 0;
}

static int recv_exact (int fd_, void *buf_, size_t size_)
{
    unsigned char *dst = static_cast<unsigned char *> (buf_);
    size_t off = 0;
    while (off < size_) {
        const ssize_t n = recv (fd_, dst + off, size_ - off, 0);
        if (n > 0) {
            off += static_cast<size_t> (n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

static uint32_t load_u32_be (const unsigned char *src_)
{
    return (static_cast<uint32_t> (src_[0]) << 24)
           | (static_cast<uint32_t> (src_[1]) << 16)
           | (static_cast<uint32_t> (src_[2]) << 8)
           | static_cast<uint32_t> (src_[3]);
}

static void store_u32_be (unsigned char *dst_, uint32_t value_)
{
    dst_[0] = static_cast<unsigned char> ((value_ >> 24) & 0xFF);
    dst_[1] = static_cast<unsigned char> ((value_ >> 16) & 0xFF);
    dst_[2] = static_cast<unsigned char> ((value_ >> 8) & 0xFF);
    dst_[3] = static_cast<unsigned char> (value_ & 0xFF);
}

#endif

static void test_sleep_ms (int delay_ms_)
{
#if defined(ZLINK_HAVE_WINDOWS)
    Sleep (static_cast<DWORD> (delay_ms_));
#else
    usleep (delay_ms_ * 1000);
#endif
}

static bool wait_counter_at_least (std::atomic<int> *counter_,
                                   int expected_,
                                   int timeout_ms_)
{
    const int slice_ms = 10;
    const int loops = timeout_ms_ > 0 ? timeout_ms_ / slice_ms + 1 : 1;
    for (int i = 0; i < loops; ++i) {
        if (counter_->load (std::memory_order_acquire) >= expected_)
            return true;
        test_sleep_ms (slice_ms);
    }
    return false;
}

struct stream_callback_probe_t
{
    stream_callback_probe_t () :
        socket (NULL),
        expected_payload (NULL),
        expected_size (0),
        calls (0),
        matched (0),
        send_ok (0),
        send_errno (0),
        routing_id_ready (0)
    {
        memset (routing_id, 0, sizeof (routing_id));
    }

    void *socket;
    const unsigned char *expected_payload;
    size_t expected_size;
    std::atomic<int> calls;
    std::atomic<int> matched;
    std::atomic<int> send_ok;
    std::atomic<int> send_errno;
    unsigned char routing_id[stream_routing_id_size];
    std::atomic<int> routing_id_ready;
};

static stream_callback_probe_t *g_stream_callback_probe = NULL;

struct stream_load_probe_t
{
    stream_load_probe_t () :
        socket (NULL),
        expected_payload_size (0),
        echoed_msgs (0),
        control_chunks (0),
        invalid_payload (0),
        send_fail (0)
    {
    }

    void *socket;
    size_t expected_payload_size;
    std::atomic<int> echoed_msgs;
    std::atomic<int> control_chunks;
    std::atomic<int> invalid_payload;
    std::atomic<int> send_fail;
};

static stream_load_probe_t *g_stream_load_probe = NULL;

struct stream_raw_stash_t
{
    stream_raw_stash_t () : data (), offset (0) {}

    std::vector<unsigned char> data;
    size_t offset;

    size_t available () const { return data.size () - offset; }

    void append (const unsigned char *src_, size_t size_)
    {
        if (!src_ || size_ == 0)
            return;
        if (offset >= data.size ()) {
            data.clear ();
            offset = 0;
        }
        const size_t old_size = data.size ();
        data.resize (old_size + size_);
        memcpy (&data[old_size], src_, size_);
    }

    void compact ()
    {
        if (offset == 0)
            return;
        if (offset >= data.size ()) {
            data.clear ();
            offset = 0;
            return;
        }
        if (offset > 4096 || offset * 2 >= data.size ()) {
            const size_t remain = data.size () - offset;
            memmove (&data[0], &data[offset], remain);
            data.resize (remain);
            offset = 0;
        }
    }

    void reset ()
    {
        data.clear ();
        offset = 0;
    }
};

struct stream_raw_load_probe_t
{
    stream_raw_load_probe_t () :
        socket (NULL),
        expected_payload_size (0),
        echoed_msgs (0),
        control_chunks (0),
        parse_error (0),
        send_fail (0),
        stashes (),
        stash_mu ()
    {
    }

    void *socket;
    size_t expected_payload_size;
    std::atomic<int> echoed_msgs;
    std::atomic<int> control_chunks;
    std::atomic<int> parse_error;
    std::atomic<int> send_fail;
    std::map<std::string, stream_raw_stash_t> stashes;
    std::mutex stash_mu;
};

static stream_raw_load_probe_t *g_stream_raw_load_probe = NULL;

static bool is_stream_control_chunk (const unsigned char *data_, size_t size_)
{
    (void) data_;
    return size_ == 0;
}

static void release_stream_callback_msg (zlink_msg_t *msg_)
{
    if (msg_)
        (void) zlink_msg_close (msg_);
}

static std::string make_routing_id_key (const unsigned char *data_, size_t size_)
{
    if (!data_ || size_ == 0)
        return std::string ();

    return std::string (reinterpret_cast<const char *> (data_), size_);
}

struct stream_monitor_probe_t
{
    stream_monitor_probe_t () :
        accepted (0),
        connection_ready (0),
        disconnected (0),
        bad_ready_routing_id (0),
        bad_disconnected_routing_id (0),
        ready_routing_ids (),
        disconnected_routing_ids ()
    {
    }

    int accepted;
    int connection_ready;
    int disconnected;
    int bad_ready_routing_id;
    int bad_disconnected_routing_id;
    std::set<std::string> ready_routing_ids;
    std::set<std::string> disconnected_routing_ids;
};

static void record_stream_monitor_event (stream_monitor_probe_t *probe_,
                                         const zlink_monitor_event_t *event_)
{
    if (!probe_ || !event_)
        return;

    switch (event_->event) {
        case ZLINK_EVENT_ACCEPTED:
            ++probe_->accepted;
            break;
        case ZLINK_EVENT_CONNECTION_READY:
            ++probe_->connection_ready;
            if (event_->routing_id.size != stream_routing_id_size) {
                ++probe_->bad_ready_routing_id;
                break;
            }
            probe_->ready_routing_ids.insert (
              make_routing_id_key (event_->routing_id.data,
                                   event_->routing_id.size));
            break;
        case ZLINK_EVENT_DISCONNECTED:
            ++probe_->disconnected;
            if (event_->routing_id.size != stream_routing_id_size) {
                ++probe_->bad_disconnected_routing_id;
                break;
            }
            probe_->disconnected_routing_ids.insert (
              make_routing_id_key (event_->routing_id.data,
                                   event_->routing_id.size));
            break;
        default:
            break;
    }
}

static void collect_stream_monitor_events (void *monitor_,
                                           stream_monitor_probe_t *probe_,
                                           int poll_timeout_ms_)
{
    if (!monitor_ || !probe_)
        return;

    zlink_pollitem_t items[] = {{monitor_, 0, ZLINK_POLLIN, 0}};
    const int rc = zlink_poll (items, 1, poll_timeout_ms_);
    if (rc <= 0 || (items[0].revents & ZLINK_POLLIN) == 0)
        return;

    for (;;) {
        zlink_monitor_event_t event;
        if (zlink_monitor_recv (monitor_, &event, ZLINK_DONTWAIT) != 0)
            break;
        record_stream_monitor_event (probe_, &event);
    }
}

static bool wait_stream_monitor_progress (void *monitor_,
                                          stream_monitor_probe_t *probe_,
                                          int expected_accepted_,
                                          size_t expected_ready_,
                                          size_t expected_disconnected_,
                                          int timeout_ms_)
{
    const int slice_ms = 20;
    const int loops = timeout_ms_ > 0 ? timeout_ms_ / slice_ms + 1 : 1;
    for (int i = 0; i < loops; ++i) {
        collect_stream_monitor_events (monitor_, probe_, slice_ms);
        if (probe_->accepted >= expected_accepted_
            && probe_->ready_routing_ids.size () >= expected_ready_
            && probe_->disconnected_routing_ids.size ()
                 >= expected_disconnected_) {
            return true;
        }
    }

    collect_stream_monitor_events (monitor_, probe_, 0);
    return probe_->accepted >= expected_accepted_
           && probe_->ready_routing_ids.size () >= expected_ready_
           && probe_->disconnected_routing_ids.size () >= expected_disconnected_;
}

struct stream_monitor_callback_probe_t
{
    stream_monitor_callback_probe_t () :
        socket (NULL),
        expected_payload_size (0),
        echoed_msgs (0),
        bad_payload (0),
        send_fail (0),
        routing_ids (),
        routing_id_mu ()
    {
    }

    void *socket;
    size_t expected_payload_size;
    std::atomic<int> echoed_msgs;
    std::atomic<int> bad_payload;
    std::atomic<int> send_fail;
    std::set<std::string> routing_ids;
    std::mutex routing_id_mu;
};

static stream_monitor_callback_probe_t *g_stream_monitor_callback_probe = NULL;

struct stream_ordering_probe_t
{
    stream_ordering_probe_t () :
        ready_events (0),
        disconnected_events (0),
        payload_before_ready (0),
        disconnect_during_payload (0),
        strict_drop_count (0),
        strict_accept_count (0),
        payloads_seen (0),
        ready_routing_ids (),
        disconnected_routing_ids (),
        active_payload_routing_ids (),
        seen_payload_routing_ids (),
        mu ()
    {
    }

    std::atomic<int> ready_events;
    std::atomic<int> disconnected_events;
    std::atomic<int> payload_before_ready;
    std::atomic<int> disconnect_during_payload;
    std::atomic<int> strict_drop_count;
    std::atomic<int> strict_accept_count;
    std::atomic<int> payloads_seen;
    std::set<std::string> ready_routing_ids;
    std::set<std::string> disconnected_routing_ids;
    std::set<std::string> active_payload_routing_ids;
    std::set<std::string> seen_payload_routing_ids;
    std::mutex mu;
};

struct stream_ordering_callback_probe_t
{
    stream_ordering_callback_probe_t () :
        socket (NULL),
        monitor (NULL),
        ordering (NULL),
        strict_gate (false),
        callback_payloads (0),
        send_fail (0),
        monitor_mu ()
    {
    }

    void *socket;
    void *monitor;
    stream_ordering_probe_t *ordering;
    bool strict_gate;
    std::atomic<int> callback_payloads;
    std::atomic<int> send_fail;
    std::mutex monitor_mu;
};

static stream_ordering_callback_probe_t *g_stream_raw_ordering_callback_probe =
  NULL;
static stream_ordering_callback_probe_t
  *g_stream_len32be_ordering_callback_probe = NULL;

static void collect_stream_ordering_monitor_events (
  void *monitor_,
  stream_ordering_probe_t *probe_,
  int poll_timeout_ms_)
{
    if (!monitor_ || !probe_)
        return;

    zlink_pollitem_t items[] = {{monitor_, 0, ZLINK_POLLIN, 0}};
    const int rc = zlink_poll (items, 1, poll_timeout_ms_);
    if (rc <= 0 || (items[0].revents & ZLINK_POLLIN) == 0)
        return;

    for (;;) {
        zlink_monitor_event_t event;
        if (zlink_monitor_recv (monitor_, &event, ZLINK_DONTWAIT) != 0)
            break;

        if (event.routing_id.size != stream_routing_id_size)
            continue;

        const std::string key =
          make_routing_id_key (event.routing_id.data, event.routing_id.size);
        std::lock_guard<std::mutex> lk (probe_->mu);
        if (event.event == ZLINK_EVENT_CONNECTION_READY) {
            probe_->ready_routing_ids.insert (key);
            probe_->ready_events.fetch_add (1, std::memory_order_release);
        } else if (event.event == ZLINK_EVENT_DISCONNECTED) {
            if (probe_->active_payload_routing_ids.find (key)
                != probe_->active_payload_routing_ids.end ()) {
                probe_->disconnect_during_payload.fetch_add (
                  1, std::memory_order_release);
            }
            probe_->disconnected_routing_ids.insert (key);
            probe_->disconnected_events.fetch_add (
              1, std::memory_order_release);
        }
    }
}

static bool mark_stream_payload_begin (stream_ordering_probe_t *probe_,
                                       const zlink_routing_id_t *rid_,
                                       bool strict_gate_)
{
    if (!probe_ || !rid_ || rid_->size != stream_routing_id_size)
        return false;

    const std::string key = make_routing_id_key (rid_->data, rid_->size);
    std::lock_guard<std::mutex> lk (probe_->mu);
    probe_->payloads_seen.fetch_add (1, std::memory_order_release);
    probe_->active_payload_routing_ids.insert (key);
    const bool ready =
      probe_->ready_routing_ids.find (key) != probe_->ready_routing_ids.end ();
    const bool first_payload =
      probe_->seen_payload_routing_ids.insert (key).second;
    if (first_payload && !ready) {
        probe_->payload_before_ready.fetch_add (1, std::memory_order_release);
    }
    if (strict_gate_) {
        if (ready)
            probe_->strict_accept_count.fetch_add (1, std::memory_order_release);
        else
            probe_->strict_drop_count.fetch_add (1, std::memory_order_release);
    }
    return ready;
}

static void mark_stream_payload_end (stream_ordering_probe_t *probe_,
                                     const zlink_routing_id_t *rid_)
{
    if (!probe_ || !rid_ || rid_->size != stream_routing_id_size)
        return;

    const std::string key = make_routing_id_key (rid_->data, rid_->size);
    std::lock_guard<std::mutex> lk (probe_->mu);
    probe_->active_payload_routing_ids.erase (key);
}

static bool wait_stream_ordering_counter (std::atomic<int> *counter_,
                                          int expected_,
                                          int timeout_ms_)
{
    return wait_counter_at_least (counter_, expected_, timeout_ms_);
}

static bool wait_stream_ordering_sets (stream_ordering_probe_t *probe_,
                                       size_t expected_ready_,
                                       size_t expected_disconnected_,
                                       int timeout_ms_)
{
    const int slice_ms = 20;
    const int loops = timeout_ms_ > 0 ? timeout_ms_ / slice_ms + 1 : 1;
    for (int i = 0; i < loops; ++i) {
        {
            std::lock_guard<std::mutex> lk (probe_->mu);
            if (probe_->ready_routing_ids.size () >= expected_ready_
                && probe_->disconnected_routing_ids.size ()
                     >= expected_disconnected_) {
                return true;
            }
        }
        test_sleep_ms (slice_ms);
    }

    std::lock_guard<std::mutex> lk (probe_->mu);
    return probe_->ready_routing_ids.size () >= expected_ready_
           && probe_->disconnected_routing_ids.size ()
                >= expected_disconnected_;
}

static bool wait_stream_ordering_sets_with_monitor (void *monitor_,
                                                    stream_ordering_probe_t *probe_,
                                                    size_t expected_ready_,
                                                    size_t expected_disconnected_,
                                                    int timeout_ms_)
{
    const int slice_ms = 20;
    const int loops = timeout_ms_ > 0 ? timeout_ms_ / slice_ms + 1 : 1;
    for (int i = 0; i < loops; ++i) {
        collect_stream_ordering_monitor_events (monitor_, probe_, slice_ms);
        {
            std::lock_guard<std::mutex> lk (probe_->mu);
            if (probe_->ready_routing_ids.size () >= expected_ready_
                && probe_->disconnected_routing_ids.size ()
                     >= expected_disconnected_) {
                return true;
            }
        }
    }

    collect_stream_ordering_monitor_events (monitor_, probe_, 0);
    std::lock_guard<std::mutex> lk (probe_->mu);
    return probe_->ready_routing_ids.size () >= expected_ready_
           && probe_->disconnected_routing_ids.size ()
                >= expected_disconnected_;
}

static void configure_stream_regression_socket (void *socket_, int backlog_)
{
    TEST_ASSERT_NOT_NULL (socket_);

    const int zero = 0;
    const int hwm = 10;
    const int timeout_ms = 200;
    const int nodelay = 1;
    const int backlog = backlog_ > 0 ? backlog_ : 256;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_SNDHWM, &hwm, sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_RCVHWM, &hwm, sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_SNDTIMEO, &timeout_ms,
                        sizeof (timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_RCVTIMEO, &timeout_ms,
                        sizeof (timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_BACKLOG, &backlog, sizeof (backlog)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_TCP_NODELAY, &nodelay,
                        sizeof (nodelay)));
}

static int stream_raw_ordering_echo_callback (const zlink_routing_id_t *rid_,
                                              zlink_msg_t *msg_)
{
    stream_ordering_callback_probe_t *probe =
      g_stream_raw_ordering_callback_probe;
    if (!probe || !probe->ordering || !rid_ || !msg_)
        return 0;

    const unsigned char *payload =
      static_cast<const unsigned char *> (zlink_msg_data (msg_));
    const size_t payload_size = zlink_msg_size (msg_);
    if (is_stream_control_chunk (payload, payload_size)) {
        release_stream_callback_msg (msg_);
        return 0;
    }

    probe->callback_payloads.fetch_add (1, std::memory_order_release);
    if (probe->monitor) {
        std::lock_guard<std::mutex> lk (probe->monitor_mu);
        collect_stream_ordering_monitor_events (probe->monitor, probe->ordering, 0);
    }
    const bool ready =
      mark_stream_payload_begin (probe->ordering, rid_, probe->strict_gate);

    if (!probe->strict_gate || ready) {
        const int send_rc =
          zlink_stream_send_msg (probe->socket, rid_, msg_, 0);
        if (send_rc != static_cast<int> (payload_size))
            probe->send_fail.fetch_add (1, std::memory_order_release);
    }

    mark_stream_payload_end (probe->ordering, rid_);
    release_stream_callback_msg (msg_);
    return 0;
}

static int stream_len32be_ordering_echo_callback (const zlink_routing_id_t *rid_,
                                                  zlink_msg_t *msgs_,
                                                  size_t msg_count_)
{
    stream_ordering_callback_probe_t *probe =
      g_stream_len32be_ordering_callback_probe;
    if (!probe || !probe->ordering || !rid_ || !msgs_ || msg_count_ == 0)
        return 0;

    for (size_t i = 0; i < msg_count_; ++i) {
        zlink_msg_t *msg = &msgs_[i];
        const unsigned char *payload =
          static_cast<const unsigned char *> (zlink_msg_data (msg));
        const size_t payload_size = zlink_msg_size (msg);
        if (is_stream_control_chunk (payload, payload_size)) {
            release_stream_callback_msg (msg);
            continue;
        }

        probe->callback_payloads.fetch_add (1, std::memory_order_release);
        if (probe->monitor) {
            std::lock_guard<std::mutex> lk (probe->monitor_mu);
            collect_stream_ordering_monitor_events (probe->monitor,
                                                    probe->ordering, 0);
        }
        const bool ready =
          mark_stream_payload_begin (probe->ordering, rid_, probe->strict_gate);

        if (!probe->strict_gate || ready) {
            const int send_rc = zlink_stream_send_msg (probe->socket, rid_, msg, 0);
            if (send_rc != static_cast<int> (payload_size))
                probe->send_fail.fetch_add (1, std::memory_order_release);
        }

        mark_stream_payload_end (probe->ordering, rid_);
        release_stream_callback_msg (msg);
    }

    return 0;
}

static int stream_monitor_len32be_echo_callback (const zlink_routing_id_t *rid_,
                                                 zlink_msg_t *msgs_,
                                                 size_t msg_count_)
{
    stream_monitor_callback_probe_t *probe = g_stream_monitor_callback_probe;
    if (!probe || !rid_ || !msgs_ || msg_count_ == 0)
        return 0;

    if (rid_->size == stream_routing_id_size) {
        std::lock_guard<std::mutex> lk (probe->routing_id_mu);
        probe->routing_ids.insert (make_routing_id_key (rid_->data, rid_->size));
    }

    for (size_t i = 0; i < msg_count_; ++i) {
        zlink_msg_t *msg = &msgs_[i];
        const unsigned char *data =
          static_cast<const unsigned char *> (zlink_msg_data (msg));
        const size_t size = zlink_msg_size (msg);
        if (is_stream_control_chunk (data, size)) {
            release_stream_callback_msg (msg);
            continue;
        }

        if (probe->expected_payload_size > 0
            && size != probe->expected_payload_size) {
            probe->bad_payload.fetch_add (1, std::memory_order_release);
            release_stream_callback_msg (msg);
            continue;
        }

        const int send_rc = zlink_stream_send_msg (probe->socket, rid_, msg, 0);
        if (send_rc != static_cast<int> (size)) {
            probe->send_fail.fetch_add (1, std::memory_order_release);
            release_stream_callback_msg (msg);
            continue;
        }

        probe->echoed_msgs.fetch_add (1, std::memory_order_release);
        release_stream_callback_msg (msg);
    }

    return 0;
}

static void fill_monitor_test_payload (std::vector<unsigned char> *payload_,
                                       uint32_t client_id_)
{
    if (!payload_ || payload_->size () < 8)
        return;

    store_u32_be (&(*payload_)[0], client_id_);
    store_u32_be (&(*payload_)[4], 1);
    for (size_t i = 8; i < payload_->size (); ++i)
        (*payload_)[i] = static_cast<unsigned char> ((client_id_ + i) & 0xFF);
}

static int stream_echo_callback (const zlink_routing_id_t *rid_,
                                 zlink_msg_t *msgs_,
                                 size_t msg_count_)
{
    stream_callback_probe_t *probe = g_stream_callback_probe;
    if (!probe || !rid_ || !msgs_ || msg_count_ == 0)
        return 0;

    for (size_t i = 0; i < msg_count_; ++i) {
        zlink_msg_t *msg = &msgs_[i];
        probe->calls.fetch_add (1, std::memory_order_release);

        const unsigned char *data =
          static_cast<const unsigned char *> (zlink_msg_data (msg));
        const size_t size = zlink_msg_size (msg);
        if (is_stream_control_chunk (data, size)) {
            release_stream_callback_msg (msg);
            continue;
        }

        if (!probe->expected_payload || size != probe->expected_size) {
            release_stream_callback_msg (msg);
            continue;
        }

        if (memcmp (data, probe->expected_payload, size) != 0) {
            release_stream_callback_msg (msg);
            continue;
        }

        if (rid_->size == stream_routing_id_size) {
            memcpy (probe->routing_id, rid_->data, stream_routing_id_size);
            probe->routing_id_ready.store (1, std::memory_order_release);
        }

        const int send_rc =
          zlink_stream_send_msg (probe->socket, rid_, msg, 0);
        if (send_rc == static_cast<int> (size))
            probe->send_ok.store (1, std::memory_order_release);
        else
            probe->send_errno.store (errno, std::memory_order_release);

        probe->matched.fetch_add (1, std::memory_order_release);
        release_stream_callback_msg (msg);
    }
    return 0;
}

static int stream_load_echo_callback (const zlink_routing_id_t *rid_,
                                      zlink_msg_t *msgs_,
                                      size_t msg_count_)
{
    stream_load_probe_t *probe = g_stream_load_probe;
    if (!probe || !rid_ || !msgs_ || msg_count_ == 0)
        return 0;

    for (size_t i = 0; i < msg_count_; ++i) {
        zlink_msg_t *msg = &msgs_[i];
        const unsigned char *data =
          static_cast<const unsigned char *> (zlink_msg_data (msg));
        const size_t size = zlink_msg_size (msg);
        if (is_stream_control_chunk (data, size)) {
            probe->control_chunks.fetch_add (1, std::memory_order_release);
            release_stream_callback_msg (msg);
            continue;
        }

        if (!data || size < 8
            || (probe->expected_payload_size > 0
                && size != probe->expected_payload_size)) {
            probe->invalid_payload.fetch_add (1, std::memory_order_release);
            release_stream_callback_msg (msg);
            continue;
        }

        const int send_rc = zlink_stream_send_msg (probe->socket, rid_, msg, 0);
        if (send_rc != static_cast<int> (size)) {
            probe->send_fail.fetch_add (1, std::memory_order_release);
            release_stream_callback_msg (msg);
            continue;
        }

        probe->echoed_msgs.fetch_add (1, std::memory_order_release);
        release_stream_callback_msg (msg);
    }

    return 0;
}

static int stream_raw_load_echo_callback (const zlink_routing_id_t *rid_,
                                          zlink_msg_t *msgs_,
                                          size_t msg_count_)
{
    stream_raw_load_probe_t *probe = g_stream_raw_load_probe;
    if (!probe || !rid_ || !msgs_ || msg_count_ == 0)
        return 0;

    for (size_t i = 0; i < msg_count_; ++i) {
        zlink_msg_t *msg = &msgs_[i];
        const unsigned char *payload =
          static_cast<const unsigned char *> (zlink_msg_data (msg));
        const size_t payload_size = zlink_msg_size (msg);

        if (is_stream_control_chunk (payload, payload_size)) {
            probe->control_chunks.fetch_add (1, std::memory_order_release);
            release_stream_callback_msg (msg);
            continue;
        }

        if ((!payload && payload_size > 0) || payload_size == 0) {
            probe->parse_error.fetch_add (1, std::memory_order_release);
            release_stream_callback_msg (msg);
            continue;
        }

        std::vector<std::vector<unsigned char> > complete_frames;
        bool invalid = false;
        {
            const std::string key (reinterpret_cast<const char *> (rid_->data),
                                   rid_->size);
            std::lock_guard<std::mutex> lk (probe->stash_mu);
            stream_raw_stash_t &stash = probe->stashes[key];
            stash.append (payload, payload_size);

            size_t consumed = 0;
            const size_t available = stash.available ();
            while (consumed + 4 <= available) {
                const unsigned char *frame = &stash.data[stash.offset + consumed];
                const size_t body_size =
                  static_cast<size_t> (load_u32_be (frame));

                if (body_size < 8 || body_size > 4 * 1024 * 1024
                    || (probe->expected_payload_size > 0
                        && body_size != probe->expected_payload_size)) {
                    invalid = true;
                    break;
                }

                const size_t frame_size = 4 + body_size;
                if (consumed + frame_size > available)
                    break;

                complete_frames.push_back (std::vector<unsigned char> (frame_size));
                memcpy (&complete_frames.back ()[0], frame, frame_size);
                consumed += frame_size;
            }

            if (invalid) {
                stash.reset ();
            } else if (consumed > 0) {
                stash.offset += consumed;
                stash.compact ();
            }
        }

        if (invalid) {
            probe->parse_error.fetch_add (1, std::memory_order_release);
            release_stream_callback_msg (msg);
            continue;
        }

        for (size_t j = 0; j < complete_frames.size (); ++j) {
            std::vector<unsigned char> &frame = complete_frames[j];
            const int send_rc = zlink_stream_send (
              probe->socket, rid_, &frame[0], frame.size (), 0);
            if (send_rc != static_cast<int> (frame.size ())) {
                probe->send_fail.fetch_add (1, std::memory_order_release);
                continue;
            }
            probe->echoed_msgs.fetch_add (1, std::memory_order_release);
        }

        release_stream_callback_msg (msg);
    }

    return 0;
}

static int stream_echo_raw_callback (const zlink_routing_id_t *rid_,
                                     zlink_msg_t *msg_)
{
    return stream_echo_callback (rid_, msg_, 1);
}

static int stream_raw_load_echo_raw_callback (const zlink_routing_id_t *rid_,
                                              zlink_msg_t *msg_)
{
    return stream_raw_load_echo_callback (rid_, msg_, 1);
}

void test_stream_callback_lifecycle ()
{
    void *pair = test_context_socket (ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (pair);

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1,
                           zlink_stream_attach_raw (pair,
                                                    stream_echo_raw_callback));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    test_context_socket_close_zero_linger (pair);

    void *stream = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (stream);

    stream_callback_probe_t probe;
    probe.socket = stream;

    g_stream_callback_probe = &probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_stream_attach_raw (stream, stream_echo_raw_callback));
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1,
                           zlink_stream_attach_raw (stream,
                                                    stream_echo_raw_callback));
    TEST_ASSERT_EQUAL_INT (EBUSY, errno);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (stream));
    g_stream_callback_probe = NULL;
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_stream_detach (stream));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    test_context_socket_close_zero_linger (stream);
}

void test_stream_recv_api_dispatch_conflict ()
{
    void *stream = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (stream);

    unsigned char buf[16];
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_recv (stream, buf, sizeof (buf),
                                           ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);

    zlink_msg_t msg;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&msg));
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_msg_recv (&msg, stream, ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&msg));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_stream_attach_raw (stream, stream_echo_raw_callback));
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_recv (stream, buf, sizeof (buf),
                                           ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EBUSY, errno);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&msg));
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_msg_recv (&msg, stream, ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EBUSY, errno);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&msg));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (stream));

    test_context_socket_close_zero_linger (stream);
}

void test_stream_dispatch_start_rejects_stream_notify ()
{
    void *stream = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (stream);

    const int enable = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (stream, ZLINK_STREAM_NOTIFY, &enable, sizeof (enable)));

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_stream_attach_raw (stream, stream_echo_raw_callback));
    TEST_ASSERT_EQUAL_INT (EOPNOTSUPP, errno);

    const int disable = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (stream, ZLINK_STREAM_NOTIFY, &disable, sizeof (disable)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_stream_attach_raw (stream, stream_echo_raw_callback));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (stream));

    test_context_socket_close_zero_linger (stream);
}

#if defined(ZLINK_HAVE_WINDOWS)
void test_stream_callback_echo_raw ()
{
    TEST_IGNORE_MESSAGE ("raw tcp helper unavailable on Windows");
}

void test_stream_callback_echo_len32be ()
{
    TEST_IGNORE_MESSAGE ("raw tcp helper unavailable on Windows");
}

void test_stream_callback_echo_single_zero_byte ()
{
    TEST_IGNORE_MESSAGE ("raw tcp helper unavailable on Windows");
}

void test_stream_len32be_single_frame ()
{
    TEST_IGNORE_MESSAGE ("raw tcp helper unavailable on Windows");
}

void test_stream_len32be_header_split ()
{
    TEST_IGNORE_MESSAGE ("raw tcp helper unavailable on Windows");
}

void test_stream_len32be_body_split ()
{
    TEST_IGNORE_MESSAGE ("raw tcp helper unavailable on Windows");
}

void test_stream_len32be_multi_frame_single_read ()
{
    TEST_IGNORE_MESSAGE ("raw tcp helper unavailable on Windows");
}

void test_stream_len32be_callback_lifecycle_contract ()
{
    TEST_IGNORE_MESSAGE ("raw tcp helper unavailable on Windows");
}
#else
void test_stream_callback_echo_raw ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (0, set_raw_fd_timeout (client_fd, 3000));

    const unsigned char payload[] = "stream-callback-raw";
    stream_callback_probe_t probe;
    probe.socket = server;
    probe.expected_payload = payload;
    probe.expected_size = sizeof (payload) - 1;

    g_stream_callback_probe = &probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_stream_attach_raw (server, stream_echo_raw_callback));
    TEST_ASSERT_EQUAL_INT (0, send_stream_packet (
                                client_fd, payload, sizeof (payload) - 1));

    unsigned char recv_buf[sizeof (payload)];
    TEST_ASSERT_EQUAL_INT (0, recv_exact (client_fd, recv_buf,
                                          sizeof (payload) - 1));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      payload, recv_buf, static_cast<unsigned int> (sizeof (payload) - 1));

    TEST_ASSERT_TRUE (wait_counter_at_least (&probe.matched, 1, 3000));
    TEST_ASSERT_EQUAL_INT (1, probe.send_ok.load (std::memory_order_acquire));
    TEST_ASSERT_TRUE (wait_counter_at_least (&probe.routing_id_ready, 1, 3000));

    const unsigned char two_part_reply[] = "stream-direct-2part";
    TEST_ASSERT_EQUAL_INT (
      static_cast<int> (stream_routing_id_size),
      TEST_ASSERT_SUCCESS_ERRNO (zlink_send (server, probe.routing_id,
                                             stream_routing_id_size,
                                             ZLINK_SNDMORE)));
    TEST_ASSERT_EQUAL_INT (
      static_cast<int> (sizeof (two_part_reply) - 1),
      TEST_ASSERT_SUCCESS_ERRNO (zlink_send (
        server, two_part_reply, sizeof (two_part_reply) - 1, 0)));

    unsigned char recv_two_part[sizeof (two_part_reply)];
    TEST_ASSERT_EQUAL_INT (
      0, recv_exact (client_fd, recv_two_part, sizeof (two_part_reply) - 1));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      two_part_reply, recv_two_part,
      static_cast<unsigned int> (sizeof (two_part_reply) - 1));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_stream_callback_probe = NULL;

    close_raw_fd (client_fd);
    test_context_socket_close_zero_linger (server);
}

void test_stream_callback_echo_len32be ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (0, set_raw_fd_timeout (client_fd, 3000));

    const unsigned char payload[] = "stream-callback-len32be";
    stream_callback_probe_t probe;
    probe.socket = server;
    probe.expected_payload = payload;
    probe.expected_size = sizeof (payload) - 1;

    g_stream_callback_probe = &probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_attach (
      server, stream_echo_callback, ZLINK_STREAM_DISPATCH_LEN32BE));

    unsigned char req_frame[4 + sizeof (payload) - 1];
    store_u32_be (&req_frame[0], sizeof (payload) - 1);
    memcpy (&req_frame[4], payload, sizeof (payload) - 1);

    TEST_ASSERT_EQUAL_INT (0, send_stream_packet (client_fd, req_frame, 2));
    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, req_frame + 2, sizeof (req_frame) - 2));

    unsigned char resp_frame[4 + sizeof (payload) - 1];
    TEST_ASSERT_EQUAL_INT (
      0, recv_exact (client_fd, resp_frame, sizeof (resp_frame)));

    const uint32_t body_size = load_u32_be (&resp_frame[0]);
    TEST_ASSERT_EQUAL_UINT32 (
      static_cast<uint32_t> (sizeof (payload) - 1), body_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      payload, &resp_frame[4], static_cast<unsigned int> (sizeof (payload) - 1));

    TEST_ASSERT_TRUE (wait_counter_at_least (&probe.matched, 1, 3000));
    TEST_ASSERT_EQUAL_INT (1, probe.send_ok.load (std::memory_order_acquire));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_stream_callback_probe = NULL;

    close_raw_fd (client_fd);
    test_context_socket_close_zero_linger (server);
}

void test_stream_callback_echo_single_zero_byte ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (0, set_raw_fd_timeout (client_fd, 3000));

    const unsigned char payload[1] = {0x00};
    stream_callback_probe_t probe;
    probe.socket = server;
    probe.expected_payload = payload;
    probe.expected_size = sizeof (payload);

    g_stream_callback_probe = &probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_stream_attach_raw (server, stream_echo_raw_callback));
    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, payload, sizeof (payload)));

    unsigned char recv_buf[sizeof (payload)];
    TEST_ASSERT_EQUAL_INT (
      0, recv_exact (client_fd, recv_buf, sizeof (payload)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      payload, recv_buf, static_cast<unsigned int> (sizeof (payload)));

    TEST_ASSERT_TRUE (wait_counter_at_least (&probe.matched, 1, 3000));
    TEST_ASSERT_EQUAL_INT (1, probe.send_ok.load (std::memory_order_acquire));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_stream_callback_probe = NULL;
    close_raw_fd (client_fd);
    test_context_socket_close_zero_linger (server);
}

struct stream_len32be_probe_t
{
    stream_len32be_probe_t () :
        socket (NULL),
        expected_packets (),
        expected_batch_sizes (),
        next_packet_index (0),
        next_batch_index (0),
        callbacks (0),
        packets (0),
        bad_payload (0),
        bad_batch (0),
        send_fail (0),
        lifecycle_observed (0),
        have_prev_slot (false),
        prev_slot (NULL),
        prev_payload ()
    {
    }

    void *socket;
    std::vector<std::vector<unsigned char> > expected_packets;
    std::vector<size_t> expected_batch_sizes;
    std::atomic<int> next_packet_index;
    std::atomic<int> next_batch_index;
    std::atomic<int> callbacks;
    std::atomic<int> packets;
    std::atomic<int> bad_payload;
    std::atomic<int> bad_batch;
    std::atomic<int> send_fail;
    std::atomic<int> lifecycle_observed;
    bool have_prev_slot;
    zlink_msg_t *prev_slot;
    std::vector<unsigned char> prev_payload;
};

static stream_len32be_probe_t *g_stream_len32be_probe = NULL;

static void push_expected_len32be_packet (stream_len32be_probe_t *probe_,
                                          const unsigned char *payload_,
                                          size_t size_)
{
    probe_->expected_packets.push_back (std::vector<unsigned char> ());
    std::vector<unsigned char> &packet = probe_->expected_packets.back ();
    packet.resize (size_);
    if (size_ > 0)
        memcpy (&packet[0], payload_, size_);
}

static int stream_len32be_probe_callback (const zlink_routing_id_t *rid_,
                                          zlink_msg_t *msgs_,
                                          size_t msg_count_)
{
    stream_len32be_probe_t *probe = g_stream_len32be_probe;
    if (!probe || !rid_ || !msgs_ || msg_count_ == 0)
        return 0;

    probe->callbacks.fetch_add (1, std::memory_order_release);
    const int batch_index =
      probe->next_batch_index.fetch_add (1, std::memory_order_acq_rel);
    if (batch_index < 0
        || static_cast<size_t> (batch_index) >= probe->expected_batch_sizes.size ()
        || probe->expected_batch_sizes[static_cast<size_t> (batch_index)]
             != msg_count_) {
        probe->bad_batch.fetch_add (1, std::memory_order_release);
    }

    for (size_t i = 0; i < msg_count_; ++i) {
        zlink_msg_t *msg = &msgs_[i];
        const unsigned char *data =
          static_cast<const unsigned char *> (zlink_msg_data (msg));
        const size_t size = zlink_msg_size (msg);

        bool payload_ok = false;
        const int packet_index =
          probe->next_packet_index.fetch_add (1, std::memory_order_acq_rel);
        if (packet_index >= 0
            && static_cast<size_t> (packet_index)
                 < probe->expected_packets.size ()) {
            const std::vector<unsigned char> &expected =
              probe->expected_packets[static_cast<size_t> (packet_index)];
            payload_ok =
              expected.size () == size
              && (size == 0
                  || (data && memcmp (&expected[0], data, size) == 0));
        }
        if (!payload_ok)
            probe->bad_payload.fetch_add (1, std::memory_order_release);

        if (probe->have_prev_slot) {
            const bool same_slot = probe->prev_slot == msg;
            bool same_payload = probe->prev_payload.size () == size;
            if (same_payload && size > 0) {
                same_payload =
                  data && memcmp (&probe->prev_payload[0], data, size) == 0;
            }
            if (!same_slot || !same_payload)
                probe->lifecycle_observed.store (1, std::memory_order_release);
        }

        probe->prev_payload.resize (size);
        if (size > 0 && data)
            memcpy (&probe->prev_payload[0], data, size);
        else if (size > 0)
            memset (&probe->prev_payload[0], 0, size);
        probe->prev_slot = msg;
        probe->have_prev_slot = true;

        const int send_rc = zlink_stream_send_msg (probe->socket, rid_, msg, 0);
        if (send_rc != static_cast<int> (size))
            probe->send_fail.fetch_add (1, std::memory_order_release);

        probe->packets.fetch_add (1, std::memory_order_release);
        release_stream_callback_msg (msg);
    }

    return 0;
}

static int send_len32be_frame (int fd_,
                               const unsigned char *payload_,
                               size_t size_)
{
    if (size_ > static_cast<size_t> (0xFFFFFFFFu)) {
        errno = EMSGSIZE;
        return -1;
    }
    if (!payload_ && size_ > 0) {
        errno = EINVAL;
        return -1;
    }

    std::vector<unsigned char> frame (4 + size_);
    store_u32_be (&frame[0], static_cast<uint32_t> (size_));
    if (size_ > 0)
        memcpy (&frame[4], payload_, size_);
    return send_stream_packet (fd_, &frame[0], frame.size ());
}

static void append_len32be_frame (std::vector<unsigned char> *out_,
                                  const unsigned char *payload_,
                                  size_t size_)
{
    if (!out_)
        return;

    const size_t offset = out_->size ();
    out_->resize (offset + 4 + size_);
    store_u32_be (&(*out_)[offset], static_cast<uint32_t> (size_));
    if (size_ > 0)
        memcpy (&(*out_)[offset + 4], payload_, size_);
}

static int recv_len32be_frame (int fd_, std::vector<unsigned char> *payload_out_)
{
    if (!payload_out_) {
        errno = EINVAL;
        return -1;
    }

    unsigned char header[4];
    if (recv_exact (fd_, header, sizeof (header)) != 0)
        return -1;

    const size_t payload_size = static_cast<size_t> (load_u32_be (header));
    payload_out_->resize (payload_size);
    if (payload_size > 0
        && recv_exact (fd_, &(*payload_out_)[0], payload_size) != 0) {
        return -1;
    }
    return 0;
}

void test_stream_len32be_single_frame ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (0, set_raw_fd_timeout (client_fd, 3000));

    const unsigned char payload[] = "len32be-single-frame";
    stream_len32be_probe_t probe;
    probe.socket = server;
    push_expected_len32be_packet (&probe, payload, sizeof (payload) - 1);
    probe.expected_batch_sizes.push_back (1);
    g_stream_len32be_probe = &probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_attach (
      server, stream_len32be_probe_callback, ZLINK_STREAM_DISPATCH_LEN32BE));

    TEST_ASSERT_EQUAL_INT (
      0, send_len32be_frame (client_fd, payload, sizeof (payload) - 1));

    std::vector<unsigned char> echoed;
    TEST_ASSERT_EQUAL_INT (0, recv_len32be_frame (client_fd, &echoed));
    TEST_ASSERT_EQUAL_UINT (
      static_cast<unsigned int> (sizeof (payload) - 1),
      static_cast<unsigned int> (echoed.size ()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      payload, &echoed[0], static_cast<unsigned int> (sizeof (payload) - 1));

    TEST_ASSERT_TRUE (wait_counter_at_least (&probe.packets, 1, 3000));
    TEST_ASSERT_EQUAL_INT (1, probe.callbacks.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (0, probe.bad_payload.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (0, probe.bad_batch.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (0, probe.send_fail.load (std::memory_order_acquire));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_stream_len32be_probe = NULL;
    close_raw_fd (client_fd);
    test_context_socket_close_zero_linger (server);
}

void test_stream_len32be_header_split ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (0, set_raw_fd_timeout (client_fd, 3000));

    const unsigned char payload[] = "len32be-header-split";
    std::vector<unsigned char> frame (4 + sizeof (payload) - 1);
    store_u32_be (&frame[0], sizeof (payload) - 1);
    memcpy (&frame[4], payload, sizeof (payload) - 1);

    stream_len32be_probe_t probe;
    probe.socket = server;
    push_expected_len32be_packet (&probe, payload, sizeof (payload) - 1);
    probe.expected_batch_sizes.push_back (1);
    g_stream_len32be_probe = &probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_attach (
      server, stream_len32be_probe_callback, ZLINK_STREAM_DISPATCH_LEN32BE));

    TEST_ASSERT_EQUAL_INT (0, send_stream_packet (client_fd, &frame[0], 2));
    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, &frame[2], frame.size () - 2));

    std::vector<unsigned char> echoed;
    TEST_ASSERT_EQUAL_INT (0, recv_len32be_frame (client_fd, &echoed));
    TEST_ASSERT_EQUAL_UINT (
      static_cast<unsigned int> (sizeof (payload) - 1),
      static_cast<unsigned int> (echoed.size ()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      payload, &echoed[0], static_cast<unsigned int> (sizeof (payload) - 1));

    TEST_ASSERT_TRUE (wait_counter_at_least (&probe.packets, 1, 3000));
    TEST_ASSERT_EQUAL_INT (1, probe.callbacks.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (0, probe.bad_payload.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (0, probe.bad_batch.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (0, probe.send_fail.load (std::memory_order_acquire));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_stream_len32be_probe = NULL;
    close_raw_fd (client_fd);
    test_context_socket_close_zero_linger (server);
}

void test_stream_len32be_body_split ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (0, set_raw_fd_timeout (client_fd, 3000));

    const unsigned char payload[] = "len32be-body-split-payload";
    std::vector<unsigned char> frame (4 + sizeof (payload) - 1);
    store_u32_be (&frame[0], sizeof (payload) - 1);
    memcpy (&frame[4], payload, sizeof (payload) - 1);

    stream_len32be_probe_t probe;
    probe.socket = server;
    push_expected_len32be_packet (&probe, payload, sizeof (payload) - 1);
    probe.expected_batch_sizes.push_back (1);
    g_stream_len32be_probe = &probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_attach (
      server, stream_len32be_probe_callback, ZLINK_STREAM_DISPATCH_LEN32BE));

    const size_t split = 4 + 5;
    TEST_ASSERT_EQUAL_INT (0, send_stream_packet (client_fd, &frame[0], split));
    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, &frame[split], frame.size () - split));

    std::vector<unsigned char> echoed;
    TEST_ASSERT_EQUAL_INT (0, recv_len32be_frame (client_fd, &echoed));
    TEST_ASSERT_EQUAL_UINT (
      static_cast<unsigned int> (sizeof (payload) - 1),
      static_cast<unsigned int> (echoed.size ()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      payload, &echoed[0], static_cast<unsigned int> (sizeof (payload) - 1));

    TEST_ASSERT_TRUE (wait_counter_at_least (&probe.packets, 1, 3000));
    TEST_ASSERT_EQUAL_INT (1, probe.callbacks.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (0, probe.bad_payload.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (0, probe.bad_batch.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (0, probe.send_fail.load (std::memory_order_acquire));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_stream_len32be_probe = NULL;
    close_raw_fd (client_fd);
    test_context_socket_close_zero_linger (server);
}

void test_stream_len32be_multi_frame_single_read ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (0, set_raw_fd_timeout (client_fd, 3000));

    const unsigned char payload1[] = "len32be-multi-1";
    const unsigned char payload2[] = "len32be-multi-2";
    const unsigned char payload3[] = "len32be-multi-3";

    stream_len32be_probe_t probe;
    probe.socket = server;
    push_expected_len32be_packet (&probe, payload1, sizeof (payload1) - 1);
    push_expected_len32be_packet (&probe, payload2, sizeof (payload2) - 1);
    push_expected_len32be_packet (&probe, payload3, sizeof (payload3) - 1);
    probe.expected_batch_sizes.push_back (3);
    g_stream_len32be_probe = &probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_attach (
      server, stream_len32be_probe_callback, ZLINK_STREAM_DISPATCH_LEN32BE));

    std::vector<unsigned char> merged_frames;
    append_len32be_frame (&merged_frames, payload1, sizeof (payload1) - 1);
    append_len32be_frame (&merged_frames, payload2, sizeof (payload2) - 1);
    append_len32be_frame (&merged_frames, payload3, sizeof (payload3) - 1);
    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, &merged_frames[0], merged_frames.size ()));

    std::vector<unsigned char> echoed;
    TEST_ASSERT_EQUAL_INT (0, recv_len32be_frame (client_fd, &echoed));
    TEST_ASSERT_EQUAL_UINT (
      static_cast<unsigned int> (sizeof (payload1) - 1),
      static_cast<unsigned int> (echoed.size ()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      payload1, &echoed[0], static_cast<unsigned int> (sizeof (payload1) - 1));
    TEST_ASSERT_EQUAL_INT (0, recv_len32be_frame (client_fd, &echoed));
    TEST_ASSERT_EQUAL_UINT (
      static_cast<unsigned int> (sizeof (payload2) - 1),
      static_cast<unsigned int> (echoed.size ()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      payload2, &echoed[0], static_cast<unsigned int> (sizeof (payload2) - 1));
    TEST_ASSERT_EQUAL_INT (0, recv_len32be_frame (client_fd, &echoed));
    TEST_ASSERT_EQUAL_UINT (
      static_cast<unsigned int> (sizeof (payload3) - 1),
      static_cast<unsigned int> (echoed.size ()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      payload3, &echoed[0], static_cast<unsigned int> (sizeof (payload3) - 1));

    TEST_ASSERT_TRUE (wait_counter_at_least (&probe.packets, 3, 3000));
    TEST_ASSERT_EQUAL_INT (1, probe.callbacks.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (0, probe.bad_payload.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (0, probe.bad_batch.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (0, probe.send_fail.load (std::memory_order_acquire));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_stream_len32be_probe = NULL;
    close_raw_fd (client_fd);
    test_context_socket_close_zero_linger (server);
}

void test_stream_recv_ready_precedes_first_payload_contract ()
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_ctx_set (get_test_context (), ZLINK_IO_THREADS, 8));

    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    configure_stream_regression_socket (server, 256);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    void *monitor = zlink_socket_monitor_open (
      server, ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
    TEST_ASSERT_NOT_NULL (monitor);
    const int monitor_linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (monitor, ZLINK_LINGER, &monitor_linger,
                        sizeof (monitor_linger)));

    stream_ordering_probe_t ordering_probe;

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (0, set_raw_fd_timeout (client_fd, 3000));

    const unsigned char payload[] = "stream-recv-ready-contract";
    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, payload, sizeof (payload) - 1));

    bool server_readable = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        zlink_pollitem_t items[] = {
          {monitor, 0, ZLINK_POLLIN, 0},
          {server, 0, ZLINK_POLLIN, 0},
        };
        const int prc = zlink_poll (items, 2, 20);
        if (prc < 0 && errno == EINTR)
            continue;
        if ((items[0].revents & ZLINK_POLLIN) != 0)
            collect_stream_ordering_monitor_events (monitor, &ordering_probe, 0);
        if ((items[1].revents & ZLINK_POLLIN) != 0) {
            server_readable = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE (
      server_readable, "STREAM recv test did not observe readable payload");

    unsigned char recv_id[stream_routing_id_size];
    unsigned char recv_buf[sizeof (payload)];
    const int rc =
      recv_stream_msg (server, recv_id, recv_buf, sizeof (recv_buf));
    TEST_ASSERT_EQUAL_INT (static_cast<int> (sizeof (payload) - 1), rc);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      payload, recv_buf, static_cast<unsigned int> (sizeof (payload) - 1));
    collect_stream_ordering_monitor_events (monitor, &ordering_probe, 0);

    zlink_routing_id_t rid;
    rid.size = static_cast<uint8_t> (stream_routing_id_size);
    memcpy (rid.data, recv_id, stream_routing_id_size);
    const bool ready = mark_stream_payload_begin (&ordering_probe, &rid, true);
    TEST_ASSERT_TRUE_MESSAGE (
      ready, "STREAM recv delivered first payload before CONNECTION_READY");

    send_stream_msg (server, recv_id, payload, sizeof (payload) - 1);
    mark_stream_payload_end (&ordering_probe, &rid);

    unsigned char echoed[sizeof (payload)];
    TEST_ASSERT_EQUAL_INT (
      static_cast<int> (sizeof (payload) - 1),
      recv_stream_packet (client_fd, echoed, sizeof (echoed)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      payload, echoed, static_cast<unsigned int> (sizeof (payload) - 1));

    close_raw_fd (client_fd);

    TEST_ASSERT_TRUE (
      wait_stream_ordering_sets_with_monitor (monitor, &ordering_probe, 1, 1,
                                              5000));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.payload_before_ready.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.disconnect_during_payload.load (
           std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      1, ordering_probe.strict_accept_count.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.strict_drop_count.load (std::memory_order_acquire));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (monitor));
    test_context_socket_close_zero_linger (server);
}

void test_stream_raw_callback_ready_precedes_first_payload_contract ()
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_ctx_set (get_test_context (), ZLINK_IO_THREADS, 8));

    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    configure_stream_regression_socket (server, 256);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    void *monitor = zlink_socket_monitor_open (
      server, ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
    TEST_ASSERT_NOT_NULL (monitor);
    const int monitor_linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (monitor, ZLINK_LINGER, &monitor_linger,
                        sizeof (monitor_linger)));

    stream_ordering_probe_t ordering_probe;

    stream_ordering_callback_probe_t callback_probe;
    callback_probe.socket = server;
    callback_probe.monitor = monitor;
    callback_probe.ordering = &ordering_probe;
    callback_probe.strict_gate = true;
    g_stream_raw_ordering_callback_probe = &callback_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_stream_attach_raw (server, stream_raw_ordering_echo_callback));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (0, set_raw_fd_timeout (client_fd, 3000));

    const unsigned char payload[] = "stream-raw-ready-contract";
    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, payload, sizeof (payload) - 1));

    unsigned char echoed[sizeof (payload)];
    TEST_ASSERT_EQUAL_INT (
      static_cast<int> (sizeof (payload) - 1),
      recv_stream_packet (client_fd, echoed, sizeof (echoed)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      payload, echoed, static_cast<unsigned int> (sizeof (payload) - 1));

    close_raw_fd (client_fd);

    TEST_ASSERT_TRUE (wait_stream_ordering_counter (
      &callback_probe.callback_payloads, 1, 5000));
    TEST_ASSERT_TRUE (
      wait_stream_ordering_sets_with_monitor (monitor, &ordering_probe, 1, 1,
                                              5000));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.payload_before_ready.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.disconnect_during_payload.load (
           std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      1, ordering_probe.strict_accept_count.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.strict_drop_count.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, callback_probe.send_fail.load (std::memory_order_acquire));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_stream_raw_ordering_callback_probe = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (monitor));
    test_context_socket_close_zero_linger (server);
}

void test_stream_len32be_callback_lifecycle_contract ()
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_ctx_set (get_test_context (), ZLINK_IO_THREADS, 8));

    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    configure_stream_regression_socket (server, 256);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    void *monitor = zlink_socket_monitor_open (
      server, ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
    TEST_ASSERT_NOT_NULL (monitor);
    const int monitor_linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (monitor, ZLINK_LINGER, &monitor_linger,
                        sizeof (monitor_linger)));

    stream_ordering_probe_t ordering_probe;

    stream_ordering_callback_probe_t callback_probe;
    callback_probe.socket = server;
    callback_probe.monitor = monitor;
    callback_probe.ordering = &ordering_probe;
    callback_probe.strict_gate = true;
    g_stream_len32be_ordering_callback_probe = &callback_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_stream_attach_len32be (server,
                                   stream_len32be_ordering_echo_callback));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);
    TEST_ASSERT_EQUAL_INT (0, set_raw_fd_timeout (client_fd, 3000));

    const unsigned char payload[] = "AuthenticateRequest";
    std::vector<unsigned char> frame (4 + sizeof (payload) - 1);
    store_u32_be (&frame[0], sizeof (payload) - 1);
    memcpy (&frame[4], payload, sizeof (payload) - 1);

    TEST_ASSERT_EQUAL_INT (0, send_stream_packet (client_fd, &frame[0], 3));
    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, &frame[3], frame.size () - 3));

    std::vector<unsigned char> echoed;
    TEST_ASSERT_EQUAL_INT (0, recv_len32be_frame (client_fd, &echoed));
    TEST_ASSERT_EQUAL_UINT (
      static_cast<unsigned int> (sizeof (payload) - 1),
      static_cast<unsigned int> (echoed.size ()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      payload, &echoed[0], static_cast<unsigned int> (sizeof (payload) - 1));

    close_raw_fd (client_fd);

    TEST_ASSERT_TRUE (wait_stream_ordering_counter (
      &callback_probe.callback_payloads, 1, 5000));
    TEST_ASSERT_TRUE (
      wait_stream_ordering_sets_with_monitor (monitor, &ordering_probe, 1, 1,
                                              5000));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.payload_before_ready.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.disconnect_during_payload.load (
           std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      1, ordering_probe.strict_accept_count.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.strict_drop_count.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, callback_probe.send_fail.load (std::memory_order_acquire));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_stream_len32be_ordering_callback_probe = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (monitor));
    test_context_socket_close_zero_linger (server);
}

static void run_stream_len32be_client_load (const char *endpoint_,
                                            uint32_t client_id_,
                                            int phases_,
                                            int messages_per_phase_,
                                            size_t payload_size_,
                                            std::atomic<int> *client_failures_)
{
    if (!endpoint_ || !client_failures_)
        return;

    std::vector<unsigned char> payload (payload_size_);
    std::vector<unsigned char> send_frame (payload_size_ + 4);
    std::vector<unsigned char> recv_frame (payload_size_ + 4);

    for (int phase = 0; phase < phases_; ++phase) {
        const int fd = connect_raw_tcp (endpoint_);
        if (fd < 0) {
            client_failures_->fetch_add (1, std::memory_order_release);
            return;
        }
        if (set_raw_fd_timeout (fd, 5000) != 0) {
            close_raw_fd (fd);
            client_failures_->fetch_add (1, std::memory_order_release);
            return;
        }

        for (int i = 0; i < messages_per_phase_; ++i) {
            const uint32_t seq =
              static_cast<uint32_t> (phase * messages_per_phase_ + i);

            store_u32_be (&payload[0], client_id_);
            store_u32_be (&payload[4], seq);
            for (size_t j = 8; j < payload_size_; ++j)
                payload[j] = static_cast<unsigned char> ((client_id_ + seq + j) & 0xFF);

            store_u32_be (&send_frame[0], static_cast<uint32_t> (payload_size_));
            memcpy (&send_frame[4], &payload[0], payload_size_);

            if (send_stream_packet (fd, &send_frame[0], send_frame.size ()) != 0
                || recv_exact (fd, &recv_frame[0], recv_frame.size ()) != 0
                || load_u32_be (&recv_frame[0]) != payload_size_
                || memcmp (&recv_frame[4], &payload[0], payload_size_) != 0) {
                client_failures_->fetch_add (1, std::memory_order_release);
                break;
            }
        }

        close_raw_fd (fd);
        if (client_failures_->load (std::memory_order_acquire) > 0)
            return;
    }
}

static void run_stream_raw_client_load (const char *endpoint_,
                                        uint32_t client_id_,
                                        int phases_,
                                        int messages_per_phase_,
                                        size_t payload_size_,
                                        std::atomic<int> *client_failures_)
{
    if (!endpoint_ || !client_failures_)
        return;

    std::vector<unsigned char> payload (payload_size_);
    std::vector<unsigned char> frame (payload_size_ + 4);
    std::vector<unsigned char> recv_frame (payload_size_ + 4);

    for (int phase = 0; phase < phases_; ++phase) {
        const int fd = connect_raw_tcp (endpoint_);
        if (fd < 0) {
            client_failures_->fetch_add (1, std::memory_order_release);
            return;
        }
        if (set_raw_fd_timeout (fd, 5000) != 0) {
            close_raw_fd (fd);
            client_failures_->fetch_add (1, std::memory_order_release);
            return;
        }

        for (int i = 0; i < messages_per_phase_; ++i) {
            const uint32_t seq =
              static_cast<uint32_t> (phase * messages_per_phase_ + i);
            store_u32_be (&payload[0], client_id_);
            store_u32_be (&payload[4], seq);
            for (size_t j = 8; j < payload_size_; ++j)
                payload[j] = static_cast<unsigned char> ((client_id_ + seq + j) & 0xFF);

            store_u32_be (&frame[0], static_cast<uint32_t> (payload_size_));
            memcpy (&frame[4], &payload[0], payload_size_);

            const size_t split =
              1 + ((static_cast<size_t> (client_id_) + seq) % (frame.size () - 1));
            if (send_stream_packet (fd, &frame[0], split) != 0
                || send_stream_packet (fd, &frame[split], frame.size () - split) != 0
                || recv_exact (fd, &recv_frame[0], recv_frame.size ()) != 0
                || load_u32_be (&recv_frame[0]) != payload_size_
                || memcmp (&recv_frame[4], &payload[0], payload_size_) != 0) {
                client_failures_->fetch_add (1, std::memory_order_release);
                break;
            }
        }

        close_raw_fd (fd);
        if (client_failures_->load (std::memory_order_acquire) > 0)
            return;
    }
}

struct stream_recv_strict_worker_t
{
    stream_recv_strict_worker_t () :
        socket (NULL),
        monitor (NULL),
        expected_messages (0),
        processed_messages (0),
        send_fail (0),
        ordering (NULL)
    {
    }

    void *socket;
    void *monitor;
    int expected_messages;
    std::atomic<int> processed_messages;
    std::atomic<int> send_fail;
    stream_ordering_probe_t *ordering;
};

static void run_stream_recv_strict_worker (stream_recv_strict_worker_t *worker_)
{
    if (!worker_ || !worker_->socket || !worker_->ordering)
        return;

    unsigned char routing_id[stream_routing_id_size];
    unsigned char buf[512];
    const int deadline_ms = 10000;
    const int slice_ms = 10;
    const int loops = deadline_ms / slice_ms;

    for (int i = 0; i < loops; ++i) {
        if (worker_->processed_messages.load (std::memory_order_acquire)
            >= worker_->expected_messages) {
            return;
        }

        zlink_pollitem_t items[] = {
          {worker_->monitor, 0, ZLINK_POLLIN, 0},
          {worker_->socket, 0, ZLINK_POLLIN, 0},
        };
        const int prc = zlink_poll (items, 2, slice_ms);
        if (prc < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        if ((items[0].revents & ZLINK_POLLIN) != 0)
            collect_stream_ordering_monitor_events (worker_->monitor,
                                                    worker_->ordering, 0);
        if (prc == 0 || (items[1].revents & ZLINK_POLLIN) == 0)
            continue;

        const int rc =
          recv_stream_msg (worker_->socket, routing_id, buf, sizeof (buf));
        if (rc <= 0)
            continue;
        collect_stream_ordering_monitor_events (worker_->monitor,
                                                worker_->ordering, 0);

        zlink_routing_id_t rid;
        rid.size = static_cast<uint8_t> (stream_routing_id_size);
        memcpy (rid.data, routing_id, stream_routing_id_size);
        const bool ready =
          mark_stream_payload_begin (worker_->ordering, &rid, true);
        if (ready) {
            const int send_rc =
              zlink_stream_send (worker_->socket, &rid, buf, static_cast<size_t> (rc), 0);
            if (send_rc != rc)
                worker_->send_fail.fetch_add (1, std::memory_order_release);
        }
        mark_stream_payload_end (worker_->ordering, &rid);
        worker_->processed_messages.fetch_add (1, std::memory_order_release);
    }
}

static void run_stream_len32be_client_once (const char *endpoint_,
                                            uint32_t client_id_,
                                            size_t payload_size_,
                                            std::atomic<int> *client_failures_)
{
    run_stream_len32be_client_load (endpoint_, client_id_, 1, 1, payload_size_,
                                    client_failures_);
}

static void run_stream_raw_client_once (const char *endpoint_,
                                        uint32_t client_id_,
                                        size_t payload_size_,
                                        std::atomic<int> *client_failures_)
{
    run_stream_raw_client_load (endpoint_, client_id_, 1, 1, payload_size_,
                                client_failures_);
}

void test_stream_monitor_len32be_multiclient_connect_disconnect ()
{
    const int client_count = 6;
    const size_t payload_size = 32;
    const int expected_msgs = client_count;

    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    void *monitor = zlink_socket_monitor_open (server, ZLINK_EVENT_ALL);
    TEST_ASSERT_NOT_NULL (monitor);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (monitor, ZLINK_LINGER, &zero, sizeof (zero)));

    stream_monitor_callback_probe_t callback_probe;
    callback_probe.socket = server;
    callback_probe.expected_payload_size = payload_size;
    g_stream_monitor_callback_probe = &callback_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_attach (
      server, stream_monitor_len32be_echo_callback,
      ZLINK_STREAM_DISPATCH_LEN32BE));

    stream_monitor_probe_t monitor_probe;
    std::vector<unsigned char> payload (payload_size);

    for (int i = 0; i < client_count; ++i) {
        const int expected = i + 1;
        const int fd = connect_raw_tcp (endpoint);
        TEST_ASSERT_TRUE (fd >= 0);
        TEST_ASSERT_EQUAL_INT (0, set_raw_fd_timeout (fd, 5000));

        // In callback mode, ACCEPTED must be observed for each new peer.
        TEST_ASSERT_TRUE (wait_stream_monitor_progress (
          monitor, &monitor_probe, expected, i, i, 5000));

        fill_monitor_test_payload (&payload, static_cast<uint32_t> (expected));
        TEST_ASSERT_EQUAL_INT (
          0, send_len32be_frame (fd, &payload[0], payload.size ()));

        std::vector<unsigned char> echoed;
        TEST_ASSERT_EQUAL_INT (0, recv_len32be_frame (fd, &echoed));
        TEST_ASSERT_EQUAL_UINT (static_cast<unsigned int> (payload.size ()),
                                static_cast<unsigned int> (echoed.size ()));
        TEST_ASSERT_EQUAL_UINT8_ARRAY (&payload[0], &echoed[0],
                                       static_cast<unsigned int> (payload.size ()));

        // CONNECTION_READY must already be observable before first payload
        // dispatch is allowed to complete for that peer.
        TEST_ASSERT_TRUE (wait_stream_monitor_progress (
          monitor, &monitor_probe, expected, expected, i, 5000));

        close_raw_fd (fd);

        TEST_ASSERT_TRUE (wait_stream_monitor_progress (
          monitor, &monitor_probe, expected, expected, expected, 5000));
    }

    TEST_ASSERT_TRUE (
      wait_counter_at_least (&callback_probe.echoed_msgs, expected_msgs, 5000));
    TEST_ASSERT_EQUAL_INT (
      0, callback_probe.bad_payload.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, callback_probe.send_fail.load (std::memory_order_acquire));

    int callback_routing_id_count = 0;
    bool callback_routing_ids_match = true;
    {
        std::lock_guard<std::mutex> lk (callback_probe.routing_id_mu);
        callback_routing_id_count =
          static_cast<int> (callback_probe.routing_ids.size ());
        callback_routing_ids_match = callback_routing_id_count == client_count;

        for (std::set<std::string>::const_iterator
               it = callback_probe.routing_ids.begin ();
             it != callback_probe.routing_ids.end (); ++it) {
            if (monitor_probe.ready_routing_ids.find (*it)
                  == monitor_probe.ready_routing_ids.end ()
                || monitor_probe.disconnected_routing_ids.find (*it)
                     == monitor_probe.disconnected_routing_ids.end ()) {
                callback_routing_ids_match = false;
                break;
            }
        }
    }

    char monitor_summary[256];
    snprintf (
      monitor_summary, sizeof (monitor_summary),
      "monitor events mismatch: accepted=%d ready=%d(discrete=%d bad_rid=%d) disconnected=%d(discrete=%d bad_rid=%d)",
      monitor_probe.accepted, monitor_probe.connection_ready,
      static_cast<int> (monitor_probe.ready_routing_ids.size ()),
      monitor_probe.bad_ready_routing_id, monitor_probe.disconnected,
      static_cast<int> (monitor_probe.disconnected_routing_ids.size ()),
      monitor_probe.bad_disconnected_routing_id);

    TEST_ASSERT_TRUE_MESSAGE (wait_stream_monitor_progress (
                                monitor, &monitor_probe, client_count,
                                client_count, client_count, 1000),
                              monitor_summary);
    TEST_ASSERT_TRUE (monitor_probe.accepted >= client_count);
    TEST_ASSERT_TRUE (monitor_probe.connection_ready >= client_count);
    TEST_ASSERT_TRUE (monitor_probe.disconnected >= client_count);
    TEST_ASSERT_EQUAL_INT (0, monitor_probe.bad_ready_routing_id);
    TEST_ASSERT_EQUAL_INT (0, monitor_probe.bad_disconnected_routing_id);
    TEST_ASSERT_EQUAL_INT (
      client_count, static_cast<int> (monitor_probe.ready_routing_ids.size ()));
    TEST_ASSERT_EQUAL_INT (
      client_count,
      static_cast<int> (monitor_probe.disconnected_routing_ids.size ()));
    TEST_ASSERT_TRUE_MESSAGE (
      callback_routing_ids_match,
      "callback routing_ids do not fully match monitor ready/disconnected ids");
    TEST_ASSERT_EQUAL_INT (client_count, callback_routing_id_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_stream_monitor_callback_probe = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (monitor));
    test_context_socket_close_zero_linger (server);
}

void test_stream_raw_multiclient_strict_ready_gating_regression ()
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_ctx_set (get_test_context (), ZLINK_IO_THREADS, 8));

    const int client_count = 64;
    const size_t payload_size = 32;

    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    configure_stream_regression_socket (server, 2048);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    void *monitor = zlink_socket_monitor_open (
      server, ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
    TEST_ASSERT_NOT_NULL (monitor);
    const int monitor_linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (monitor, ZLINK_LINGER, &monitor_linger,
                        sizeof (monitor_linger)));

    stream_ordering_probe_t ordering_probe;

    stream_ordering_callback_probe_t callback_probe;
    callback_probe.socket = server;
    callback_probe.monitor = monitor;
    callback_probe.ordering = &ordering_probe;
    callback_probe.strict_gate = true;
    g_stream_raw_ordering_callback_probe = &callback_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_stream_attach_raw (server, stream_raw_ordering_echo_callback));

    std::atomic<int> client_failures (0);
    std::vector<std::thread> clients;
    clients.reserve (client_count);
    for (int i = 0; i < client_count; ++i) {
        clients.push_back (
          std::thread (run_stream_raw_client_once, endpoint,
                       static_cast<uint32_t> (i), payload_size,
                       &client_failures));
    }
    for (size_t i = 0; i < clients.size (); ++i)
        clients[i].join ();

    TEST_ASSERT_TRUE (wait_stream_ordering_counter (
      &callback_probe.callback_payloads, client_count, 10000));
    TEST_ASSERT_TRUE (
      wait_stream_ordering_sets_with_monitor (monitor, &ordering_probe,
                                              client_count, client_count,
                                              10000));
    TEST_ASSERT_EQUAL_INT (0, client_failures.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.payload_before_ready.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.disconnect_during_payload.load (
           std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.strict_drop_count.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      client_count,
      ordering_probe.strict_accept_count.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, callback_probe.send_fail.load (std::memory_order_acquire));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_stream_raw_ordering_callback_probe = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (monitor));
    test_context_socket_close_zero_linger (server);
}

void test_stream_len32be_multiclient_strict_ready_gating_regression ()
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_ctx_set (get_test_context (), ZLINK_IO_THREADS, 8));

    const int client_count = 64;
    const size_t payload_size = 32;

    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    configure_stream_regression_socket (server, 2048);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    void *monitor = zlink_socket_monitor_open (
      server, ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
    TEST_ASSERT_NOT_NULL (monitor);
    const int monitor_linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (monitor, ZLINK_LINGER, &monitor_linger,
                        sizeof (monitor_linger)));

    stream_ordering_probe_t ordering_probe;

    stream_ordering_callback_probe_t callback_probe;
    callback_probe.socket = server;
    callback_probe.monitor = monitor;
    callback_probe.ordering = &ordering_probe;
    callback_probe.strict_gate = true;
    g_stream_len32be_ordering_callback_probe = &callback_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_stream_attach_len32be (server,
                                   stream_len32be_ordering_echo_callback));

    std::atomic<int> client_failures (0);
    std::vector<std::thread> clients;
    clients.reserve (client_count);
    for (int i = 0; i < client_count; ++i) {
        clients.push_back (
          std::thread (run_stream_len32be_client_once, endpoint,
                       static_cast<uint32_t> (i), payload_size,
                       &client_failures));
    }
    for (size_t i = 0; i < clients.size (); ++i)
        clients[i].join ();

    TEST_ASSERT_TRUE (wait_stream_ordering_counter (
      &callback_probe.callback_payloads, client_count, 10000));
    TEST_ASSERT_TRUE (
      wait_stream_ordering_sets_with_monitor (monitor, &ordering_probe,
                                              client_count, client_count,
                                              10000));
    TEST_ASSERT_EQUAL_INT (0, client_failures.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.payload_before_ready.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.disconnect_during_payload.load (
           std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.strict_drop_count.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      client_count,
      ordering_probe.strict_accept_count.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, callback_probe.send_fail.load (std::memory_order_acquire));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_stream_len32be_ordering_callback_probe = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (monitor));
    test_context_socket_close_zero_linger (server);
}

void test_stream_recv_multiclient_strict_ready_gating_regression ()
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_ctx_set (get_test_context (), ZLINK_IO_THREADS, 8));

    const int client_count = 64;
    const size_t payload_size = 32;

    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    configure_stream_regression_socket (server, 2048);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    void *monitor = zlink_socket_monitor_open (
      server, ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
    TEST_ASSERT_NOT_NULL (monitor);
    const int monitor_linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (monitor, ZLINK_LINGER, &monitor_linger,
                        sizeof (monitor_linger)));

    stream_ordering_probe_t ordering_probe;

    stream_recv_strict_worker_t worker;
    worker.socket = server;
    worker.monitor = monitor;
    worker.expected_messages = client_count;
    worker.ordering = &ordering_probe;
    std::thread worker_thread (run_stream_recv_strict_worker, &worker);

    std::atomic<int> client_failures (0);
    std::vector<std::thread> clients;
    clients.reserve (client_count);
    for (int i = 0; i < client_count; ++i) {
        clients.push_back (
          std::thread (run_stream_raw_client_once, endpoint,
                       static_cast<uint32_t> (i), payload_size,
                       &client_failures));
    }
    for (size_t i = 0; i < clients.size (); ++i)
        clients[i].join ();
    worker_thread.join ();

    TEST_ASSERT_TRUE (wait_stream_ordering_counter (
      &worker.processed_messages, client_count, 10000));
    TEST_ASSERT_TRUE (
      wait_stream_ordering_sets_with_monitor (monitor, &ordering_probe,
                                              client_count, client_count,
                                              10000));
    TEST_ASSERT_EQUAL_INT (0, client_failures.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.payload_before_ready.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.disconnect_during_payload.load (
           std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      0, ordering_probe.strict_drop_count.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (
      client_count,
      ordering_probe.strict_accept_count.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (0, worker.send_fail.load (std::memory_order_acquire));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (monitor));
    test_context_socket_close_zero_linger (server);
}

void test_stream_raw_multiclient_load_integrity ()
{
    const int client_count = 48;
    const int phases = 2;
    const int messages_per_phase = 48;
    const size_t payload_size = 192;
    const int expected_msgs = client_count * phases * messages_per_phase;

    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    stream_raw_load_probe_t probe;
    probe.socket = server;
    probe.expected_payload_size = payload_size;
    g_stream_raw_load_probe = &probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_stream_attach_raw (server, stream_raw_load_echo_raw_callback));

    std::atomic<int> client_failures (0);
    std::vector<std::thread> clients;
    clients.reserve (client_count);
    for (int i = 0; i < client_count; ++i) {
        clients.push_back (
          std::thread (run_stream_raw_client_load, endpoint,
                       static_cast<uint32_t> (i), phases, messages_per_phase,
                       payload_size, &client_failures));
    }
    for (size_t i = 0; i < clients.size (); ++i)
        clients[i].join ();

    TEST_ASSERT_EQUAL_INT (0, client_failures.load (std::memory_order_acquire));
    TEST_ASSERT_TRUE (
      wait_counter_at_least (&probe.echoed_msgs, expected_msgs, 10000));
    TEST_ASSERT_EQUAL_INT (
      0, probe.parse_error.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (0, probe.send_fail.load (std::memory_order_acquire));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_stream_raw_load_probe = NULL;
    test_context_socket_close_zero_linger (server);
}

void test_stream_len32be_multiclient_load_integrity ()
{
    const int client_count = 64;
    const int phases = 2;
    const int messages_per_phase = 64;
    const size_t payload_size = 256;
    const int expected_msgs = client_count * phases * messages_per_phase;

    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    stream_load_probe_t probe;
    probe.socket = server;
    probe.expected_payload_size = payload_size;
    g_stream_load_probe = &probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_attach (
      server, stream_load_echo_callback, ZLINK_STREAM_DISPATCH_LEN32BE));

    std::atomic<int> client_failures (0);
    std::vector<std::thread> clients;
    clients.reserve (client_count);
    for (int i = 0; i < client_count; ++i) {
        clients.push_back (
          std::thread (run_stream_len32be_client_load, endpoint,
                       static_cast<uint32_t> (i), phases, messages_per_phase,
                       payload_size, &client_failures));
    }
    for (size_t i = 0; i < clients.size (); ++i)
        clients[i].join ();

    TEST_ASSERT_EQUAL_INT (0, client_failures.load (std::memory_order_acquire));
    TEST_ASSERT_TRUE (wait_counter_at_least (&probe.echoed_msgs, expected_msgs, 10000));
    TEST_ASSERT_EQUAL_INT (
      0, probe.invalid_payload.load (std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (0, probe.send_fail.load (std::memory_order_acquire));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_detach (server));
    g_stream_load_probe = NULL;
    test_context_socket_close_zero_linger (server);
}
#endif

void test_stream_tcp_basic ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    void *monitor = zlink_socket_monitor_open (
      server, ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
    TEST_ASSERT_NOT_NULL (monitor);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (monitor, ZLINK_LINGER, &zero, sizeof (zero)));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);

    const char payload[] = "hello";
    TEST_ASSERT_EQUAL_INT (0, send_stream_packet (
                                client_fd, payload, sizeof (payload) - 1));

    unsigned char recv_id[stream_routing_id_size];
    char recv_buf[64];
    int rc = recv_stream_msg (server, recv_id, recv_buf, sizeof (recv_buf));
    TEST_ASSERT_EQUAL_INT ((int) sizeof (payload) - 1, rc);
    TEST_ASSERT_EQUAL_STRING_LEN (payload, recv_buf, sizeof (payload) - 1);

    unsigned char server_id[stream_routing_id_size];
    TEST_ASSERT_TRUE (wait_monitor_event (
      monitor, server, ZLINK_EVENT_CONNECTION_READY, server_id, 2000));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (server_id, recv_id, stream_routing_id_size);

    const char reply[] = "world";
    send_stream_msg (server, recv_id, reply, sizeof (reply) - 1);

    char client_recv_buf[64];
    rc = recv_stream_packet (client_fd, client_recv_buf, sizeof (client_recv_buf));
    TEST_ASSERT_EQUAL_INT ((int) sizeof (reply) - 1, rc);
    TEST_ASSERT_EQUAL_STRING_LEN (reply, client_recv_buf, sizeof (reply) - 1);

    close_raw_fd (client_fd);

    TEST_ASSERT_TRUE (wait_monitor_event (
      monitor, server, ZLINK_EVENT_DISCONNECTED, recv_id, 10000));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (server_id, recv_id, stream_routing_id_size);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (monitor));
    test_context_socket_close_zero_linger (server);
}

void test_stream_maxmsgsize ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    const int64_t maxmsgsize = 4;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_MAXMSGSIZE, &maxmsgsize, sizeof (maxmsgsize)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    void *monitor = zlink_socket_monitor_open (
      server, ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
    TEST_ASSERT_NOT_NULL (monitor);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (monitor, ZLINK_LINGER, &zero, sizeof (zero)));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);

    const char probe_payload[] = "ok";
    TEST_ASSERT_EQUAL_INT (0, send_stream_packet (
                                client_fd, probe_payload,
                                sizeof (probe_payload) - 1));

    unsigned char server_id[stream_routing_id_size];
    char probe_recv_buf[16];
    int rc = recv_stream_msg (server, server_id, probe_recv_buf,
                              sizeof (probe_recv_buf));
    TEST_ASSERT_EQUAL_INT ((int) sizeof (probe_payload) - 1, rc);
    TEST_ASSERT_EQUAL_STRING_LEN (probe_payload, probe_recv_buf,
                                  sizeof (probe_payload) - 1);

    unsigned char connect_id[stream_routing_id_size];
    TEST_ASSERT_TRUE (wait_monitor_event (
      monitor, server, ZLINK_EVENT_CONNECTION_READY, connect_id, 2000));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (connect_id, server_id, stream_routing_id_size);

    char payload[1024];
    memset (payload, 'A', sizeof (payload));
    TEST_ASSERT_EQUAL_INT (0, send_stream_packet (
                                client_fd, payload, sizeof (payload)));

    unsigned char recv_id[stream_routing_id_size];
    TEST_ASSERT_TRUE (wait_monitor_event (
      monitor, server, ZLINK_EVENT_DISCONNECTED, recv_id, 10000));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (server_id, recv_id, stream_routing_id_size);

    close_raw_fd (client_fd);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (monitor));
    test_context_socket_close_zero_linger (server);
}

void test_stream_connect_rejected ()
{
    void *stream = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (stream);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (stream, ZLINK_LINGER, &zero, sizeof (zero)));

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_connect (stream, "tcp://127.0.0.1:5555"));
    TEST_ASSERT_EQUAL_INT (EOPNOTSUPP, errno);

    test_context_socket_close_zero_linger (stream);
}

#if defined ZLINK_HAVE_WS
void test_stream_ws_basic ()
{
    TEST_IGNORE_MESSAGE ("STREAM client path removed; raw websocket client not in this unit test");
}

#if defined ZLINK_HAVE_WSS
void test_stream_wss_basic ()
{
    TEST_IGNORE_MESSAGE ("STREAM client path removed; raw websocket client not in this unit test");
}
#endif  // ZLINK_HAVE_WSS
#endif  // ZLINK_HAVE_WS

int main (void)
{
    UNITY_BEGIN ();

    setup_test_environment ();

    RUN_TEST (test_stream_callback_lifecycle);
    RUN_TEST (test_stream_recv_api_dispatch_conflict);
    RUN_TEST (test_stream_dispatch_start_rejects_stream_notify);
    RUN_TEST (test_stream_callback_echo_raw);
    RUN_TEST (test_stream_callback_echo_len32be);
    RUN_TEST (test_stream_callback_echo_single_zero_byte);
    RUN_TEST (test_stream_len32be_single_frame);
    RUN_TEST (test_stream_len32be_header_split);
    RUN_TEST (test_stream_len32be_body_split);
    RUN_TEST (test_stream_len32be_multi_frame_single_read);
    RUN_TEST (test_stream_recv_ready_precedes_first_payload_contract);
    RUN_TEST (test_stream_raw_callback_ready_precedes_first_payload_contract);
    RUN_TEST (test_stream_len32be_callback_lifecycle_contract);
#if !defined(ZLINK_HAVE_WINDOWS)
    RUN_TEST (test_stream_monitor_len32be_multiclient_connect_disconnect);
    RUN_TEST (test_stream_raw_multiclient_strict_ready_gating_regression);
    RUN_TEST (test_stream_len32be_multiclient_strict_ready_gating_regression);
    RUN_TEST (test_stream_recv_multiclient_strict_ready_gating_regression);
    RUN_TEST (test_stream_raw_multiclient_load_integrity);
    RUN_TEST (test_stream_len32be_multiclient_load_integrity);
#endif
    RUN_TEST (test_stream_connect_rejected);

#if defined ZLINK_HAVE_WS
    RUN_TEST (test_stream_ws_basic);
#if defined ZLINK_HAVE_WSS
    RUN_TEST (test_stream_wss_basic);
#endif
#else
    TEST_IGNORE_MESSAGE ("WebSocket support not enabled");
#endif

    return UNITY_END ();
}
