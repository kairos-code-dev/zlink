/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

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

void test_stream_fastpath_tcp_basic ()
{
    void *server = test_context_socket (ZLINK_STREAM);
    void *client = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (client, ZLINK_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    unsigned char server_id[stream_routing_id_size];
    unsigned char client_id[stream_routing_id_size];

    recv_stream_event (server, 0x01, server_id);
    recv_stream_event (client, 0x01, client_id);

    const char payload[] = "hello";
    send_stream_msg (client, client_id, payload, sizeof (payload) - 1);

    unsigned char recv_id[stream_routing_id_size];
    char recv_buf[64];
    int rc = recv_stream_msg (server, recv_id, recv_buf, sizeof (recv_buf));
    TEST_ASSERT_EQUAL_INT ((int) sizeof (payload) - 1, rc);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (server_id, recv_id, stream_routing_id_size);
    TEST_ASSERT_EQUAL_STRING_LEN (payload, recv_buf, sizeof (payload) - 1);

    const char reply[] = "world";
    send_stream_msg (server, server_id, reply, sizeof (reply) - 1);

    rc = recv_stream_msg (client, recv_id, recv_buf, sizeof (recv_buf));
    TEST_ASSERT_EQUAL_INT ((int) sizeof (reply) - 1, rc);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (client_id, recv_id, stream_routing_id_size);
    TEST_ASSERT_EQUAL_STRING_LEN (reply, recv_buf, sizeof (reply) - 1);

    test_context_socket_close_zero_linger (client);

    zlink_pollitem_t items[] = {{server, 0, ZLINK_POLLIN, 0}};
    TEST_ASSERT_EQUAL_INT (1, zlink_poll (items, 1, 2000));

    recv_stream_event (server, 0x00, recv_id);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (server_id, recv_id, stream_routing_id_size);

    test_context_socket_close_zero_linger (server);
}

int main (void)
{
    UNITY_BEGIN ();

    setup_test_environment ();

    RUN_TEST (test_stream_fastpath_tcp_basic);

    return UNITY_END ();
}
