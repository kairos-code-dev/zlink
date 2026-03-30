/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_pubsub_scenario_shared.hpp"

#include <string.h>
#include <thread>

void test_spot_peer_ipc ()
{
#if !defined(ZLINK_HAVE_IPC)
    TEST_IGNORE_MESSAGE ("IPC not compiled");
    return;
#else
    if (!zlink_has ("ipc")) {
        TEST_IGNORE_MESSAGE ("IPC not available");
        return;
    }
    run_spot_peer_transport_test (peer_transport_ipc);
#endif
}

void test_spot_peer_tcp ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }
    run_spot_peer_transport_test (peer_transport_tcp);
}

void test_spot_peer_ws ()
{
    if (!zlink_has ("ws")) {
        TEST_IGNORE_MESSAGE ("WS not available");
        return;
    }
    run_spot_peer_transport_test (peer_transport_ws);
}

void test_spot_peer_tls ()
{
    if (!zlink_has ("tls")) {
        TEST_IGNORE_MESSAGE ("TLS not available");
        return;
    }
    run_spot_peer_transport_test (peer_transport_tls);
}

void test_spot_peer_wss ()
{
    if (!zlink_has ("wss")) {
        TEST_IGNORE_MESSAGE ("WSS not available");
        return;
    }
    run_spot_peer_transport_test (peer_transport_wss);
}

void test_spot_child_handles_reject_tls_configuration ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = create_spot_node (ctx, "spot-child-tls-policy");
    TEST_ASSERT_NOT_NULL (node);

    void *pub = create_spot_pub_handle (node);
    void *sub = create_spot_sub_handle (node, NULL);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_set_tls_server (pub, "server.crt", "server.key", 0));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_set_tls_client (pub, "ca.crt", "localhost", 0));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_set_tls_client (sub, "ca.crt", "localhost", 0));
    TEST_ASSERT_EQUAL_INT (ENOTSUP, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_unified_wss_subscription_ready_first_delivery ()
{
    if (!zlink_has ("wss")) {
        TEST_IGNORE_MESSAGE ("WSS not available");
        return;
    }

    tls_test_files_t files = make_tls_test_files ();
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_spot_node (ctx, "spot-wss-ready");
    void *sub_node = create_spot_node (ctx, "spot-wss-ready");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);
    void *pub = create_spot_pub_handle (pub_node);
    void *sub = create_spot_sub_handle (sub_node, &queued_spot_handler);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_tls_server (pub_node, files.server_cert.c_str (),
                            files.server_key.c_str (), 0));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_tls_client (
      sub_node, files.ca_cert.c_str (), "localhost", 0));

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = 35200;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "wss://localhost:", &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));

    service_monitor_probe_t sub_monitor_probe;
    void *sub_monitor = open_spot_node_monitor_with_probe (
      sub_node, ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED
        | ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED
        | ZLINK_MONITOR_EVENT_ERROR,
      &sub_monitor_probe);
    TEST_ASSERT_NOT_NULL (sub_monitor);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "wss:ready:first-delivery"));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, 10000));
    TEST_ASSERT_TRUE (wait_for_service_event (
      &sub_monitor_probe, ZLINK_SPOT_MONITOR_EVENT_SUBSCRIPTION_READY_CHANGED,
      endpoint, 10000));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      pub_node, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_SEND_READY, 1,
      10000));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      sub_node, ZLINK_SPOT_ROLE_SUB, ZLINK_MONITOR_STATE_READY, 1, 10000));

    TEST_ASSERT_SUCCESS_ERRNO (publish_text (
      &zlink_publish, pub, "wss:ready:first-delivery", "wss-ready", 0));
    TEST_ASSERT_TRUE (wait_for_spot_message (
      sub, "wss:ready:first-delivery", "wss-ready", 9, 5000));

    TEST_ASSERT_SUCCESS_ERRNO (close_service_monitor_with_probe (&sub_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    cleanup_tls_test_files (files);
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
    void *sub_c = create_spot_sub_handle (node_c, &queued_spot_handler);
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
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      node_a, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_SEND_READY, 1, 5000));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      node_c, ZLINK_SPOT_ROLE_SUB, ZLINK_MONITOR_STATE_READY, 1, 5000));

    bool got_a = false;
    for (int i = 0; i < 10 && !got_a; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (
          publish_text (&zlink_publish, pub_a, "multi:topic", "from-a", 0));
        got_a = wait_for_spot_message (sub_c, "multi:topic", "from-a", 6, 300);
    }
    TEST_ASSERT_TRUE (got_a);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_disconnect_peer (node_c, "inproc://pub-a"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (node_c, "inproc://pub-b"));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      node_b, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_SEND_READY, 1, 5000));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      node_c, ZLINK_SPOT_ROLE_SUB, ZLINK_MONITOR_STATE_READY, 1, 5000));

    bool got_b = false;
    for (int i = 0; i < 10 && !got_b; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (
          publish_text (&zlink_publish, pub_b, "multi:topic", "from-b", 0));
        got_b = wait_for_spot_message (sub_c, "multi:topic", "from-b", 6, 300);
    }
    TEST_ASSERT_TRUE (got_b);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_unset_subscription (sub_c, "multi:topic"));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&node_c));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_same_handle_concurrent_publish ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = create_spot_node (ctx, "spot-concurrent-pub");
    void *sub_node = create_spot_node (ctx, "spot-concurrent-pub");
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);
    void *pub = create_spot_pub_handle (pub_node);
    void *sub = create_spot_sub_handle (sub_node, &queued_spot_handler);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    char endpoint[MAX_SOCKET_STRING];
    int port_seed = 35320;
    TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
      pub_node, "tcp://127.0.0.1:", &port_seed, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer (sub_node, endpoint));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "same:topic"));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      pub_node, ZLINK_SPOT_ROLE_PUB, ZLINK_MONITOR_STATE_SEND_READY, 1, 5000));
    TEST_ASSERT_TRUE (wait_for_spot_node_ready_state (
      sub_node, ZLINK_SPOT_ROLE_SUB, ZLINK_MONITOR_STATE_READY, 1, 5000));

    std::thread t1 ([&]() {
        TEST_ASSERT_SUCCESS_ERRNO (
          publish_text (&zlink_publish, pub, "same:topic", "a", 0));
    });
    std::thread t2 ([&]() {
        TEST_ASSERT_SUCCESS_ERRNO (
          publish_text (&zlink_publish, pub, "same:topic", "b", 0));
    });
    t1.join ();
    t2.join ();

    const bool got_a = wait_for_spot_message (sub, "same:topic", "a", 1, 3000);
    const bool got_b = wait_for_spot_message (sub, "same:topic", "b", 1, 3000);
    TEST_ASSERT_TRUE (got_a || got_b);

    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_node_with_handles (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
