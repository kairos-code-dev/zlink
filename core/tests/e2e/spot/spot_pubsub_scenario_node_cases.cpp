/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_pubsub_scenario_shared.hpp"

#include <thread>

struct node_publish_probe_t
{
    void *node;
    const char *topic;
    const char *payload;
    std::atomic<int> rc;
    std::atomic<int> err;
};

static void spot_node_publish_worker (node_publish_probe_t *probe_)
{
    if (!probe_)
        return;

    const int rc =
      publish_text (&zlink_publish, probe_->node, probe_->topic,
                    probe_->payload, 0);
    probe_->rc.store (rc);
    probe_->err.store (rc == 0 ? 0 : zlink_errno ());
}

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
      pub_node, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_SEND_READY, 1, 5000));
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

void test_spot_node_direct_remote_peer_mesh ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_a = create_spot_node (ctx, "spot-node-mesh");
    void *node_b = create_spot_node (ctx, "spot-node-mesh");
    TEST_ASSERT_NOT_NULL (node_a);
    TEST_ASSERT_NOT_NULL (node_b);
    void *pub = create_spot_pub_handle (node_a);
    void *sub = create_spot_sub_handle (node_b, &queued_spot_handler);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = 22140;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      node_a, "tcp://127.0.0.1:", &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (node_b, endpoint));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "mesh:direct"));
    msleep (100);

    bool got_direct = false;
    for (int i = 0; i < 15 && !got_direct; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (publish_text (
          &zlink_publish, pub, "mesh:direct", "from-node", 0));
        got_direct =
          wait_for_spot_message (sub, "mesh:direct", "from-node", 9, 200);
    }
    TEST_ASSERT_TRUE (got_direct);

    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_direct_sub_option_inheritance_and_handler_conflict ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    const int rcvhwm = 32;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (node, ZLINK_OPT_RCVHWM, &rcvhwm, sizeof (rcvhwm)));

    int observed = 0;
    size_t observed_size = sizeof (observed);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_option (node, ZLINK_OPT_RCVHWM, &observed, &observed_size));
    TEST_ASSERT_EQUAL_INT (rcvhwm, observed);

    void *sub = create_spot_sub_handle (node, NULL);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (sub, &ignore_spot_handler, NULL));
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_subscribe_handler (sub, &ignore_spot_handler, NULL));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_direct_first_publish_race ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_spot_node (ctx, "spot-node-race");
    void *sub_node = create_spot_node (ctx, "spot-node-race");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);
    void *pub = create_spot_pub_handle (pub_node);
    void *sub = create_spot_sub_handle (sub_node, &queued_spot_handler);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = 22180;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "race:topic"));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      pub_node, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_SEND_READY, 1, 5000));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      sub_node, ZLINK_SPOT_ROLE_SUB, ZLINK_MONITOR_STATE_READY, 1, 5000));

    node_publish_probe_t probe_a;
    probe_a.node = pub;
    probe_a.topic = "race:topic";
    probe_a.payload = "a";
    probe_a.rc.store (-999);
    probe_a.err.store (0);

    node_publish_probe_t probe_b;
    probe_b.node = pub;
    probe_b.topic = "race:topic";
    probe_b.payload = "b";
    probe_b.rc.store (-999);
    probe_b.err.store (0);

    std::thread worker_a (spot_node_publish_worker, &probe_a);
    std::thread worker_b (spot_node_publish_worker, &probe_b);
    worker_a.join ();
    worker_b.join ();

    TEST_ASSERT_EQUAL_INT (0, probe_a.rc.load ());
    TEST_ASSERT_EQUAL_INT (0, probe_b.rc.load ());
    TEST_ASSERT_TRUE (
      wait_for_spot_message (sub, "race:topic", "a", 1, 3000)
      || wait_for_spot_message (sub, "race:topic", "b", 1, 3000));

    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
