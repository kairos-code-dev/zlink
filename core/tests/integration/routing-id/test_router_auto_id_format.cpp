/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"
#include "../../../src/core/ctx.hpp"

#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
void *create_sync_socket (int type_)
{
    void *socket =
      static_cast<zlink::ctx_t *> (get_test_context ())->create_socket (type_);
    TEST_ASSERT_NOT_NULL (socket);
    return socket;
}

void close_sync_socket_zero_linger (void *socket_)
{
    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (socket_));
}
}

void test_router_auto_id_format ()
{
    void *server = create_sync_socket (ZLINK_ROUTER);
    void *client = create_sync_socket (ZLINK_DEALER);
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

    unsigned char auto_routing_id[255];
    size_t auto_routing_id_size = sizeof (auto_routing_id);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_getsockopt (
      client, ZLINK_ROUTING_ID, auto_routing_id, &auto_routing_id_size));
    TEST_ASSERT_EQUAL_UINT (16u, auto_routing_id_size);

    const char payload[] = "hello";
    send_string_expect_success (client, payload, 0);

    unsigned char routing_id[255];
    const int rid_size = TEST_ASSERT_SUCCESS_ERRNO (
      zlink::recv_buffer_internal (server, routing_id, sizeof (routing_id), 0));
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
