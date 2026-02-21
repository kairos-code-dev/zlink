/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include "utils/config.hpp"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#if defined(ZLINK_HAVE_WINDOWS)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
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

static void configure_stream_len32be (void *socket_,
                                      int packet_max_size_,
                                      int packet_buffer_max_)
{
    const int mode = ZLINK_STREAM_PACKET_MODE_LEN32BE;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      socket_, ZLINK_STREAM_PACKET_MODE, &mode, sizeof (mode)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      socket_, ZLINK_STREAM_PACKET_MAX_SIZE, &packet_max_size_,
      sizeof (packet_max_size_)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      socket_, ZLINK_STREAM_PACKET_BUFFER_MAX, &packet_buffer_max_,
      sizeof (packet_buffer_max_)));
}

static void write_u32_be (unsigned char out_[4], uint32_t value_)
{
    out_[0] = static_cast<unsigned char> ((value_ >> 24) & 0xFFu);
    out_[1] = static_cast<unsigned char> ((value_ >> 16) & 0xFFu);
    out_[2] = static_cast<unsigned char> ((value_ >> 8) & 0xFFu);
    out_[3] = static_cast<unsigned char> (value_ & 0xFFu);
}

static uint32_t read_u32_be (const unsigned char in_[4])
{
    return (static_cast<uint32_t> (in_[0]) << 24)
           | (static_cast<uint32_t> (in_[1]) << 16)
           | (static_cast<uint32_t> (in_[2]) << 8)
           | static_cast<uint32_t> (in_[3]);
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

static int recv_exact (int fd_, void *buf_, size_t size_)
{
    LIBZLINK_UNUSED (fd_);
    LIBZLINK_UNUSED (buf_);
    LIBZLINK_UNUSED (size_);
    return -1;
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

void test_stream_len32be_recv_fragmented_and_coalesced ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    configure_stream_len32be (server, 1024, 4096);

    const int recv_timeout_ms = 5000;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      server, ZLINK_RCVTIMEO, &recv_timeout_ms, sizeof (recv_timeout_ms)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);

    const char fragmented_payload[] = "fragmented-payload";
    enum
    {
        fragmented_size = sizeof (fragmented_payload) - 1
    };
    unsigned char fragmented_header[4];
    write_u32_be (fragmented_header, fragmented_size);

    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, fragmented_header, 2));

    unsigned char header_and_partial[5];
    memcpy (header_and_partial, fragmented_header + 2, 2);
    memcpy (header_and_partial + 2, fragmented_payload, 3);
    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, header_and_partial, sizeof (header_and_partial)));

    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, fragmented_payload + 3, fragmented_size - 3));

    unsigned char recv_id[stream_routing_id_size];
    char recv_buf[128];
    int rc = recv_stream_msg (server, recv_id, recv_buf, sizeof (recv_buf));
    TEST_ASSERT_EQUAL_INT (fragmented_size, rc);
    TEST_ASSERT_EQUAL_STRING_LEN (fragmented_payload, recv_buf, fragmented_size);

    const char packet_a[] = "ABC";
    const char packet_b[] = "01234567";
    enum
    {
        packet_a_size = sizeof (packet_a) - 1,
        packet_b_size = sizeof (packet_b) - 1,
        coalesced_size = 4 + packet_a_size + 4 + packet_b_size
    };

    unsigned char coalesced[coalesced_size];
    write_u32_be (coalesced, packet_a_size);
    memcpy (coalesced + 4, packet_a, packet_a_size);
    write_u32_be (coalesced + 4 + packet_a_size, packet_b_size);
    memcpy (coalesced + 8 + packet_a_size, packet_b, packet_b_size);

    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, coalesced, sizeof (coalesced)));

    unsigned char recv_id_a[stream_routing_id_size];
    char recv_buf_a[64];
    rc = recv_stream_msg (server, recv_id_a, recv_buf_a, sizeof (recv_buf_a));
    TEST_ASSERT_EQUAL_INT (packet_a_size, rc);
    TEST_ASSERT_EQUAL_STRING_LEN (packet_a, recv_buf_a, packet_a_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (recv_id, recv_id_a, stream_routing_id_size);

    unsigned char recv_id_b[stream_routing_id_size];
    char recv_buf_b[64];
    rc = recv_stream_msg (server, recv_id_b, recv_buf_b, sizeof (recv_buf_b));
    TEST_ASSERT_EQUAL_INT (packet_b_size, rc);
    TEST_ASSERT_EQUAL_STRING_LEN (packet_b, recv_buf_b, packet_b_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (recv_id, recv_id_b, stream_routing_id_size);

    close_raw_fd (client_fd);
    test_context_socket_close_zero_linger (server);
}

void test_stream_len32be_send_frames_payload ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    configure_stream_len32be (server, 1024, 4096);

    const int recv_timeout_ms = 5000;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      server, ZLINK_RCVTIMEO, &recv_timeout_ms, sizeof (recv_timeout_ms)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);

    const char probe_payload[] = "probe";
    enum
    {
        probe_size = sizeof (probe_payload) - 1
    };
    unsigned char probe_header[4];
    write_u32_be (probe_header, probe_size);
    TEST_ASSERT_EQUAL_INT (0, send_stream_packet (client_fd, probe_header, 4));
    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, probe_payload, probe_size));

    unsigned char server_id[stream_routing_id_size];
    char probe_recv_buf[64];
    int rc = recv_stream_msg (server, server_id, probe_recv_buf,
                              sizeof (probe_recv_buf));
    TEST_ASSERT_EQUAL_INT (probe_size, rc);
    TEST_ASSERT_EQUAL_STRING_LEN (probe_payload, probe_recv_buf, probe_size);

    const char reply_payload[] = "reply";
    enum
    {
        reply_size = sizeof (reply_payload) - 1,
        framed_reply_size = 4 + reply_size
    };
    send_stream_msg (server, server_id, reply_payload, reply_size);

    unsigned char framed_reply[framed_reply_size];
    TEST_ASSERT_EQUAL_INT (
      0, recv_exact (client_fd, framed_reply, sizeof (framed_reply)));
    TEST_ASSERT_EQUAL_UINT32 (reply_size, read_u32_be (framed_reply));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      reinterpret_cast<const unsigned char *> (reply_payload),
      framed_reply + 4, reply_size);

    close_raw_fd (client_fd);
    test_context_socket_close_zero_linger (server);
}

void test_stream_len32be_oversize_disconnect ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    configure_stream_len32be (server, 16, 128);

    const int recv_timeout_ms = 200;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      server, ZLINK_RCVTIMEO, &recv_timeout_ms, sizeof (recv_timeout_ms)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    void *monitor = zlink_socket_monitor_open (
      server, ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
    TEST_ASSERT_NOT_NULL (monitor);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (monitor, ZLINK_LINGER, &zero, sizeof (zero)));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);

    unsigned char connected_id[stream_routing_id_size];
    TEST_ASSERT_TRUE (wait_monitor_event (
      monitor, server, ZLINK_EVENT_CONNECTION_READY, connected_id, 2000));

    unsigned char invalid_header[4];
    write_u32_be (invalid_header, 4096);
    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, invalid_header, sizeof (invalid_header)));

    unsigned char tmp_rid[stream_routing_id_size];
    char tmp_payload[32];
    int rc = zlink_recv (server, tmp_rid, stream_routing_id_size, 0);
    if (rc == static_cast<int> (stream_routing_id_size)) {
        TEST_ASSERT_EQUAL_INT (
          -1, zlink_recv (server, tmp_payload, sizeof (tmp_payload),
                          ZLINK_DONTWAIT));
    } else {
        TEST_ASSERT_EQUAL_INT (-1, rc);
    }

    unsigned char disconnected_id[stream_routing_id_size];
    TEST_ASSERT_TRUE (wait_monitor_event (
      monitor, server, ZLINK_EVENT_DISCONNECTED, disconnected_id, 10000));
    TEST_ASSERT_EQUAL_UINT8_ARRAY (
      connected_id, disconnected_id, stream_routing_id_size);

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

    RUN_TEST (test_stream_tcp_basic);
    RUN_TEST (test_stream_maxmsgsize);
    RUN_TEST (test_stream_len32be_recv_fragmented_and_coalesced);
    RUN_TEST (test_stream_len32be_send_frames_payload);
    RUN_TEST (test_stream_len32be_oversize_disconnect);
    RUN_TEST (test_stream_tcp_connect_basic);

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
