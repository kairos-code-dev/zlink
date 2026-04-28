/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_pubsub_scenario_shared.hpp"

#include <string.h>

void test_spot_peer_tcp ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }
    run_spot_peer_tcp_test ();
}

void test_spot_peer_tcp_reverse_publish ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }
    run_spot_peer_reverse_tcp_test ();
}

void test_spot_multi_publisher ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_a = create_spot_node (ctx, "spot-multi-pub");
    void *node_b = create_spot_node (ctx, "spot-multi-pub");
    void *node_c = create_spot_node (ctx, "spot-multi-pub");
    TEST_ASSERT_NOT_NULL (node_a);
    TEST_ASSERT_NOT_NULL (node_b);
    TEST_ASSERT_NOT_NULL (node_c);
    void *pub_a = create_spot_pub_handle (node_a);
    void *pub_b = create_spot_pub_handle (node_b);
    void *sub_c = create_spot_sub_handle (node_c);
    TEST_ASSERT_NOT_NULL (pub_a);
    TEST_ASSERT_NOT_NULL (pub_b);
    TEST_ASSERT_NOT_NULL (sub_c);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, "inproc://pub-a"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, "inproc://pub-b"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_c, "inproc://sub-c"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (node_c, "inproc://pub-a"));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub_c, "multi:topic"));

    bool got_a = false;
    for (int i = 0; i < 10 && !got_a; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (
          publish_text (&zlink_publish, pub_a, "multi:topic", "from-a", 0));
        got_a =
          wait_for_spot_recv_message (sub_c, "multi:topic", "from-a", 6, 300);
    }
    TEST_ASSERT_TRUE (got_a);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_disconnect_peer (node_c, "inproc://pub-a"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (node_c, "inproc://pub-b"));

    bool got_b = false;
    for (int i = 0; i < 10 && !got_b; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (
          publish_text (&zlink_publish, pub_b, "multi:topic", "from-b", 0));
        got_b =
          wait_for_spot_recv_message (sub_c, "multi:topic", "from-b", 6, 300);
    }
    TEST_ASSERT_TRUE (got_b);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_unset_subscription (sub_c, "multi:topic"));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&node_c));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_aggregate_subscription_refcount ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_a = create_spot_node (ctx, "spot-aggregate-refcount");
    void *node_b = create_spot_node (ctx, "spot-aggregate-refcount");
    TEST_ASSERT_NOT_NULL (node_a);
    TEST_ASSERT_NOT_NULL (node_b);
    void *pub = create_spot_pub_handle (node_a);
    void *sub_one = zlink_spot_new (node_b);
    void *sub_two = zlink_spot_new (node_b);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub_one);
    TEST_ASSERT_NOT_NULL (sub_two);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, "inproc://agg-a"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, "inproc://agg-b"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (node_b, "inproc://agg-a"));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub_one, "aggregate:topic"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub_two, "aggregate:topic"));

    bool warmed = false;
    for (int i = 0; i < 10 && !warmed; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (
          publish_text (&zlink_publish, pub, "aggregate:topic", "warm", 0));
        warmed =
          wait_for_spot_recv_message (sub_two, "aggregate:topic", "warm", 4, 300);
    }
    TEST_ASSERT_TRUE (warmed);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_unset_subscription (sub_one, "aggregate:topic"));

    bool delivered_after_first_unsubscribe = false;
    for (int i = 0; i < 10 && !delivered_after_first_unsubscribe; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (
          publish_text (&zlink_publish, pub, "aggregate:topic", "after", 0));
        delivered_after_first_unsubscribe = wait_for_spot_recv_message (
          sub_two, "aggregate:topic", "after", 5, 300);
    }
    TEST_ASSERT_TRUE (delivered_after_first_unsubscribe);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_unset_subscription (sub_two, "aggregate:topic"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_two));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_one));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
