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

void test_spot_unified_wss_subscription_ready_first_delivery ()
{
    if (!zlink_has ("wss")) {
        TEST_IGNORE_MESSAGE ("WSS not available");
        return;
    }

    const int iteration_count = 1;
    const char *const topic = "wss:ready:first-delivery";
    const char *const payload = "wss-ready";
    tls_test_files_t files = make_tls_test_files ();
    int port_seed = 35200;

    for (int iteration = 0; iteration < iteration_count; ++iteration) {
        step_log ("spot unified wss ready delivery: create ctx");
        void *ctx = zlink_ctx_new ();
        TEST_ASSERT_NOT_NULL (ctx);

        void *pub_node = zlink_spot_node_new (ctx, "perf-spot");
        void *sub_node = zlink_spot_node_new (ctx, "perf-spot-client");
        TEST_ASSERT_NOT_NULL (pub_node);
        TEST_ASSERT_NOT_NULL (sub_node);
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_recv_spot_handler (pub_node, &ignore_spot_handler, NULL));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_recv_spot_handler (sub_node, &ignore_spot_handler, NULL));

        const int linger = 0;
        const int sndhwm = 1000;
        const int rcvhwm = 1000;
        const int sndtimeo_ms = 200;
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_set_pub_option (pub_node, ZLINK_SPOT_PUB_OPT_LINGER,
                                          &linger, sizeof (linger)));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_set_pub_option (pub_node, ZLINK_SPOT_PUB_OPT_SNDHWM,
                                          &sndhwm, sizeof (sndhwm)));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_set_pub_option (pub_node, ZLINK_SPOT_PUB_OPT_SNDTIMEO,
                                          &sndtimeo_ms,
                                          sizeof (sndtimeo_ms)));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_set_sub_option (sub_node, ZLINK_SPOT_SUB_OPT_LINGER,
                                          &linger, sizeof (linger)));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_set_sub_option (sub_node, ZLINK_SPOT_SUB_OPT_RCVHWM,
                                          &rcvhwm, sizeof (rcvhwm)));

        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_set_tls_server (pub_node, files.server_cert.c_str (),
                                          files.server_key.c_str ()));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_set_tls_client (
          sub_node, files.ca_cert.c_str (), "localhost", 0));

        step_log ("spot unified wss ready delivery: create handles");
        void *pub = create_spot_handle (pub_node, &ignore_spot_handler);
        void *sub = create_spot_handle (sub_node, &queued_spot_handler);
        TEST_ASSERT_NOT_NULL (pub);
        TEST_ASSERT_NOT_NULL (sub);
        TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (sub, false));

        service_monitor_probe_t monitor_probe;
        void *sub_monitor = open_spot_monitor_with_probe (
          sub, ZLINK_SPOT_ROLE_SUB,
          ZLINK_SPOT_SUB_FILTER_APPLIED
            | ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED
            | ZLINK_MONITOR_EVENT_ERROR,
          &monitor_probe);
        TEST_ASSERT_NOT_NULL (sub_monitor);

        char endpoint[MAX_SOCKET_STRING] = {0};
        step_log ("spot unified wss ready delivery: bind/connect");
        TEST_ASSERT_SUCCESS_ERRNO (bind_spot_node_with_port_seed (
          pub_node, "wss://localhost:", &port_seed, endpoint));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_connect_peer_pub (sub_node, endpoint));

        step_log ("spot unified wss ready delivery: subscribe and wait");
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub, topic));
        TEST_ASSERT_TRUE (wait_for_spot_pub_peers (pub, 5000));
        TEST_ASSERT_TRUE (wait_for_spot_sub_peers (sub, 5000));
        TEST_ASSERT_TRUE (wait_for_service_event_match (
          &monitor_probe, ZLINK_SPOT_SUB_FILTER_APPLIED, NULL, topic, -1, NULL,
          3000));
        TEST_ASSERT_TRUE (wait_for_service_event_match (
          &monitor_probe, ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED, endpoint,
          topic, 1, NULL, 10000));

        step_log ("spot unified wss ready delivery: publish immediately");
        TEST_ASSERT_SUCCESS_ERRNO (
          publish_text (&zlink_spot_publish, pub, topic, payload, 0));
        TEST_ASSERT_TRUE (
          wait_for_spot_message (sub, topic, payload, strlen (payload), 3000));

        TEST_ASSERT_SUCCESS_ERRNO (
          close_service_monitor_with_probe (&sub_monitor));
        remove_queued_spot_probe (sub, false);
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    }

    cleanup_tls_test_files (files);
}

void test_spot_multi_publisher ()
{
    step_log ("multi_publisher: create ctx");
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    step_log ("multi_publisher: create nodes");
    void *node_a = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node_b);
    void *node_c = create_spot_node (ctx, "spot-test");
    TEST_ASSERT_NOT_NULL (node_c);

    step_log ("multi_publisher: bind nodes");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, "inproc://pub-a"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, "inproc://pub-b"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_c, "inproc://sub-c"));

    step_log ("multi_publisher: connect peers");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (node_c, "inproc://pub-a"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (node_c, "inproc://pub-b"));

    void *spot_c_pub = NULL;
    void *spot_c_sub = NULL;
    step_log ("multi_publisher: create node_c child handles");
    TEST_ASSERT_SUCCESS_ERRNO (
      create_spot_pub_sub (node_c, &spot_c_pub, &spot_c_sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot_c_sub, "multi:topic"));
    TEST_ASSERT_NOT_NULL (ensure_queued_spot_probe (spot_c_sub, false));

    msleep (250);

    void *spot_a_pub = NULL;
    void *spot_a_sub = NULL;
    step_log ("multi_publisher: create node_a child handles");
    TEST_ASSERT_SUCCESS_ERRNO (
      create_spot_pub_sub (node_a, &spot_a_pub, &spot_a_sub));
    void *spot_b_pub = NULL;
    void *spot_b_sub = NULL;
    step_log ("multi_publisher: create node_b child handles");
    TEST_ASSERT_SUCCESS_ERRNO (
      create_spot_pub_sub (node_b, &spot_b_pub, &spot_b_sub));
    msleep (250);

    zlink_msg_t parts_a[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts_a[0], 5));
    memcpy (zlink_msg_data (&parts_a[0]), "from-a", 5);

    zlink_msg_t parts_b[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts_b[0], 6));
    memcpy (zlink_msg_data (&parts_b[0]), "from-b", 6);

    step_log ("multi_publisher: warmup publish");
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_spot_publish, spot_a_pub, "multi:topic", "warm-a", 0));
    TEST_ASSERT_TRUE (
      wait_for_spot_message (spot_c_sub, "multi:topic", "warm-a", 6, 5000));
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_text (&zlink_spot_publish, spot_b_pub, "multi:topic", "warm-b", 0));
    TEST_ASSERT_TRUE (
      wait_for_spot_message (spot_c_sub, "multi:topic", "warm-b", 6, 5000));

    step_log ("multi_publisher: publish");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_a_pub, "multi:topic", parts_a, 1, 0));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_b_pub, "multi:topic", parts_b, 1, 0));

    step_log ("multi_publisher: wait receives");
    TEST_ASSERT_TRUE (
      wait_for_spot_message (spot_c_sub, "multi:topic", "from-a", 5, 2000));
    TEST_ASSERT_TRUE (
      wait_for_spot_message (spot_c_sub, "multi:topic", "from-b", 6, 2000));

    step_log ("multi_publisher: disconnect peers");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_unsubscribe (spot_c_sub, "multi:topic"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_disconnect_peer_pub (node_c, "inproc://pub-a"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_disconnect_peer_pub (node_c, "inproc://pub-b"));
    msleep (50);

    step_log ("multi_publisher: destroy child handles");
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_pub_sub (&spot_a_pub, &spot_a_sub));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_pub_sub (&spot_b_pub, &spot_b_sub));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_pub_sub (&spot_c_pub, &spot_c_sub));
    step_log ("multi_publisher: destroy nodes");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_c));
    step_log ("multi_publisher: term ctx");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_same_handle_concurrent_publish ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = create_spot_node (ctx, "spot-concurrent");
    TEST_ASSERT_NOT_NULL (node);

    void *sub = create_spot_handle (node, &ignore_spot_handler);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (sub, "concurrent:topic"));

    const int publisher_count = 4;
    const int messages_per_publisher = 32;
    std::vector<int> worker_errno (publisher_count, 0);
    std::vector<int> worker_progress (publisher_count, 0);
    std::vector<std::thread> workers;
    std::mutex start_mutex;
    std::condition_variable start_cv;
    int ready_threads = 0;
    bool start = false;

    for (int i = 0; i < publisher_count; ++i) {
        workers.push_back (std::thread ([&, i] () {
            {
                std::unique_lock<std::mutex> lock (start_mutex);
                ++ready_threads;
                start_cv.notify_all ();
                start_cv.wait (lock, [&start] () { return start; });
            }

            for (int seq = 0; seq < messages_per_publisher; ++seq) {
                char payload[32];
                snprintf (payload, sizeof (payload), "pub-%d-%02d", i, seq);
                if (publish_text (&zlink_spot_node_publish, node,
                                  "concurrent:topic",
                                  payload, 0)
                    != 0) {
                    worker_errno[i] = errno;
                    return;
                }
                worker_progress[i] += 1;
            }
        }));
    }

    {
        std::unique_lock<std::mutex> lock (start_mutex);
        TEST_ASSERT_TRUE (start_cv.wait_for (
          lock, std::chrono::seconds (5),
          [&ready_threads, publisher_count] () {
              return ready_threads == publisher_count;
          }));
        start = true;
        start_cv.notify_all ();
    }

    for (size_t i = 0; i < workers.size (); ++i)
        workers[i].join ();

    for (int i = 0; i < publisher_count; ++i) {
        TEST_ASSERT_EQUAL_INT (messages_per_publisher, worker_progress[i]);
        TEST_ASSERT_EQUAL_INT (0, worker_errno[i]);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
