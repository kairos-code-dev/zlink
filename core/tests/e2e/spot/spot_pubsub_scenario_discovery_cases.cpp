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

    void *discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (discovery, registry_router, 2000));
    void *pub_node = create_spot_node (ctx, "spot-discovery-interop");
    void *sub_node = create_spot_node (ctx, "spot-discovery-interop");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    char pub_endpoint[MAX_SOCKET_STRING];
    char sub_endpoint[MAX_SOCKET_STRING];
    int port_seed = 33100;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &port_seed, pub_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      sub_node, "tcp://127.0.0.1:", &port_seed, sub_endpoint));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_publish (pub_node, "__warmup__", NULL, 0, 0));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (pub_node, discovery));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_attach_discovery (sub_node, discovery));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      pub_node, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_SEND_READY, 1, 5000));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      sub_node, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_SEND_READY, 1, 5000));

    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (sub_node, true));
    service_monitor_probe_t node_sub_monitor_probe;
    void *node_sub_monitor = open_spot_node_monitor_with_probe (
      sub_node, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
        | ZLINK_MONITOR_EVENT_ERROR,
      &node_sub_monitor_probe);
    TEST_ASSERT_NOT_NULL (node_sub_monitor);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_subscribe (sub_node, "interop:node"));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &node_sub_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &node_sub_monitor_probe, ZLINK_SPOT_SUB_SUBSCRIPTION_READY, pub_endpoint,
      3000));

    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_spot_node_publish, pub_node, "interop:node", "node-hop", 0));
    TEST_ASSERT_TRUE (wait_for_node_message (
      sub_node, "interop:node", "node-hop", 8, 5000));
    TEST_ASSERT_SUCCESS_ERRNO (
      close_service_monitor_with_probe (&node_sub_monitor));

    void *child_sub = create_spot_handle (pub_node, &queued_spot_handler);
    void *child_pub = create_spot_handle (sub_node, &ignore_spot_handler);
    TEST_ASSERT_NOT_NULL (child_sub);
    TEST_ASSERT_NOT_NULL (child_pub);

    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (child_sub, false));
    service_monitor_probe_t child_sub_monitor_probe;
    void *child_sub_monitor = open_spot_monitor_with_probe (
      child_sub, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_SUBSCRIPTION_READY
        | ZLINK_MONITOR_EVENT_ERROR,
      &child_sub_monitor_probe);
    TEST_ASSERT_NOT_NULL (child_sub_monitor);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_subscribe (child_sub, "interop:child"));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &child_sub_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 3000));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &child_sub_monitor_probe, ZLINK_SPOT_SUB_SUBSCRIPTION_READY,
      sub_endpoint, 3000));

    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_spot_publish, child_pub, "interop:child", "child-hop", 0));
    TEST_ASSERT_TRUE (wait_for_spot_message (
      child_sub, "interop:child", "child-hop", 9, 5000));
    TEST_ASSERT_SUCCESS_ERRNO (
      close_service_monitor_with_probe (&child_sub_monitor));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_unsubscribe (sub_node, "interop:node"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_unsubscribe (child_sub, "interop:child"));
    msleep (50);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&child_sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&child_pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
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
      env_int_or_default ("ZLINK_SPOT_MULTI_NODE_FIELD_WIDTH", 8);
    const int field_height =
      env_int_or_default ("ZLINK_SPOT_MULTI_NODE_FIELD_HEIGHT", 8);
    const int zone_count = field_width * field_height;
    const int spot_node_count =
      env_int_or_default ("ZLINK_SPOT_MULTI_NODE_COUNT", 4);
    const int sub_propagation_ms = 1200;
    const int recv_timeout_ms = 1000;

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
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_SPOT);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (discovery, registry_router, 2000));
    std::vector<void *> nodes (spot_node_count, static_cast<void *> (NULL));
    std::vector<std::string> node_endpoints (spot_node_count);
    int port_seed = 33000;
    for (int i = 0; i < spot_node_count; ++i) {
        nodes[i] = create_spot_node (ctx, "spot-field-mmorpg");
        TEST_ASSERT_NOT_NULL (nodes[i]);
        int sndhwm = 1000000;
        int rcvhwm = 1000000;
        int linger = 0;
        TEST_ASSERT_SUCCESS_ERRNO (set_node_pub_option (
          nodes[i], ZLINK_SPOT_PUB_OPT_SNDHWM, &sndhwm, sizeof (sndhwm)));
        TEST_ASSERT_SUCCESS_ERRNO (set_node_sub_option (
          nodes[i], ZLINK_SPOT_SUB_OPT_RCVHWM, &rcvhwm, sizeof (rcvhwm)));
        TEST_ASSERT_SUCCESS_ERRNO (set_node_pub_option (
          nodes[i], ZLINK_SPOT_PUB_OPT_LINGER, &linger, sizeof (linger)));
        TEST_ASSERT_SUCCESS_ERRNO (set_node_sub_option (
          nodes[i], ZLINK_SPOT_SUB_OPT_LINGER, &linger, sizeof (linger)));
        char endpoint[256] = {0};
        TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
          nodes[i], "tcp://127.0.0.1:", &port_seed, endpoint));
        node_endpoints[i] = endpoint;
    }

    for (int i = 0; i < spot_node_count; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_attach_discovery (nodes[i], discovery));
    }

    if (spot_node_count > 1) {
        for (int i = 0; i < spot_node_count; ++i) {
            TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
              nodes[i], ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_SEND_READY, 1,
              5000));
        }
    }

    std::vector<void *> spot_pubs (zone_count, static_cast<void *> (NULL));
    std::vector<void *> spot_subs (zone_count, static_cast<void *> (NULL));
    std::vector<std::string> topics (zone_count);

    for (int y = 0; y < field_height; ++y) {
        for (int x = 0; x < field_width; ++x) {
            const int idx = zone_idx (x, y, field_width);
            const int owner_node = idx % spot_node_count;

            TEST_ASSERT_SUCCESS_ERRNO (create_spot_pub_sub (
              nodes[owner_node], &spot_pubs[idx], &spot_subs[idx]));

            char topic_buf[64];
            snprintf (topic_buf, sizeof (topic_buf), "field-mm:%d:%d:state", x, y);
            topics[idx] = topic_buf;
        }
    }

    for (int y = 0; y < field_height; ++y) {
        for (int x = 0; x < field_width; ++x) {
            const int dst_idx = zone_idx (x, y, field_width);

            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    if (ox != 0 && oy != 0)
                        continue;

                    const int src_x = x + ox;
                    const int src_y = y + oy;
                    if (src_x < 0 || src_x >= field_width || src_y < 0
                        || src_y >= field_height)
                        continue;

                    const int src_idx = zone_idx (src_x, src_y, field_width);
                    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (
                      spot_subs[dst_idx], topics[src_idx].c_str ()));
                }
            }
        }
    }

    for (int i = 0; i < zone_count; ++i)
        TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (spot_subs[i], false));

    msleep (sub_propagation_ms);

    const int sample_coords[][2] = {{0, 0},
                                    {field_width - 1, 0},
                                    {0, field_height - 1},
                                    {field_width - 1, field_height - 1},
                                    {field_width / 2, field_height / 2},
                                    {field_width / 4, field_height / 4},
                                    {field_width / 5, (field_height * 2) / 5},
                                    {(field_width * 33) / 100,
                                     (field_height * 77) / 100},
                                    {(field_width * 44) / 100,
                                     (field_height * 55) / 100},
                                    {(field_width * 7) / 10,
                                     (field_height * 3) / 10},
                                    {(field_width * 88) / 100,
                                     (field_height * 11) / 100},
                                    {(field_width * 95) / 100,
                                     (field_height * 95) / 100}};
    const size_t sample_count = sizeof (sample_coords) / sizeof (sample_coords[0]);

    for (size_t i = 0; i < sample_count; ++i) {
        const int src_idx =
          zone_idx (sample_coords[i][0], sample_coords[i][1], field_width);
        const int src_x = src_idx % field_width;
        const int src_y = src_idx / field_width;

        zlink_msg_t part;
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, sizeof (int)));
        memcpy (zlink_msg_data (&part), &src_idx, sizeof (int));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_publish (spot_pubs[src_idx], topics[src_idx].c_str (),
                              &part, 1, 0));

        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                if (ox != 0 && oy != 0)
                    continue;

                const int dst_x = src_x + ox;
                const int dst_y = src_y + oy;
                if (dst_x < 0 || dst_x >= field_width || dst_y < 0
                    || dst_y >= field_height)
                    continue;

                const int dst_idx = zone_idx (dst_x, dst_y, field_width);
                TEST_ASSERT_TRUE (wait_for_spot_message (
                  spot_subs[dst_idx], topics[src_idx].c_str (),
                  (const char *) &src_idx, sizeof (int), recv_timeout_ms));
            }
        }
    }

    for (int i = 0; i < zone_count; ++i)
        TEST_ASSERT_SUCCESS_ERRNO (
          destroy_spot_pub_sub (&spot_pubs[i], &spot_subs[i]));

    for (int i = 0; i < spot_node_count; ++i)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&nodes[i]));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
