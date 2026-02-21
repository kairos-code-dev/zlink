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

    RUN_TEST (test_stream_tcp_basic);
    RUN_TEST (test_stream_maxmsgsize);
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
