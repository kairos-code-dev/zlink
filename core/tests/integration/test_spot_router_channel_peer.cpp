/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <cstring>

namespace
{
void set_routing_id_text (void *handle_, const char *text_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (handle_, text_, strlen (text_)));
}

zlink_routing_id_t get_routing_id_value (void *handle_)
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (handle_, &rid));
    return rid;
}

void bind_router (void *router_, char *endpoint_, size_t endpoint_size_)
{
    snprintf (endpoint_, endpoint_size_, "inproc://router-channel-legacy-%p", router_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router_, endpoint_));
}

void test_spot_node_router_channel_rejects_invalid_channel_name ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);

    TEST_ASSERT_EQUAL (ZLINK_CONNECT_INVALID_ARGUMENT,
                       zlink_spot_node_connect_router_channel_peer (node, "", "tcp://127.0.0.1:1"));
    TEST_ASSERT_EQUAL (EINVAL, zlink_errno ());
    TEST_ASSERT_EQUAL (ZLINK_CONNECT_INVALID_ARGUMENT, zlink_spot_node_connect_router_channel_peer (
                                                         node, NULL, "tcp://127.0.0.1:1"));
    TEST_ASSERT_EQUAL (EINVAL, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_router_channel_legacy_apis_return_migration_error ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *discovery = zlink_discovery_new (ctx, ZLINK_AUTO_CONNECT_CLIENT_SERVER, "api");
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (discovery);

    set_routing_id_text (router, "legacy-route-router");
    char endpoint[MAX_SOCKET_STRING];
    bind_router (router, endpoint, sizeof (endpoint));
    const zlink_routing_id_t router_rid = get_routing_id_value (router);

    TEST_ASSERT_NOT_EQUAL (ZLINK_CONNECT_OK,
                           zlink_spot_node_connect_router_channel_peer (node, "api", endpoint));
    TEST_ASSERT_EQUAL (ENOTSUP, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (ZLINK_CONNECT_OK,
                           zlink_spot_node_connect_router_channel_peer_rid (node, "api",
                                                                            &router_rid,
                                                                            endpoint));
    TEST_ASSERT_EQUAL (ENOTSUP, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (ZLINK_CONNECT_OK,
                           zlink_spot_node_disconnect_router_channel_peer (node, "api", endpoint));
    TEST_ASSERT_EQUAL (ENOTSUP, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (ZLINK_CONNECT_OK,
                           zlink_spot_node_disconnect_router_channel_peer_rid (node, "api",
                                                                               &router_rid));
    TEST_ASSERT_EQUAL (ENOTSUP, zlink_errno ());
    TEST_ASSERT_NOT_EQUAL (ZLINK_CONFIG_OK,
                           zlink_spot_node_attach_router_channel_discovery (node, "api",
                                                                            discovery));
    TEST_ASSERT_EQUAL (ENOTSUP, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
} // namespace

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_spot_node_router_channel_legacy_apis_return_migration_error);
    RUN_TEST (test_spot_node_router_channel_rejects_invalid_channel_name);
    return UNITY_END ();
}
