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
        send_errno (0)
    {
    }

    void *socket;
    const unsigned char *expected_payload;
    size_t expected_size;
    std::atomic<int> calls;
    std::atomic<int> matched;
    std::atomic<int> send_ok;
    std::atomic<int> send_errno;
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
    return size_ == 0
           || (size_ == 1
               && data_
               && (data_[0] == static_cast<unsigned char> (0x00)
                   || data_[0] == static_cast<unsigned char> (0x01)));
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
        if (is_stream_control_chunk (data, size))
            continue;

        if (!probe->expected_payload || size != probe->expected_size)
            continue;

        if (memcmp (data, probe->expected_payload, size) != 0)
            continue;

        const int send_rc =
          zlink_stream_send (probe->socket, rid_, data, size, 0);
        if (send_rc == static_cast<int> (size))
            probe->send_ok.store (1, std::memory_order_release);
        else
            probe->send_errno.store (errno, std::memory_order_release);

        probe->matched.fetch_add (1, std::memory_order_release);
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
            continue;
        }

        if (!data || size < 8
            || (probe->expected_payload_size > 0
                && size != probe->expected_payload_size)) {
            probe->invalid_payload.fetch_add (1, std::memory_order_release);
            continue;
        }

        const int send_rc = zlink_stream_send (probe->socket, rid_, data, size, 0);
        if (send_rc != static_cast<int> (size)) {
            probe->send_fail.fetch_add (1, std::memory_order_release);
            continue;
        }

        probe->echoed_msgs.fetch_add (1, std::memory_order_release);
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
            continue;
        }

        if ((!payload && payload_size > 0) || payload_size == 0) {
            probe->parse_error.fetch_add (1, std::memory_order_release);
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
    }

    return 0;
}

void test_stream_callback_lifecycle ()
{
    void *pair = test_context_socket (ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (pair);

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1,
                           zlink_stream_start (pair, stream_echo_callback, 0));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    test_context_socket_close_zero_linger (pair);

    void *stream = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (stream);

    stream_callback_probe_t probe;
    probe.socket = stream;

    g_stream_callback_probe = &probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_stream_start (stream, stream_echo_callback, 0));
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1,
                           zlink_stream_start (stream, stream_echo_callback, 0));
    TEST_ASSERT_EQUAL_INT (EBUSY, errno);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_stop (stream));
    g_stream_callback_probe = NULL;
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_stream_stop (stream));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    test_context_socket_close_zero_linger (stream);
}

void test_stream_recv_api_not_supported ()
{
    void *stream = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (stream);

    unsigned char buf[16];
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_recv (stream, buf, sizeof (buf),
                                           ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, errno);

    zlink_msg_t msg;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&msg));
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_msg_recv (&msg, stream, ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, errno);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&msg));

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
      zlink_stream_start (server, stream_echo_callback, 0));
    TEST_ASSERT_EQUAL_INT (0, send_stream_packet (
                                client_fd, payload, sizeof (payload) - 1));

    unsigned char recv_buf[sizeof (payload)];
    TEST_ASSERT_EQUAL_INT (0, recv_exact (client_fd, recv_buf,
                                          sizeof (payload) - 1));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      payload, recv_buf, static_cast<unsigned int> (sizeof (payload) - 1));

    TEST_ASSERT_TRUE (wait_counter_at_least (&probe.matched, 1, 3000));
    TEST_ASSERT_EQUAL_INT (1, probe.send_ok.load (std::memory_order_acquire));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_stop (server));
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
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_start (
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
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_stop (server));
    g_stream_callback_probe = NULL;

    close_raw_fd (client_fd);
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
      zlink_stream_start (server, stream_raw_load_echo_callback, 0));

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

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_stop (server));
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
    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_start (
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

    TEST_ASSERT_SUCCESS_ERRNO (zlink_stream_stop (server));
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

void test_stream_tcp_connect_basic ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    void *client = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (client);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client, ZLINK_LINGER, &zero, sizeof (zero)));

    const int io_timeout_ms = 5000;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      server, ZLINK_RCVTIMEO, &io_timeout_ms, sizeof (io_timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      server, ZLINK_SNDTIMEO, &io_timeout_ms, sizeof (io_timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      client, ZLINK_RCVTIMEO, &io_timeout_ms, sizeof (io_timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      client, ZLINK_SNDTIMEO, &io_timeout_ms, sizeof (io_timeout_ms)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    void *client_monitor = zlink_socket_monitor_open (
      client, ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
    TEST_ASSERT_NOT_NULL (client_monitor);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client_monitor, ZLINK_LINGER, &zero, sizeof (zero)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    unsigned char client_peer_id[stream_routing_id_size];
    TEST_ASSERT_TRUE (wait_monitor_event (
      client_monitor, client, ZLINK_EVENT_CONNECTION_READY, client_peer_id, 5000));

    const char req_payload[] = "hello";
    send_stream_msg (client, client_peer_id, req_payload, sizeof (req_payload) - 1);

    unsigned char server_peer_id[stream_routing_id_size];
    char server_recv_buf[64];
    int rc = recv_stream_msg (
      server, server_peer_id, server_recv_buf, sizeof (server_recv_buf));
    TEST_ASSERT_EQUAL_INT ((int) sizeof (req_payload) - 1, rc);
    TEST_ASSERT_EQUAL_STRING_LEN (
      req_payload, server_recv_buf, sizeof (req_payload) - 1);

    const char resp_payload[] = "world";
    send_stream_msg (
      server, server_peer_id, resp_payload, sizeof (resp_payload) - 1);

    unsigned char client_recv_id[stream_routing_id_size];
    char client_recv_buf[64];
    rc = recv_stream_msg (
      client, client_recv_id, client_recv_buf, sizeof (client_recv_buf));
    TEST_ASSERT_EQUAL_INT ((int) sizeof (resp_payload) - 1, rc);
    TEST_ASSERT_EQUAL_STRING_LEN (
      resp_payload, client_recv_buf, sizeof (resp_payload) - 1);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      client_peer_id, client_recv_id, stream_routing_id_size);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (client_monitor));
    test_context_socket_close_zero_linger (server);
    test_context_socket_close_zero_linger (client);
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
    RUN_TEST (test_stream_recv_api_not_supported);
    RUN_TEST (test_stream_callback_echo_raw);
    RUN_TEST (test_stream_callback_echo_len32be);
#if !defined(ZLINK_HAVE_WINDOWS)
    RUN_TEST (test_stream_raw_multiclient_load_integrity);
    RUN_TEST (test_stream_len32be_multiclient_load_integrity);
#endif

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
