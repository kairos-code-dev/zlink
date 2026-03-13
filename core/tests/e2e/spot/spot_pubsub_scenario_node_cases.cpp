/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_pubsub_scenario_shared.hpp"

#include <thread>

struct node_publish_probe_t
{
    void *node;
    const char *topic;
    const char *payload;
    size_t payload_size;
    std::atomic<int> rc;
    std::atomic<int> err;
};

static void spot_node_publish_worker (node_publish_probe_t *probe_)
{
    if (!probe_)
        return;

    const int rc = publish_text (&zlink_spot_node_publish, probe_->node,
                                 probe_->topic, probe_->payload, 0);
    probe_->rc.store (rc);
    probe_->err.store (rc == 0 ? 0 : zlink_errno ());
}

void test_spot_node_direct_local_and_child_interop ()
{
    step_log ("node_child_interop: create ctx");
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    step_log ("node_child_interop: create node");
    void *node = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node);

    step_log ("node_child_interop: create child handles");
    void *child_pub = create_spot_handle (node, &ignore_spot_handler);
    void *child_sub = create_spot_handle (node, &queued_spot_handler);
    TEST_ASSERT_NOT_NULL (child_pub);
    TEST_ASSERT_NOT_NULL (child_sub);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_subscribe (node, "mix:direct"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_subscribe (node, "mix:child"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (child_sub, "mix:direct"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (child_sub, "mix:child"));
    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (node, true));
    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (child_sub, false));
    msleep (50);

    step_log ("node_child_interop: publish");
    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_spot_publish, child_pub, "mix:child", "child-msg", 0));
    step_log ("node_child_interop: wait node recv");
    TEST_ASSERT_TRUE (wait_for_node_message (node, "mix:child", "child-msg", 9,
                                             1000));
    step_log ("node_child_interop: wait child recv");
    TEST_ASSERT_TRUE (
      wait_for_spot_message (child_sub, "mix:child", "child-msg", 9, 1000));

    step_log ("node_child_interop: visibility subscribe");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_subscribe (node, "mix:visibility"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_subscribe (child_sub, "mix:visibility"));
    msleep (50);

    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_spot_publish, child_pub, "mix:visibility", "before-unsub", 0));
    TEST_ASSERT_TRUE (wait_for_node_message (
      node, "mix:visibility", "before-unsub", 12, 1000));
    TEST_ASSERT_TRUE (wait_for_spot_message (
      child_sub, "mix:visibility", "before-unsub", 12, 1000));

    step_log ("node_child_interop: visibility unsubscribe");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_unsubscribe (node, "mix:visibility"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_unsubscribe (child_sub, "mix:visibility"));
    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_spot_publish, child_pub, "mix:visibility", "after-unsub", 0));
    TEST_ASSERT_FALSE (wait_for_node_message (
      node, "mix:visibility", "after-unsub", 11, 200));
    TEST_ASSERT_FALSE (wait_for_spot_message (
      child_sub, "mix:visibility", "after-unsub", 11, 200));

    step_log ("node_child_interop: visibility resubscribe");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_subscribe (node, "mix:visibility"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_subscribe (child_sub, "mix:visibility"));
    msleep (50);
    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_spot_publish, child_pub, "mix:visibility", "after-resub", 0));
    TEST_ASSERT_TRUE (wait_for_node_message (
      node, "mix:visibility", "after-resub", 11, 1000));
    TEST_ASSERT_TRUE (wait_for_spot_message (
      child_sub, "mix:visibility", "after-resub", 11, 1000));

    step_log ("node_child_interop: unsubscribe and reset handlers");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_unsubscribe (node, "mix:direct"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_unsubscribe (node, "mix:child"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_unsubscribe (node, "mix:visibility"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_unsubscribe (child_sub, "mix:direct"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_unsubscribe (child_sub, "mix:child"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_unsubscribe (child_sub, "mix:visibility"));
    msleep (50);

    step_log ("node_child_interop: destroy child_sub");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&child_sub));
    step_log ("node_child_interop: destroy child_pub");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&child_pub));
    step_log ("node_child_interop: destroy node");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    step_log ("node_child_interop: ctx term");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_direct_remote_peer_mesh ()
{
    step_log ("node_remote_mesh: create ctx");
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    step_log ("node_remote_mesh: create nodes");
    void *node_a = create_spot_node (ctx, "spot-test");
    void *node_b = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node_a);
    TEST_ASSERT_NOT_NULL (node_b);

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = 22103;
    step_log ("node_remote_mesh: bind and connect");
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      node_a, "tcp://127.0.0.1:", &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (node_b, endpoint));

    step_log ("node_remote_mesh: subscribe");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_subscribe (node_b, "mesh:direct"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_subscribe (node_b, "mesh:child"));
    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (node_b, true));
    msleep (250);

    step_log ("node_remote_mesh: publish direct");
    bool got_direct = false;
    for (int i = 0; i < 15 && !got_direct; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (publish_text (
          &zlink_spot_node_publish, node_a, "mesh:direct", "from-node", 0));
        got_direct =
          wait_for_node_message (node_b, "mesh:direct", "from-node", 9, 100);
    }
    TEST_ASSERT_TRUE (got_direct);

    step_log ("node_remote_mesh: create child pub");
    void *child_pub = create_spot_handle (node_a, &ignore_spot_handler);
    TEST_ASSERT_NOT_NULL (child_pub);
    step_log ("node_remote_mesh: publish child");
    bool got_child = false;
    for (int i = 0; i < 15 && !got_child; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (publish_text (
          &zlink_spot_publish, child_pub, "mesh:child", "from-child", 0));
        got_child =
          wait_for_node_message (node_b, "mesh:child", "from-child", 10, 100);
    }
    TEST_ASSERT_TRUE (got_child);

    step_log ("node_remote_mesh: destroy child");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&child_pub));
    step_log ("node_remote_mesh: destroy node_b");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    step_log ("node_remote_mesh: destroy node_a");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    step_log ("node_remote_mesh: term ctx");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_direct_sub_option_inheritance_and_handler_conflict ()
{
    step_log ("sub_option_inheritance: create ctx");
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    step_log ("sub_option_inheritance: create node");
    void *node = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node);
    step_log ("sub_option_inheritance: create child");
    void *child_old = create_spot_handle (node, &ignore_spot_handler);
    TEST_ASSERT_NOT_NULL (child_old);

    step_log ("sub_option_inheritance: destroy child");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&child_old));
    step_log ("sub_option_inheritance: destroy node");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    step_log ("sub_option_inheritance: term ctx");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_direct_first_publish_race ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node);

    void *child_sub = create_spot_handle (node, &ignore_spot_handler);
    TEST_ASSERT_NOT_NULL (child_sub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (child_sub, "race:topic"));
    msleep (50);

    node_publish_probe_t probe_a;
    probe_a.node = node;
    probe_a.topic = "race:topic";
    probe_a.payload = "a";
    probe_a.payload_size = 1;
    probe_a.rc.store (-999);
    probe_a.err.store (0);

    node_publish_probe_t probe_b;
    probe_b.node = node;
    probe_b.topic = "race:topic";
    probe_b.payload = "b";
    probe_b.payload_size = 1;
    probe_b.rc.store (-999);
    probe_b.err.store (0);

    std::thread worker_a (spot_node_publish_worker, &probe_a);
    std::thread worker_b (spot_node_publish_worker, &probe_b);
    worker_a.join ();
    worker_b.join ();

    TEST_ASSERT_EQUAL_INT (0, probe_a.rc.load ());
    TEST_ASSERT_EQUAL_INT (0, probe_a.err.load ());
    TEST_ASSERT_EQUAL_INT (0, probe_b.rc.load ());
    TEST_ASSERT_EQUAL_INT (0, probe_b.err.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&child_sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
