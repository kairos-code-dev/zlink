/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_pubsub_scenario_shared.hpp"

#include <string.h>

void test_spot_node_discovery_direct_and_child_interop ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    int registry_seed = 33190;
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));

    void *pub_discovery = zlink_discovery_new (
      ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "spot-discovery-interop");
    void *sub_discovery = zlink_discovery_new (
      ctx, ZLINK_AUTO_CONNECT_SPOT_MESH, "spot-discovery-interop");
    TEST_ASSERT_NOT_NULL (pub_discovery);
    TEST_ASSERT_NOT_NULL (sub_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (pub_discovery, registry_router, 2000));
    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (sub_discovery, registry_router, 2000));

    void *pub_node = create_spot_node (ctx, "spot-discovery-interop");
    void *sub_node = create_spot_node (ctx, "spot-discovery-interop");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);
    void *pub = create_spot_pub_handle (pub_node);
    void *sub = create_spot_sub_handle (sub_node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (pub_node, "z-spot-discovery-pub", 20));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (sub_node, "a-spot-discovery-sub", 20));

    char pub_endpoint[MAX_SOCKET_STRING];
    char sub_endpoint[MAX_SOCKET_STRING];
    int port_seed = 33100;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &port_seed, pub_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      sub_node, "tcp://127.0.0.1:", &port_seed, sub_endpoint));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (pub_node, pub_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (sub_node, sub_discovery));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "interop:node"));

    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      pub_node, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_READY, 1, 10000));

    bool received = false;
    for (int attempt = 0; attempt < 40 && !received; ++attempt) {
        TEST_ASSERT_SUCCESS_ERRNO (publish_text (
          &zlink_publish, pub, "interop:node", "node-hop", 0));
        received = wait_for_spot_recv_message (
          sub, "interop:node", "node-hop", 8, 250);
    }
    TEST_ASSERT_TRUE (received);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_unset_subscription (sub, "interop:node"));

    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&sub_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&pub_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
