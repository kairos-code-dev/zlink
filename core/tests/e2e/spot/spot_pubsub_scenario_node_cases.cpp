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
    void *sub = create_spot_sub_handle (sub_node, &queued_spot_handler);
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
    TEST_ASSERT_TRUE (wait_for_spot_message (
      sub, "mix:direct", "direct-msg", 10, 3000));

    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_publish, pub, "mix:visibility", "before-unsub", 0));
    TEST_ASSERT_TRUE (wait_for_spot_message (
      sub, "mix:visibility", "before-unsub", 12, 3000));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_unset_subscription (sub, "mix:visibility"));
    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_publish, pub, "mix:visibility", "after-unsub", 0));
    TEST_ASSERT_FALSE (wait_for_spot_message (
      sub, "mix:visibility", "after-unsub", 11, 200));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "mix:visibility"));
    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_publish, pub, "mix:visibility", "after-resub", 0));
    TEST_ASSERT_TRUE (wait_for_spot_message (
      sub, "mix:visibility", "after-resub", 11, 3000));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_unset_subscription (sub, "mix:direct"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_unset_subscription (sub, "mix:visibility"));

    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
