/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_pubsub_scenario_shared.hpp"

void test_spot_sub_delivery_ready_immediate_first_publish ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_spot_node (ctx, "spot-delivery-ready");
    void *sub_node = create_spot_node (ctx, "spot-delivery-ready");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);
    void *pub = create_spot_pub_handle (pub_node);
    void *sub = create_spot_sub_handle (sub_node, &queued_spot_handler);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = 35380;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));

    service_monitor_probe_t sub_monitor_probe;
    void *sub_monitor = open_spot_node_monitor_with_probe (
      sub_node, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED
        | ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED
        | ZLINK_MONITOR_EVENT_ERROR,
      &sub_monitor_probe);
    TEST_ASSERT_NOT_NULL (sub_monitor);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "delivery:topic"));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 5000));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_monitor_probe, ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED,
      endpoint, 5000));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      pub_node, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_SEND_READY, 1, 5000));

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_publish, pub, "delivery:topic", "first", 0));
    TEST_ASSERT_TRUE (wait_for_spot_message (
      sub, "delivery:topic", "first", 5, 3000));

    TEST_ASSERT_SUCCESS_ERRNO (close_service_monitor_with_probe (&sub_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
