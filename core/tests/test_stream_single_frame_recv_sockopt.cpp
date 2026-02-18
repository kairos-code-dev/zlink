/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <errno.h>
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
    std::vector<unsigned char> frame (4 + size_);
    write_u32_be (&frame[0], static_cast<uint32_t> (size_));
    if (size_ > 0)
        memcpy (&frame[4], data_, size_);
    return send_all (fd_, &frame[0], frame.size ());
}

static void close_raw_fd (int fd_)
{
    if (fd_ >= 0)
        close (fd_);
}
#endif

static uint32_t recv_connect_event (void *socket_, unsigned char expected_code_)
{
    zlink_msg_t first;
    zlink_msg_t second;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&first));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&second));

    int rc = zlink_msg_recv (&first, socket_, 0);
    TEST_ASSERT_TRUE (rc > 0);

    int more = 0;
    size_t more_size = sizeof (more);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (socket_, ZLINK_RCVMORE, &more, &more_size));

    uint32_t routing_id = 0;
    unsigned char code = 0xFF;

    if (rc == static_cast<int> (stream_routing_id_size) && more) {
        routing_id = read_u32_be (
          static_cast<const unsigned char *> (zlink_msg_data (&first)));
        rc = zlink_msg_recv (&second, socket_, 0);
        TEST_ASSERT_EQUAL_INT (1, rc);
        code = *(static_cast<const unsigned char *> (zlink_msg_data (&second)));
    } else {
        TEST_ASSERT_FALSE (more);
        TEST_ASSERT_EQUAL_INT (1, rc);
        routing_id = zlink_msg_get_routing_id (&first);
        code = *(static_cast<const unsigned char *> (zlink_msg_data (&first)));
    }

    TEST_ASSERT_EQUAL_UINT8 (expected_code_, code);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&second));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&first));
    return routing_id;
}

void test_stream_single_frame_recv_sockopt_off ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      server, ZLINK_STREAM_SINGLE_FRAME_RECV, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);

    const uint32_t server_routing_id = recv_connect_event (server, 0x01);
    TEST_ASSERT_NOT_EQUAL (0, server_routing_id);

    const char payload[] = "abc";
    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, payload, sizeof (payload) - 1));

    zlink_msg_t first;
    zlink_msg_t second;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&first));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&second));

    int rc = zlink_msg_recv (&first, server, 0);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (stream_routing_id_size), rc);

    int more = 0;
    size_t more_size = sizeof (more);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (server, ZLINK_RCVMORE, &more, &more_size));
    TEST_ASSERT_TRUE (more);

    rc = zlink_msg_recv (&second, server, 0);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (sizeof (payload) - 1), rc);
    TEST_ASSERT_EQUAL_MEMORY (payload, zlink_msg_data (&second),
                              sizeof (payload) - 1);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&second));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&first));
    close_raw_fd (client_fd);
    test_context_socket_close_zero_linger (server);
}

void test_stream_single_frame_recv_sockopt_on ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));

    const int one = 1;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      server, ZLINK_STREAM_SINGLE_FRAME_RECV, &one, sizeof (one)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    const int client_fd = connect_raw_tcp (endpoint);
    TEST_ASSERT_TRUE (client_fd >= 0);

    const uint32_t server_routing_id = recv_connect_event (server, 0x01);
    TEST_ASSERT_NOT_EQUAL (0, server_routing_id);

    const char payload[] = "xyz";
    TEST_ASSERT_EQUAL_INT (
      0, send_stream_packet (client_fd, payload, sizeof (payload) - 1));

    zlink_msg_t msg;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&msg));

    const int rc = zlink_msg_recv (&msg, server, 0);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (sizeof (payload) - 1), rc);
    TEST_ASSERT_EQUAL_MEMORY (payload, zlink_msg_data (&msg),
                              sizeof (payload) - 1);

    int more = 1;
    size_t more_size = sizeof (more);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (server, ZLINK_RCVMORE, &more, &more_size));
    TEST_ASSERT_FALSE (more);

    TEST_ASSERT_EQUAL_UINT32 (server_routing_id,
                              zlink_msg_get_routing_id (&msg));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&msg));
    close_raw_fd (client_fd);
    test_context_socket_close_zero_linger (server);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_stream_single_frame_recv_sockopt_off);
    RUN_TEST (test_stream_single_frame_recv_sockopt_on);
    return UNITY_END ();
}
