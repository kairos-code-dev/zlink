/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_pubsub_scenario_shared.hpp"

void test_spot_node_direct_local_and_child_interop ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_spot_node (ctx, "spot-node-direct");
    void *sub_node = create_spot_node (ctx, "spot-node-direct");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);
    void *pub = create_spot_pub_handle (pub_node);
    void *sub = create_spot_sub_handle (sub_node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = 22100;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "mix:direct"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "mix:visibility"));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      pub_node, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_READY, 1, 5000));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      sub_node, ZLINK_SPOT_ROLE_SUB, ZLINK_MONITOR_STATE_READY, 1, 5000));

    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_publish, pub, "mix:direct", "direct-msg", 0));
    TEST_ASSERT_TRUE (wait_for_spot_recv_message (
      sub, "mix:direct", "direct-msg", 10, 3000));

    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_publish, pub, "mix:visibility", "before-unsub", 0));
    TEST_ASSERT_TRUE (wait_for_spot_recv_message (
      sub, "mix:visibility", "before-unsub", 12, 3000));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_unset_subscription (sub, "mix:visibility"));
    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_publish, pub, "mix:visibility", "after-unsub", 0));
    TEST_ASSERT_FALSE (wait_for_spot_recv_message (
      sub, "mix:visibility", "after-unsub", 11, 200));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "mix:visibility"));
    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_publish, pub, "mix:visibility", "after-resub", 0));
    TEST_ASSERT_TRUE (wait_for_spot_recv_message (
      sub, "mix:visibility", "after-resub", 11, 3000));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_unset_subscription (sub, "mix:direct"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_unset_subscription (sub, "mix:visibility"));

    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_pub_ingress_local_spot_subscribe_surface ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);
    void *pub = zlink_socket (ctx, ZLINK_SOCKET_PUB);
    TEST_ASSERT_NOT_NULL (pub);

    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (node, ZLINK_OPT_LINGER, &linger, sizeof (linger)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (spot, ZLINK_OPT_LINGER, &linger, sizeof (linger)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (pub, ZLINK_OPT_LINGER, &linger, sizeof (linger)));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (spot, "spot:ingress-local"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_attach_pub_ingress (node, pub));

    bool received = false;
    for (int attempt = 0; attempt < 100 && !received; ++attempt) {
        TEST_ASSERT_SUCCESS_ERRNO (publish_text (
          &zlink_publish, pub, "spot:ingress-local", "ingress-local", 0));

        zlink_routing_id_t source_rid;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        char service_name[16] = {0};
        size_t service_name_len = sizeof (service_name);
        char topic[64] = {0};
        size_t topic_len = sizeof (topic);
        memset (&source_rid, 0, sizeof (source_rid));

        const zlink_recv_result_t rc = zlink_spot_subscribe (
          spot, &source_rid, &parts, &part_count, service_name,
          &service_name_len, topic, &topic_len,
          static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT));
        if (rc == ZLINK_RECV_NO_DATA && errno == EAGAIN) {
            msleep (10);
            continue;
        }

        TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK, rc);
        TEST_ASSERT_EQUAL_UINT (0u, static_cast<unsigned int> (service_name_len));
        TEST_ASSERT_EQUAL_UINT (
          strlen ("spot:ingress-local"), static_cast<unsigned int> (topic_len));
        TEST_ASSERT_EQUAL_STRING_LEN ("spot:ingress-local", topic, topic_len);
        TEST_ASSERT_EQUAL_UINT (1u, static_cast<unsigned int> (part_count));
        TEST_ASSERT_EQUAL_UINT (
          strlen ("ingress-local"),
          static_cast<unsigned int> (zlink_msg_size (&parts[0])));
        TEST_ASSERT_EQUAL_MEMORY ("ingress-local", zlink_msg_data (&parts[0]),
                                  strlen ("ingress-local"));
        zlink_multipart_close (parts, part_count);
        received = true;
    }

    TEST_ASSERT_TRUE (received);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
