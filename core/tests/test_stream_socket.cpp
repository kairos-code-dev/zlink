/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include "utils/config.hpp"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vector>

#if defined(ZLINK_HAVE_WINDOWS)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

SETUP_TEARDOWN_TESTCONTEXT

static const size_t stream_routing_id_size = 4;

static void recv_stream_event (void *socket_,
                               unsigned char expected_code_,
                               unsigned char routing_id_[stream_routing_id_size])
{
    int rc = zlink_recv (socket_, routing_id_, stream_routing_id_size, 0);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (stream_routing_id_size), rc);

    int more = 0;
    size_t more_size = sizeof (more);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (socket_, ZLINK_RCVMORE, &more, &more_size));
    TEST_ASSERT_TRUE (more);

    unsigned char code = 0xFF;
    rc = zlink_recv (socket_, &code, 1, 0);
    TEST_ASSERT_EQUAL_INT (1, rc);
    TEST_ASSERT_EQUAL_UINT8 (expected_code_, code);
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

static uint32_t read_u32_be (const unsigned char *p_)
{
    return (static_cast<uint32_t> (p_[0]) << 24)
           | (static_cast<uint32_t> (p_[1]) << 16)
           | (static_cast<uint32_t> (p_[2]) << 8)
           | static_cast<uint32_t> (p_[3]);
}

static void write_u32_be (unsigned char *p_, uint32_t v_)
{
    p_[0] = static_cast<unsigned char> ((v_ >> 24) & 0xFF);
    p_[1] = static_cast<unsigned char> ((v_ >> 16) & 0xFF);
    p_[2] = static_cast<unsigned char> ((v_ >> 8) & 0xFF);
    p_[3] = static_cast<unsigned char> (v_ & 0xFF);
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

static int recv_all (int fd_, unsigned char *buf_, size_t size_)
{
    size_t off = 0;
    while (off < size_) {
        const ssize_t n = recv (fd_, buf_ + off, size_ - off, 0);
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
    std::vector<unsigned char> frame (4 + size_);
    write_u32_be (&frame[0], static_cast<uint32_t> (size_));
    if (size_ > 0)
        memcpy (&frame[4], data_, size_);
    return send_all (fd_, &frame[0], frame.size ());
}

static int recv_stream_packet (int fd_, void *buf_, size_t cap_)
{
    unsigned char hdr[4];
    if (recv_all (fd_, hdr, sizeof (hdr)) != 0)
        return -1;

    const uint32_t size = read_u32_be (hdr);
    if (size > cap_)
        return -1;

    if (size > 0 && recv_all (fd_, static_cast<unsigned char *> (buf_), size) != 0)
        return -1;

    return static_cast<int> (size);
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

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);

    unsigned char server_id[stream_routing_id_size];
    recv_stream_event (server, 0x01, server_id);

    const char payload[] = "hello";
    TEST_ASSERT_EQUAL_INT (0, send_stream_packet (
                                client_fd, payload, sizeof (payload) - 1));

    unsigned char recv_id[stream_routing_id_size];
    char recv_buf[64];
    int rc = recv_stream_msg (server, recv_id, recv_buf, sizeof (recv_buf));
    TEST_ASSERT_EQUAL_INT ((int) sizeof (payload) - 1, rc);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (server_id, recv_id, stream_routing_id_size);
    TEST_ASSERT_EQUAL_STRING_LEN (payload, recv_buf, sizeof (payload) - 1);

    const char reply[] = "world";
    send_stream_msg (server, server_id, reply, sizeof (reply) - 1);

    char client_recv_buf[64];
    rc = recv_stream_packet (client_fd, client_recv_buf, sizeof (client_recv_buf));
    TEST_ASSERT_EQUAL_INT ((int) sizeof (reply) - 1, rc);
    TEST_ASSERT_EQUAL_STRING_LEN (reply, client_recv_buf, sizeof (reply) - 1);

    close_raw_fd (client_fd);

    zlink_pollitem_t items[] = {{server, 0, ZLINK_POLLIN, 0}};
    TEST_ASSERT_EQUAL_INT (1, zlink_poll (items, 1, 2000));

    recv_stream_event (server, 0x00, recv_id);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (server_id, recv_id, stream_routing_id_size);

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

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);

    unsigned char server_id[stream_routing_id_size];
    recv_stream_event (server, 0x01, server_id);

    const char payload[] = "toolarge";
    TEST_ASSERT_EQUAL_INT (0, send_stream_packet (
                                client_fd, payload, sizeof (payload) - 1));

    zlink_pollitem_t items[] = {{server, 0, ZLINK_POLLIN, 0}};
    TEST_ASSERT_EQUAL_INT (1, zlink_poll (items, 1, 2000));

    unsigned char recv_id[stream_routing_id_size];
    recv_stream_event (server, 0x00, recv_id);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (server_id, recv_id, stream_routing_id_size);

    close_raw_fd (client_fd);
    test_context_socket_close_zero_linger (server);
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
