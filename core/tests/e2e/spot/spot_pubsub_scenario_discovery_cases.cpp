/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_pubsub_scenario_shared.hpp"

#include <string.h>

static int zone_idx (int x_, int y_, int width_)
{
    return y_ * width_ + x_;
}

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

    void *discovery = zlink_discovery_new (
      ctx, ZLINK_SERVICE_TYPE_SPOT, "spot-discovery-interop");
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (discovery, registry_router, 2000));

    void *pub_node = create_spot_node (ctx, "spot-discovery-interop");
    void *sub_node = create_spot_node (ctx, "spot-discovery-interop");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);
    void *pub = create_spot_pub_handle (pub_node);
    void *sub = create_spot_sub_handle (sub_node, &queued_spot_handler);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    char pub_endpoint[MAX_SOCKET_STRING];
    char sub_endpoint[MAX_SOCKET_STRING];
    int port_seed = 33100;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &port_seed, pub_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      sub_node, "tcp://127.0.0.1:", &port_seed, sub_endpoint));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (pub_node, discovery));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (sub_node, discovery));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "interop:node"));

    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      pub_node, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_SEND_READY, 1, 5000));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      sub_node, ZLINK_SPOT_ROLE_SUB, ZLINK_MONITOR_STATE_READY, 1, 5000));
    TEST_ASSERT_TRUE (wait_for_spot_ready_state (
      pub, ZLINK_MONITOR_STATE_SEND_READY, 1, 5000));
    TEST_ASSERT_TRUE (wait_for_spot_ready_state (
      sub, ZLINK_MONITOR_STATE_READY, 1, 5000));

    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_publish, pub, "interop:node", "node-hop", 0));
    TEST_ASSERT_TRUE (wait_for_spot_message (
      sub, "interop:node", "node-hop", 8, 5000));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_unset_subscription (sub, "interop:node"));

    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_mmorpg_zone_adjacency_scale_multi_node_discovery ()
{
    if (env_int_or_default ("ZLINK_SPOT_RUN_MULTI_NODE_DISCOVERY", 0) == 0) {
        TEST_IGNORE_MESSAGE (
          "Multi-node discovery scale test disabled "
          "(set ZLINK_SPOT_RUN_MULTI_NODE_DISCOVERY=1)");
        return;
    }

    const int field_width =
      env_int_or_default ("ZLINK_SPOT_MULTI_NODE_FIELD_WIDTH", 4);
    const int field_height =
      env_int_or_default ("ZLINK_SPOT_MULTI_NODE_FIELD_HEIGHT", 4);
    const int zone_count = field_width * field_height;
    const int spot_node_count =
      env_int_or_default ("ZLINK_SPOT_MULTI_NODE_COUNT", 4);

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    int registry_seed = 33090;
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT, "spot-field-mmorpg");
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (discovery, registry_router, 2000));

    std::vector<void *> nodes (spot_node_count, static_cast<void *> (NULL));
    std::vector<void *> subs (spot_node_count, static_cast<void *> (NULL));
    int port_seed = 33000;
    for (int i = 0; i < spot_node_count; ++i) {
        nodes[i] = create_spot_node (ctx, "spot-field-mmorpg");
        TEST_ASSERT_NOT_NULL (nodes[i]);
        subs[i] = create_spot_sub_handle (nodes[i], &queued_spot_handler);
        TEST_ASSERT_NOT_NULL (subs[i]);

        char endpoint[256] = {0};
        TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
          nodes[i], "tcp://127.0.0.1:", &port_seed, endpoint));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_attach_discovery (nodes[i], discovery));
    }

    for (int i = 0; i < spot_node_count; ++i) {
        TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
          nodes[i], ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_SEND_READY, 1,
          5000));
    }

    std::vector<std::string> topics (zone_count);
    for (int y = 0; y < field_height; ++y) {
        for (int x = 0; x < field_width; ++x) {
            const int idx = zone_idx (x, y, field_width);
            char topic_buf[64];
            snprintf (topic_buf, sizeof (topic_buf), "field-mm:%d:%d:state", x, y);
            topics[idx] = topic_buf;
            TEST_ASSERT_SUCCESS_ERRNO (
              zlink_set_subscription (subs[idx % spot_node_count],
                                      topics[idx].c_str ()));
        }
    }

    msleep (300);

    for (int i = 0; i < spot_node_count; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&nodes[i]));
    }
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_discovery_destroy_invalidates_attached_spot_node_handle ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT, "spot-owned-node");
    TEST_ASSERT_NOT_NULL (discovery);

    void *node = create_spot_node (ctx, "spot-owned-node");
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (node, discovery));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));

    zlink_spot_node_status_t status;
    memset (&status, 0, sizeof (status));
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_spot_node_status_snapshot (node, &status));
    TEST_ASSERT_EQUAL_INT (ESHUTDOWN, errno);

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1,
                           zlink_spot_node_bind (node, "tcp://127.0.0.1:*"));
    TEST_ASSERT_EQUAL_INT (ESHUTDOWN, errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_manual_peer_topology_ownership ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    int registry_seed = 33290;
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub), registry_router,
      sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

    void *server = create_spot_node (ctx, "spot-manual-topology");
    void *client = create_spot_node (ctx, "spot-manual-topology");
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = 33200;
    step_log ("spot manual topology: bind server");
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      server, "tcp://127.0.0.1:", &port_seed, endpoint));
    step_log ("spot manual topology: connect manual peer");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_connect_peer (client, endpoint));

    step_log ("spot manual topology: create discovery");
    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT, "spot-manual-topology");
    TEST_ASSERT_NOT_NULL (discovery);
    step_log ("spot manual topology: connect discovery");
    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (discovery, registry_router, 2000));

    step_log ("spot manual topology: attach rejected while manual peer active");
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_spot_node_attach_discovery (client, discovery));
    TEST_ASSERT_EQUAL_INT (EBUSY, errno);

    step_log ("spot manual topology: disconnect manual peer");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_disconnect_peer (client, endpoint));
    step_log ("spot manual topology: attach discovery");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (client, discovery));

    step_log ("spot manual topology: manual peer apis rejected");
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_spot_node_connect_peer (client, endpoint));
    TEST_ASSERT_EQUAL_INT (EBUSY, errno);
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_spot_node_disconnect_peer (client, endpoint));
    TEST_ASSERT_EQUAL_INT (EBUSY, errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&server));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
