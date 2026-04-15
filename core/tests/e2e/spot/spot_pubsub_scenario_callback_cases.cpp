/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_pubsub_scenario_shared.hpp"

#include <string.h>

void test_spot_sub_handler_basic ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_spot_node (ctx, "spot-callback-basic");
    void *sub_node = create_spot_node (ctx, "spot-callback-basic");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);
    void *pub = create_spot_pub_handle (pub_node);
    void *sub = create_spot_sub_handle (sub_node, NULL);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = 33140;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "zone:auto:test"));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      pub_node, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_READY, 1, 5000));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      sub_node, ZLINK_SPOT_ROLE_SUB, ZLINK_MONITOR_STATE_READY, 1, 5000));

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "ping", 4);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_publish (pub, "zone:auto:test", &part, 1, 0));

    TEST_ASSERT_TRUE (wait_for_spot_recv_message (
      sub, "zone:auto:test", "ping", 4, 3000));

    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_recv_callback_isolated_by_handle ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_spot_node (ctx, "spot-callback-isolation");
    void *sub_a = create_spot_node (ctx, "spot-callback-isolation");
    void *sub_b = create_spot_node (ctx, "spot-callback-isolation");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_a);
    TEST_ASSERT_NOT_NULL (sub_b);
    void *pub = create_spot_pub_handle (pub_node);
    void *sub_a_handle = create_spot_sub_handle (sub_a, NULL);
    void *sub_b_handle = create_spot_sub_handle (sub_b, NULL);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub_a_handle);
    TEST_ASSERT_NOT_NULL (sub_b_handle);

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = 33180;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_a, endpoint));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub_a_handle, "zone:auto:test"));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      pub_node, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_READY, 1, 5000));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      sub_a, ZLINK_SPOT_ROLE_SUB, ZLINK_MONITOR_STATE_READY, 1, 5000));

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_publish, pub, "zone:auto:test", "ping", 0));
    TEST_ASSERT_TRUE (wait_for_spot_recv_message (
      sub_a_handle, "zone:auto:test", "ping", 4, 3000));
    TEST_ASSERT_FALSE (wait_for_spot_recv_message (
      sub_b_handle, "zone:auto:test", "ping", 4, 200));

    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&sub_b));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&sub_a));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
