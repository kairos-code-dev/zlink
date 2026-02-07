/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"
#include "../testutil.hpp"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

void setUp ()
{
}

void tearDown ()
{
}

static void test_spot_local_pubsub ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot, "chat:room1:msg"));

    zlink_msg_t parts[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 5));
    memcpy (zlink_msg_data (&parts[0]), "hello", 5);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot, "chat:room1:msg", parts, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    char topic[256];
    size_t topic_len = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot, &recv_parts, &recv_count, 0, topic, &topic_len));
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count);
    TEST_ASSERT_EQUAL_STRING ("chat:room1:msg", topic);
    TEST_ASSERT_EQUAL_INT (5, (int) zlink_msg_size (&recv_parts[0]));
    TEST_ASSERT_EQUAL_MEMORY ("hello", zlink_msg_data (&recv_parts[0]), 5);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_pattern_subscribe ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe_pattern (spot, "zone:12:*"));

    zlink_msg_t parts[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 4));
    memcpy (zlink_msg_data (&parts[0]), "ping", 4);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot, "zone:12:state", parts, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    char topic[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot, &recv_parts, &recv_count, 0, topic, NULL));
    TEST_ASSERT_EQUAL_STRING ("zone:12:state", topic);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_publish_no_subscribers ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    zlink_msg_t parts[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 3));
    memcpy (zlink_msg_data (&parts[0]), "nop", 3);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot, "metrics:cpu", parts, 1, 0));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_multipart_publish ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot, "mp:topic"));

    zlink_msg_t parts[2];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 3));
    memcpy (zlink_msg_data (&parts[0]), "one", 3);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[1], 3));
    memcpy (zlink_msg_data (&parts[1]), "two", 3);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot, "mp:topic", parts, 2, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot, &recv_parts, &recv_count, 0, NULL, NULL));
    TEST_ASSERT_EQUAL_INT (2, (int) recv_count);
    TEST_ASSERT_EQUAL_MEMORY ("one", zlink_msg_data (&recv_parts[0]), 3);
    TEST_ASSERT_EQUAL_MEMORY ("two", zlink_msg_data (&recv_parts[1]), 3);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_peer_pubsub ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_a = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_b);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, "inproc://spot-a"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, "inproc://spot-b"));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (node_b, "inproc://spot-a"));

    void *spot_b = zlink_spot_new (node_b);
    TEST_ASSERT_NOT_NULL (spot_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot_b, "peer:topic"));

    msleep (50);

    void *spot_a = zlink_spot_new (node_a);
    TEST_ASSERT_NOT_NULL (spot_a);

    zlink_msg_t parts[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 4));
    memcpy (zlink_msg_data (&parts[0]), "pong", 4);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_a, "peer:topic", parts, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    char topic[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot_b, &recv_parts, &recv_count, 0, topic, NULL));
    TEST_ASSERT_EQUAL_STRING ("peer:topic", topic);
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count);
    TEST_ASSERT_EQUAL_MEMORY ("pong", zlink_msg_data (&recv_parts[0]), 4);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

enum peer_transport_t
{
    peer_transport_ipc = 0,
    peer_transport_tcp,
    peer_transport_ws,
    peer_transport_tls,
    peer_transport_wss
};

static void run_spot_peer_transport_test (peer_transport_t transport_)
{
    const bool is_ipc = transport_ == peer_transport_ipc;
    const bool use_tls =
      transport_ == peer_transport_tls || transport_ == peer_transport_wss;

    const char *topic = NULL;
    const char *payload = NULL;
    const char *bind_endpoint = NULL;

    switch (transport_) {
        case peer_transport_ipc:
            topic = "ipc:test";
            payload = "ipc-msg";
            break;
        case peer_transport_tcp:
            topic = "tcp:test";
            payload = "tcp-msg";
            bind_endpoint = "tcp://127.0.0.1:*";
            break;
        case peer_transport_ws:
            topic = "ws:test";
            payload = "ws-msg";
            bind_endpoint = "ws://127.0.0.1:*";
            break;
        case peer_transport_tls:
            topic = "tls:test";
            payload = "tls-msg";
            bind_endpoint = "tls://localhost:*";
            break;
        case peer_transport_wss:
            topic = "wss:test";
            payload = "wss-msg";
            bind_endpoint = "wss://localhost:*";
            break;
        default:
            TEST_FAIL_MESSAGE ("Unknown peer transport");
            return;
    }

    tls_test_files_t files;
    if (use_tls)
        files = make_tls_test_files ();

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_a = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_b);

    char endpoint_a[MAX_SOCKET_STRING] = {0};

    if (is_ipc) {
        char endpoint_b[MAX_SOCKET_STRING];
        make_random_ipc_endpoint (endpoint_a);
        make_random_ipc_endpoint (endpoint_b);

        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, endpoint_a));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, endpoint_b));
    } else {
        if (use_tls) {
            TEST_ASSERT_SUCCESS_ERRNO (
              zlink_spot_node_set_tls_server (node_a, files.server_cert.c_str (),
                                              files.server_key.c_str ()));
        }

        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, bind_endpoint));

        size_t endpoint_len = sizeof (endpoint_a);
        void *pub_socket_a = zlink_spot_node_pub_socket (node_a);
        TEST_ASSERT_NOT_NULL (pub_socket_a);
        TEST_ASSERT_SUCCESS_ERRNO (zlink_getsockopt (
          pub_socket_a, ZLINK_LAST_ENDPOINT, endpoint_a, &endpoint_len));

        if (use_tls) {
            TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_set_tls_client (
              node_b, files.ca_cert.c_str (), "localhost", 0));
            TEST_ASSERT_SUCCESS_ERRNO (
              zlink_spot_node_set_tls_server (node_b, files.server_cert.c_str (),
                                              files.server_key.c_str ()));
        }

        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, bind_endpoint));
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_connect_peer_pub (node_b, endpoint_a));

    void *spot_b = zlink_spot_new (node_b);
    TEST_ASSERT_NOT_NULL (spot_b);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot_b, topic));

    msleep (use_tls ? 500 : 50);

    void *spot_a = zlink_spot_new (node_a);
    TEST_ASSERT_NOT_NULL (spot_a);

    zlink_msg_t parts[1];
    const size_t payload_size = strlen (payload);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], payload_size));
    memcpy (zlink_msg_data (&parts[0]), payload, payload_size);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_a, topic, parts, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    char recv_topic[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot_b, &recv_parts, &recv_count, 0, recv_topic, NULL));
    TEST_ASSERT_EQUAL_STRING (topic, recv_topic);
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count);
    TEST_ASSERT_EQUAL_MEMORY (payload, zlink_msg_data (&recv_parts[0]), payload_size);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));

    if (use_tls)
        cleanup_tls_test_files (files);
}

static void test_spot_peer_ipc ()
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

static void test_spot_peer_tcp ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }
    run_spot_peer_transport_test (peer_transport_tcp);
}

static void test_spot_peer_ws ()
{
    if (!zlink_has ("ws")) {
        TEST_IGNORE_MESSAGE ("WS not available");
        return;
    }
    run_spot_peer_transport_test (peer_transport_ws);
}

static void test_spot_peer_tls ()
{
    if (!zlink_has ("tls")) {
        TEST_IGNORE_MESSAGE ("TLS not available");
        return;
    }
    run_spot_peer_transport_test (peer_transport_tls);
}

static void test_spot_peer_wss ()
{
    if (!zlink_has ("wss")) {
        TEST_IGNORE_MESSAGE ("WSS not available");
        return;
    }
    run_spot_peer_transport_test (peer_transport_wss);
}

static void test_spot_unsubscribe ()
{
    // TODO: zlink_spot_sub_socket API ??? ???????
    TEST_IGNORE_MESSAGE ("Unsubscribe test pending - requires spot_sub_socket API");
    return;

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot, "unsub:topic"));

    zlink_msg_t parts[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 4));
    memcpy (zlink_msg_data (&parts[0]), "msg1", 4);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot, "unsub:topic", parts, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot, &recv_parts, &recv_count, 0, NULL, NULL));
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count);
    TEST_ASSERT_EQUAL_MEMORY ("msg1", zlink_msg_data (&recv_parts[0]), 4);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_unsubscribe (spot, "unsub:topic"));

    msleep (50);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 4));
    memcpy (zlink_msg_data (&parts[0]), "msg2", 4);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot, "unsub:topic", parts, 1, 0));

    int rcvtimeo = 100;
    void *sub_socket = zlink_spot_sub_socket (spot);
    TEST_ASSERT_NOT_NULL (sub_socket);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (sub_socket, ZLINK_RCVTIMEO, &rcvtimeo, sizeof (rcvtimeo)));

    int rc = zlink_spot_recv (spot, &recv_parts, &recv_count, 0, NULL, NULL);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_multi_publisher ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_a = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_b);
    void *node_c = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_c);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, "inproc://pub-a"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, "inproc://pub-b"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_c, "inproc://sub-c"));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (node_c, "inproc://pub-a"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (node_c, "inproc://pub-b"));

    void *spot_c = zlink_spot_new (node_c);
    TEST_ASSERT_NOT_NULL (spot_c);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot_c, "multi:topic"));

    msleep (50);

    void *spot_a = zlink_spot_new (node_a);
    TEST_ASSERT_NOT_NULL (spot_a);
    void *spot_b = zlink_spot_new (node_b);
    TEST_ASSERT_NOT_NULL (spot_b);

    zlink_msg_t parts_a[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts_a[0], 5));
    memcpy (zlink_msg_data (&parts_a[0]), "from-a", 5);

    zlink_msg_t parts_b[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts_b[0], 6));
    memcpy (zlink_msg_data (&parts_b[0]), "from-b", 6);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_a, "multi:topic", parts_a, 1, 0));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_b, "multi:topic", parts_b, 1, 0));

    zlink_msg_t *recv_parts_1 = NULL;
    size_t recv_count_1 = 0;
    char topic_1[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot_c, &recv_parts_1, &recv_count_1, 0, topic_1, NULL));
    TEST_ASSERT_EQUAL_STRING ("multi:topic", topic_1);
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count_1);

    zlink_msg_t *recv_parts_2 = NULL;
    size_t recv_count_2 = 0;
    char topic_2[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot_c, &recv_parts_2, &recv_count_2, 0, topic_2, NULL));
    TEST_ASSERT_EQUAL_STRING ("multi:topic", topic_2);
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count_2);

    bool got_from_a = false;
    bool got_from_b = false;

    if (zlink_msg_size (&recv_parts_1[0]) == 5
        && memcmp (zlink_msg_data (&recv_parts_1[0]), "from-a", 5) == 0) {
        got_from_a = true;
    } else if (zlink_msg_size (&recv_parts_1[0]) == 6
               && memcmp (zlink_msg_data (&recv_parts_1[0]), "from-b", 6) == 0) {
        got_from_b = true;
    }

    if (zlink_msg_size (&recv_parts_2[0]) == 5
        && memcmp (zlink_msg_data (&recv_parts_2[0]), "from-a", 5) == 0) {
        got_from_a = true;
    } else if (zlink_msg_size (&recv_parts_2[0]) == 6
               && memcmp (zlink_msg_data (&recv_parts_2[0]), "from-b", 6) == 0) {
        got_from_b = true;
    }

    TEST_ASSERT_TRUE (got_from_a);
    TEST_ASSERT_TRUE (got_from_b);

    zlink_msgv_close (recv_parts_1, recv_count_1);
    zlink_msgv_close (recv_parts_2, recv_count_2);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_c));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_c));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static int zone_idx (int x_, int y_, int width_)
{
    return y_ * width_ + x_;
}

static bool zone_is_adjacent_or_self (int src_x_,
                                      int src_y_,
                                      int dst_x_,
                                      int dst_y_)
{
    const int dx = src_x_ - dst_x_;
    const int dy = src_y_ - dst_y_;
    const int manhattan = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    return manhattan <= 1;
}

static int env_int_or_default (const char *name_, int default_)
{
    const char *val = getenv (name_);
    if (!val || !*val)
        return default_;

    const int parsed = atoi (val);
    if (parsed <= 0)
        return default_;
    return parsed;
}

static bool wait_for_provider_count (void *discovery_,
                                     const char *service_name_,
                                     int expected_count_,
                                     int timeout_ms_)
{
    const int sleep_ms_step = 25;
    const int max_attempts = timeout_ms_ / sleep_ms_step;

    for (int i = 0; i < max_attempts; ++i) {
        const int count =
          zlink_discovery_receiver_count (discovery_, service_name_);
        if (count == expected_count_)
            return true;
        msleep (sleep_ms_step);
    }
    return false;
}

static bool wait_for_spot_message (void *spot_,
                                   const char *expected_topic_,
                                   const char *expected_payload_,
                                   size_t expected_payload_size_,
                                   int timeout_ms_)
{
    const int sleep_ms_step = 10;
    const int max_attempts = timeout_ms_ / sleep_ms_step;

    for (int i = 0; i < max_attempts; ++i) {
        zlink_msg_t *recv_parts = NULL;
        size_t recv_count = 0;
        char topic[256];
        int rc = zlink_spot_recv (
          spot_, &recv_parts, &recv_count, ZLINK_DONTWAIT, topic, NULL);
        if (rc == 0) {
            bool ok = recv_count == 1
                      && strcmp (topic, expected_topic_) == 0
                      && zlink_msg_size (&recv_parts[0]) == expected_payload_size_
                      && memcmp (zlink_msg_data (&recv_parts[0]),
                                 expected_payload_,
                                 expected_payload_size_)
                           == 0;
            zlink_msgv_close (recv_parts, recv_count);
            if (ok)
                return true;
            continue;
        }
        if (errno != EAGAIN)
            return false;
        msleep (sleep_ms_step);
    }
    return false;
}

static void test_spot_mmorpg_zone_adjacency_scale ()
{
    const int field_width = env_int_or_default ("ZLINK_SPOT_FIELD_WIDTH", 16);
    const int field_height = env_int_or_default ("ZLINK_SPOT_FIELD_HEIGHT", 16);
    const int zone_count = field_width * field_height;

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    std::vector<void *> spots (zone_count, static_cast<void *> (NULL));
    std::vector<int> expected_counts (zone_count, 0);
    std::vector<std::string> topics (zone_count);

    for (int y = 0; y < field_height; ++y) {
        for (int x = 0; x < field_width; ++x) {
            const int idx = zone_idx (x, y, field_width);

            spots[idx] = zlink_spot_new (node);
            TEST_ASSERT_NOT_NULL (spots[idx]);

            char topic_buf[64];
            snprintf (topic_buf, sizeof (topic_buf), "field:%d:%d:state", x, y);
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
                    TEST_ASSERT_SUCCESS_ERRNO (
                      zlink_spot_subscribe (spots[dst_idx], topics[src_idx].c_str ()));
                    expected_counts[dst_idx]++;
                }
            }
        }
    }

    msleep (1000);

    for (int y = 0; y < field_height; ++y) {
        for (int x = 0; x < field_width; ++x) {
            const int src_idx = zone_idx (x, y, field_width);

            zlink_msg_t part;
            TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, sizeof (int)));
            memcpy (zlink_msg_data (&part), &src_idx, sizeof (int));

            TEST_ASSERT_SUCCESS_ERRNO (
              zlink_spot_publish (spots[src_idx], topics[src_idx].c_str (), &part, 1, 0));
            if ((src_idx % 32) == 0)
                msleep (1);
        }
    }

    for (int y = 0; y < field_height; ++y) {
        for (int x = 0; x < field_width; ++x) {
            const int dst_idx = zone_idx (x, y, field_width);
            std::vector<unsigned char> seen (zone_count, 0);
            int received = 0;
            while (received < expected_counts[dst_idx]) {
                zlink_msg_t *recv_parts = NULL;
                size_t recv_count = 0;
                char recv_topic[128];
                TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_recv (
                  spots[dst_idx], &recv_parts, &recv_count, 0, recv_topic, NULL));
                received++;

                TEST_ASSERT_EQUAL_INT (1, (int) recv_count);
                TEST_ASSERT_EQUAL_INT ((int) sizeof (int),
                                       (int) zlink_msg_size (&recv_parts[0]));

                int src_idx = -1;
                memcpy (&src_idx, zlink_msg_data (&recv_parts[0]), sizeof (int));
                TEST_ASSERT_TRUE (src_idx >= 0 && src_idx < zone_count);
                TEST_ASSERT_FALSE (seen[src_idx] != 0);
                seen[src_idx] = 1;

                const int src_x = src_idx % field_width;
                const int src_y = src_idx / field_width;
                TEST_ASSERT_TRUE (
                  zone_is_adjacent_or_self (src_x, src_y, x, y));
                TEST_ASSERT_EQUAL_STRING (topics[src_idx].c_str (), recv_topic);

                zlink_msgv_close (recv_parts, recv_count);
            }

            TEST_ASSERT_EQUAL_INT (expected_counts[dst_idx], received);
        }
    }

    for (int i = 0; i < zone_count; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spots[i]));
    }
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_mmorpg_zone_adjacency_scale_multi_node_discovery ()
{
    const int field_width = 100;
    const int field_height = 100;
    const int zone_count = field_width * field_height;
    const int spot_node_count = 10;

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_endpoints (registry, "inproc://spot-reg-pub-mmorpg",
                                    "inproc://spot-reg-router-mmorpg"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_SPOT_NODE);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://spot-reg-pub-mmorpg"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "spot-field-mmorpg"));

    std::vector<void *> nodes (spot_node_count, static_cast<void *> (NULL));
    for (int i = 0; i < spot_node_count; ++i) {
        nodes[i] = zlink_spot_node_new (ctx);
        TEST_ASSERT_NOT_NULL (nodes[i]);
        int sndhwm = 1000000;
        int rcvhwm = 1000000;
        int rcvtimeo = 5000;
        int linger = 0;
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_setsockopt (
          nodes[i], ZLINK_SPOT_NODE_SOCKET_PUB, ZLINK_SNDHWM, &sndhwm,
          sizeof (sndhwm)));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_setsockopt (
          nodes[i], ZLINK_SPOT_NODE_SOCKET_SUB, ZLINK_RCVHWM, &rcvhwm,
          sizeof (rcvhwm)));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_setsockopt (
          nodes[i], ZLINK_SPOT_NODE_SOCKET_SUB, ZLINK_RCVTIMEO, &rcvtimeo,
          sizeof (rcvtimeo)));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_setsockopt (
          nodes[i], ZLINK_SPOT_NODE_SOCKET_PUB, ZLINK_LINGER, &linger,
          sizeof (linger)));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_setsockopt (
          nodes[i], ZLINK_SPOT_NODE_SOCKET_SUB, ZLINK_LINGER, &linger,
          sizeof (linger)));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_bind (nodes[i], "tcp://127.0.0.1:*"));
    }

    for (int i = 0; i < spot_node_count; ++i) {
        char endpoint[256] = {0};
        size_t endpoint_len = sizeof (endpoint);
        TEST_ASSERT_SUCCESS_ERRNO (zlink_getsockopt (
          zlink_spot_node_pub_socket (nodes[i]), ZLINK_LAST_ENDPOINT, endpoint,
          &endpoint_len));

        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_connect_registry (
          nodes[i], "inproc://spot-reg-router-mmorpg"));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_register (
          nodes[i], "spot-field-mmorpg", endpoint));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_set_discovery (
          nodes[i], discovery, "spot-field-mmorpg"));
    }

    TEST_ASSERT_TRUE (
      wait_for_provider_count (discovery, "spot-field-mmorpg", spot_node_count, 5000));
    // Auto peer connections can be delayed under load.
    msleep (3000);

    std::vector<void *> spots (zone_count, static_cast<void *> (NULL));
    std::vector<std::string> topics (zone_count);

    for (int y = 0; y < field_height; ++y) {
        for (int x = 0; x < field_width; ++x) {
            const int idx = zone_idx (x, y, field_width);
            const int owner_node = idx % spot_node_count;

            spots[idx] = zlink_spot_new (nodes[owner_node]);
            TEST_ASSERT_NOT_NULL (spots[idx]);

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
                    TEST_ASSERT_SUCCESS_ERRNO (
                      zlink_spot_subscribe (spots[dst_idx], topics[src_idx].c_str ()));
                }
            }
        }
    }

    // Allow subscription propagation across all spot nodes before publishing.
    msleep (5000);

    const int sample_coords[][2] = {{0, 0},
                                    {99, 0},
                                    {0, 99},
                                    {99, 99},
                                    {50, 50},
                                    {10, 10},
                                    {20, 40},
                                    {33, 77},
                                    {44, 55},
                                    {70, 30},
                                    {88, 11},
                                    {95, 95}};
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
          zlink_spot_publish (spots[src_idx], topics[src_idx].c_str (), &part, 1, 0));

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
                  spots[dst_idx], topics[src_idx].c_str (),
                  (const char *) &src_idx, sizeof (int), 1000));
            }
        }
    }

    for (int i = 0; i < zone_count; ++i)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spots[i]));

    for (int i = 0; i < spot_node_count; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_unregister (nodes[i], "spot-field-mmorpg"));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&nodes[i]));
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_discovery_auto_peer_connect ()
{
    const char *service_name = "spot-field-auto";
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_endpoints (registry, "inproc://spot-reg-pub-auto",
                                    "inproc://spot-reg-router-auto"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_SPOT_NODE);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery, "inproc://spot-reg-pub-auto"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, service_name));

    void *node_a = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_a);
    void *node_b = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_b);

    int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_setsockopt (node_a, ZLINK_SPOT_NODE_SOCKET_PUB,
                                  ZLINK_LINGER, &linger, sizeof (linger)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_setsockopt (node_a, ZLINK_SPOT_NODE_SOCKET_SUB,
                                  ZLINK_LINGER, &linger, sizeof (linger)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_setsockopt (node_b, ZLINK_SPOT_NODE_SOCKET_PUB,
                                  ZLINK_LINGER, &linger, sizeof (linger)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_setsockopt (node_b, ZLINK_SPOT_NODE_SOCKET_SUB,
                                  ZLINK_LINGER, &linger, sizeof (linger)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, "tcp://127.0.0.1:*"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, "tcp://127.0.0.1:*"));

    char ep_a[256] = {0};
    size_t ep_a_len = sizeof (ep_a);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (zlink_spot_node_pub_socket (node_a), ZLINK_LAST_ENDPOINT,
                        ep_a, &ep_a_len));

    char ep_b[256] = {0};
    size_t ep_b_len = sizeof (ep_b);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (zlink_spot_node_pub_socket (node_b), ZLINK_LAST_ENDPOINT,
                        ep_b, &ep_b_len));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_registry (node_a, "inproc://spot-reg-router-auto"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_registry (node_b, "inproc://spot-reg-router-auto"));
    msleep (100);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_register (node_a, service_name, ep_a));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_register (node_b, service_name, ep_b));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_discovery (node_a, discovery, service_name));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_discovery (node_b, discovery, service_name));

    TEST_ASSERT_TRUE (wait_for_provider_count (discovery, service_name, 2, 4000));
    msleep (800);

    void *spot_a = zlink_spot_new (node_a);
    TEST_ASSERT_NOT_NULL (spot_a);
    void *spot_b = zlink_spot_new (node_b);
    TEST_ASSERT_NOT_NULL (spot_b);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot_a, "zone:auto:test"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot_b, "zone:auto:test"));
    msleep (100);

    zlink_msg_t part_a;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part_a, 6));
    memcpy (zlink_msg_data (&part_a), "from-a", 6);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_a, "zone:auto:test", &part_a, 1, 0));
    TEST_ASSERT_TRUE (
      wait_for_spot_message (spot_b, "zone:auto:test", "from-a", 6, 3000));

    zlink_msg_t part_b;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part_b, 6));
    memcpy (zlink_msg_data (&part_b), "from-b", 6);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot_b, "zone:auto:test", &part_b, 1, 0));
    TEST_ASSERT_TRUE (
      wait_for_spot_message (spot_a, "zone:auto:test", "from-b", 6, 3000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_node_setsockopt ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    /* Queue PUB SNDHWM before bind (socket does not exist yet) */
    int sndhwm = 500;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_setsockopt (node, ZLINK_SPOT_NODE_SOCKET_PUB,
                                  ZLINK_SNDHWM, &sndhwm, sizeof (sndhwm)));

    /* Queue SUB RCVHWM before the worker creates the socket */
    int rcvhwm = 600;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_setsockopt (node, ZLINK_SPOT_NODE_SOCKET_SUB,
                                  ZLINK_RCVHWM, &rcvhwm, sizeof (rcvhwm)));

    /* Invalid socket role returns -1 */
    int dummy = 1;
    int rc = zlink_spot_node_setsockopt (node, 99, ZLINK_SNDHWM, &dummy,
                                         sizeof (dummy));
    TEST_ASSERT_EQUAL_INT (-1, rc);

    /* bind triggers PUB creation -> queued opts applied */
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_bind (node, "inproc://spot-sockopt"));

    void *pub_sock = zlink_spot_node_pub_socket (node);
    TEST_ASSERT_NOT_NULL (pub_sock);

    int actual_sndhwm = 0;
    size_t optlen = sizeof (actual_sndhwm);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (pub_sock, ZLINK_SNDHWM, &actual_sndhwm, &optlen));
    TEST_ASSERT_EQUAL_INT (500, actual_sndhwm);

    /* Set PUB option after socket exists -> applied immediately */
    int sndhwm2 = 700;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_setsockopt (node, ZLINK_SPOT_NODE_SOCKET_PUB,
                                  ZLINK_SNDHWM, &sndhwm2, sizeof (sndhwm2)));
    actual_sndhwm = 0;
    optlen = sizeof (actual_sndhwm);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (pub_sock, ZLINK_SNDHWM, &actual_sndhwm, &optlen));
    TEST_ASSERT_EQUAL_INT (700, actual_sndhwm);

    /* spot_t setsockopt delegates to node */
    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    int spot_sndhwm = 800;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_setsockopt (spot, ZLINK_SPOT_SOCKET_PUB,
                             ZLINK_SNDHWM, &spot_sndhwm,
                             sizeof (spot_sndhwm)));
    actual_sndhwm = 0;
    optlen = sizeof (actual_sndhwm);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (pub_sock, ZLINK_SNDHWM, &actual_sndhwm, &optlen));
    TEST_ASSERT_EQUAL_INT (800, actual_sndhwm);

    /* DEALER role on spot is rejected */
    rc = zlink_spot_setsockopt (spot, 99, ZLINK_SNDHWM, &dummy, sizeof (dummy));
    TEST_ASSERT_EQUAL_INT (-1, rc);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

int main (int, char **)
{
    setup_test_environment (300);

    UNITY_BEGIN ();
    RUN_TEST (test_spot_local_pubsub);
    RUN_TEST (test_spot_pattern_subscribe);
    RUN_TEST (test_spot_publish_no_subscribers);
    RUN_TEST (test_spot_multipart_publish);
    RUN_TEST (test_spot_peer_pubsub);
    RUN_TEST (test_spot_peer_ipc);
    RUN_TEST (test_spot_peer_tcp);
    RUN_TEST (test_spot_peer_ws);
    RUN_TEST (test_spot_peer_tls);
    RUN_TEST (test_spot_peer_wss);
    RUN_TEST (test_spot_unsubscribe);
    RUN_TEST (test_spot_multi_publisher);
    RUN_TEST (test_spot_mmorpg_zone_adjacency_scale);
    RUN_TEST (test_spot_mmorpg_zone_adjacency_scale_multi_node_discovery);
    RUN_TEST (test_spot_node_setsockopt);
    return UNITY_END ();
}
