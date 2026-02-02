/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

SETUP_TEARDOWN_TESTCONTEXT

static const size_t stream_routing_id_size = 5;

static void recv_stream_event (void *socket_,
                               unsigned char expected_code_,
                               unsigned char routing_id_[255])
{
    int rc = zlink_recv (socket_, routing_id_, 255, 0);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (stream_routing_id_size), rc);
    TEST_ASSERT_EQUAL_UINT8 (0, routing_id_[0]);

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

void test_stream_auto_routing_id_size ()
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
    bind_loopback_ipv4 (server, endpoint, sizeof endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    unsigned char server_id[255];
    unsigned char client_id[255];
    recv_stream_event (server, 0x01, server_id);
    recv_stream_event (client, 0x01, client_id);

    test_context_socket_close_zero_linger (client);
    test_context_socket_close_zero_linger (server);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_stream_auto_routing_id_size);
    return UNITY_END ();
}
