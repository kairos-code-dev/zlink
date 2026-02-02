/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

void test_router_auto_id_format ()
{
    void *server = test_context_socket (ZLINK_ROUTER);
    void *client = test_context_socket (ZLINK_DEALER);
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

    const char payload[] = "hello";
    send_string_expect_success (client, payload, 0);

    unsigned char routing_id[255];
    int rid_len = zlink_recv (server, routing_id, sizeof routing_id, 0);
    TEST_ASSERT_EQUAL_INT (5, rid_len);
    TEST_ASSERT_EQUAL_UINT8 (0, routing_id[0]);

    char recv_buf[sizeof payload];
    int rc = zlink_recv (server, recv_buf, sizeof recv_buf, 0);
    TEST_ASSERT_EQUAL_INT ((int) sizeof (payload) - 1, rc);
    TEST_ASSERT_EQUAL_STRING_LEN (payload, recv_buf, sizeof (payload) - 1);

    test_context_socket_close_zero_linger (client);
    test_context_socket_close_zero_linger (server);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_router_auto_id_format);
    return UNITY_END ();
}
