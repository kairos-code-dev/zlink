/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
void *create_sync_socket (int type_)
{
    void *socket =
      zlink_socket (get_test_context (), static_cast<zlink_socket_type_t> (type_));
    TEST_ASSERT_NOT_NULL (socket);
    return socket;
}

void close_sync_socket_zero_linger (void *socket_)
{
    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (socket_, ZLINK_OPT_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (socket_));
}
}

void test_router_auto_id_format ()
{
    void *server = create_sync_socket (ZLINK_SOCKET_ROUTER);
    void *client = create_sync_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (server, ZLINK_OPT_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (client, ZLINK_OPT_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    unsigned char auto_routing_id[255];
    size_t auto_routing_id_size = sizeof (auto_routing_id);
    zlink_routing_id_t auto_routing_id_value;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_routing_id (client, &auto_routing_id_value));
    TEST_ASSERT_EQUAL_UINT (16u, auto_routing_id_value.size);
    memcpy (auto_routing_id, auto_routing_id_value.data,
            auto_routing_id_value.size);
    auto_routing_id_size = auto_routing_id_value.size;
    TEST_ASSERT_EQUAL_UINT (16u, auto_routing_id_size);

    const char payload[] = "hello";
    send_string_expect_success (client, payload, 0);

    unsigned char routing_id[255];
    const int rid_size = TEST_ASSERT_SUCCESS_ERRNO (
      zlink_recv (server, routing_id, sizeof (routing_id), 0));
    TEST_ASSERT_EQUAL_INT (16, rid_size);
    TEST_ASSERT_EQUAL_MEMORY (auto_routing_id, routing_id, auto_routing_id_size);
    recv_string_expect_success (server, payload, 0);

    close_sync_socket_zero_linger (client);
    close_sync_socket_zero_linger (server);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_router_auto_id_format);
    return UNITY_END ();
}
