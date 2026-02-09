/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"
#include "../testutil.hpp"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

static int create_spot_pub_sub (void *node, void **pub_p, void **sub_p)
{
    if (!pub_p || !sub_p) {
        errno = EINVAL;
        return -1;
    }

    *pub_p = zlink_spot_pub_new (node);
    if (!*pub_p)
        return -1;

    *sub_p = zlink_spot_sub_new (node);
    if (!*sub_p) {
        void *pub = *pub_p;
        *pub_p = NULL;
        zlink_spot_pub_destroy (&pub);
        return -1;
    }
    return 0;
}

static int destroy_spot_pub_sub (void **pub_p, void **sub_p)
{
    if (!pub_p || !sub_p) {
        errno = EFAULT;
        return -1;
    }
    int rc_pub = zlink_spot_pub_destroy (pub_p);
    int rc_sub = zlink_spot_sub_destroy (sub_p);
    return (rc_pub == 0 && rc_sub == 0) ? 0 : -1;
}

void setUp ()
{
}

void tearDown ()
{
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
#if defined(ZLINK_HAVE_IPC)
        char endpoint_b[MAX_SOCKET_STRING];
        make_random_ipc_endpoint (endpoint_a);
        make_random_ipc_endpoint (endpoint_b);

        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_a, endpoint_a));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node_b, endpoint_b));
#else
        TEST_IGNORE_MESSAGE ("IPC not compiled");
        return;
#endif
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

    void *spot_b_pub = NULL;
    void *spot_b_sub = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (create_spot_pub_sub (node_b, &spot_b_pub, &spot_b_sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_subscribe (spot_b_sub, topic));

    msleep (use_tls ? 500 : 50);

    void *spot_a_pub = NULL;
    void *spot_a_sub = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (create_spot_pub_sub (node_a, &spot_a_pub, &spot_a_sub));

    zlink_msg_t parts[1];
    const size_t payload_size = strlen (payload);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], payload_size));
    memcpy (zlink_msg_data (&parts[0]), payload, payload_size);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_publish (spot_a_pub, topic, parts, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    char recv_topic[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_sub_recv (spot_b_sub, &recv_parts, &recv_count, 0, recv_topic, NULL));
    TEST_ASSERT_EQUAL_STRING (topic, recv_topic);
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count);
    TEST_ASSERT_EQUAL_MEMORY (payload, zlink_msg_data (&recv_parts[0]), payload_size);
    zlink_msgv_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_pub_sub (&spot_a_pub, &spot_a_sub));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_pub_sub (&spot_b_pub, &spot_b_sub));
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

    void *spot_c_pub = NULL;
    void *spot_c_sub = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (create_spot_pub_sub (node_c, &spot_c_pub, &spot_c_sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_subscribe (spot_c_sub, "multi:topic"));

    msleep (50);

    void *spot_a_pub = NULL;
    void *spot_a_sub = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (create_spot_pub_sub (node_a, &spot_a_pub, &spot_a_sub));
    void *spot_b_pub = NULL;
    void *spot_b_sub = NULL;
    TEST_ASSERT_SUCCESS_ERRNO (create_spot_pub_sub (node_b, &spot_b_pub, &spot_b_sub));

    zlink_msg_t parts_a[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts_a[0], 5));
    memcpy (zlink_msg_data (&parts_a[0]), "from-a", 5);

    zlink_msg_t parts_b[1];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts_b[0], 6));
    memcpy (zlink_msg_data (&parts_b[0]), "from-b", 6);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_publish (spot_a_pub, "multi:topic", parts_a, 1, 0));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_publish (spot_b_pub, "multi:topic", parts_b, 1, 0));

    zlink_msg_t *recv_parts_1 = NULL;
    size_t recv_count_1 = 0;
    char topic_1[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_sub_recv (spot_c_sub, &recv_parts_1, &recv_count_1, 0, topic_1, NULL));
    TEST_ASSERT_EQUAL_STRING ("multi:topic", topic_1);
    TEST_ASSERT_EQUAL_INT (1, (int) recv_count_1);

    zlink_msg_t *recv_parts_2 = NULL;
    size_t recv_count_2 = 0;
    char topic_2[256];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_sub_recv (spot_c_sub, &recv_parts_2, &recv_count_2, 0, topic_2, NULL));
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

    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_pub_sub (&spot_a_pub, &spot_a_sub));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_pub_sub (&spot_b_pub, &spot_b_sub));
    TEST_ASSERT_SUCCESS_ERRNO (destroy_spot_pub_sub (&spot_c_pub, &spot_c_sub));
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

static bool wait_for_spot_message (void *spot_sub_,
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
        int rc = zlink_spot_sub_recv (
          spot_sub_, &recv_parts, &recv_count, ZLINK_DONTWAIT, topic, NULL);
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
    if (env_int_or_default ("ZLINK_SPOT_RUN_MMORPG_SCALE", 0) == 0) {
        TEST_IGNORE_MESSAGE (
          "MMORPG scale test disabled (set ZLINK_SPOT_RUN_MMORPG_SCALE=1)");
        return;
    }

    const int field_width = env_int_or_default ("ZLINK_SPOT_FIELD_WIDTH", 16);
    const int field_height = env_int_or_default ("ZLINK_SPOT_FIELD_HEIGHT", 16);
    const int zone_count = field_width * field_height;
    const int subscription_settle_ms = 300;
    const int recv_wait_timeout_ms = 1500;
    const int recv_poll_step_ms = 10;

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    std::vector<void *> spot_pubs (zone_count, static_cast<void *> (NULL));
    std::vector<void *> spot_subs (zone_count, static_cast<void *> (NULL));
    std::vector<int> expected_counts (zone_count, 0);
    std::vector<std::string> topics (zone_count);

    for (int y = 0; y < field_height; ++y) {
        for (int x = 0; x < field_width; ++x) {
            const int idx = zone_idx (x, y, field_width);

            TEST_ASSERT_SUCCESS_ERRNO (
              create_spot_pub_sub (node, &spot_pubs[idx], &spot_subs[idx]));

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
                      zlink_spot_sub_subscribe (spot_subs[dst_idx], topics[src_idx].c_str ()));
                    expected_counts[dst_idx]++;
                }
            }
        }
    }

    msleep (subscription_settle_ms);

    for (int y = 0; y < field_height; ++y) {
        for (int x = 0; x < field_width; ++x) {
            const int src_idx = zone_idx (x, y, field_width);

            zlink_msg_t part;
            TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, sizeof (int)));
            memcpy (zlink_msg_data (&part), &src_idx, sizeof (int));

            TEST_ASSERT_SUCCESS_ERRNO (
              zlink_spot_pub_publish (spot_pubs[src_idx], topics[src_idx].c_str (), &part, 1, 0));
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
                const int max_attempts = recv_wait_timeout_ms / recv_poll_step_ms;
                bool got_message = false;
                for (int attempt = 0; attempt < max_attempts; ++attempt) {
                    const int rc = zlink_spot_sub_recv (spot_subs[dst_idx], &recv_parts,
                                                    &recv_count, ZLINK_DONTWAIT,
                                                    recv_topic, NULL);
                    if (rc == 0) {
                        got_message = true;
                        break;
                    }
                    TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
                    msleep (recv_poll_step_ms);
                }
                TEST_ASSERT_TRUE_MESSAGE (
                  got_message,
                  "Timed out waiting for zone-adjacency spot message");
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
        TEST_ASSERT_SUCCESS_ERRNO (
          destroy_spot_pub_sub (&spot_pubs[i], &spot_subs[i]));
    }
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_mmorpg_zone_adjacency_scale_multi_node_discovery ()
{
    if (env_int_or_default ("ZLINK_SPOT_RUN_MULTI_NODE_DISCOVERY", 0) == 0) {
        TEST_IGNORE_MESSAGE (
          "Multi-node discovery scale test disabled "
          "(set ZLINK_SPOT_RUN_MULTI_NODE_DISCOVERY=1)");
        return;
    }

    const int field_width = 24;
    const int field_height = 24;
    const int zone_count = field_width * field_height;
    const int spot_node_count = 4;
    const int auto_peer_settle_ms = 800;
    const int sub_propagation_ms = 1200;
    const int recv_timeout_ms = 1000;

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_endpoints (registry, "inproc://spot-reg-pub-mmorpg",
                                    "inproc://spot-reg-router-mmorpg"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_SPOT);
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

    TEST_ASSERT_TRUE (wait_for_provider_count (
      discovery, "spot-field-mmorpg", spot_node_count, 3000));
    // Auto peer connections can be delayed under load.
    msleep (auto_peer_settle_ms);

    std::vector<void *> spot_pubs (zone_count, static_cast<void *> (NULL));
    std::vector<void *> spot_subs (zone_count, static_cast<void *> (NULL));
    std::vector<std::string> topics (zone_count);

    for (int y = 0; y < field_height; ++y) {
        for (int x = 0; x < field_width; ++x) {
            const int idx = zone_idx (x, y, field_width);
            const int owner_node = idx % spot_node_count;

            TEST_ASSERT_SUCCESS_ERRNO (create_spot_pub_sub (
              nodes[owner_node], &spot_pubs[idx], &spot_subs[idx]));

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
                      zlink_spot_sub_subscribe (spot_subs[dst_idx], topics[src_idx].c_str ()));
                }
            }
        }
    }

    // Allow subscription propagation across all spot nodes before publishing.
    msleep (sub_propagation_ms);

    const int sample_coords[][2] = {{0, 0},
                                    {field_width - 1, 0},
                                    {0, field_height - 1},
                                    {field_width - 1, field_height - 1},
                                    {field_width / 2, field_height / 2},
                                    {field_width / 4, field_height / 4},
                                    {field_width / 5, (field_height * 2) / 5},
                                    {(field_width * 33) / 100,
                                     (field_height * 77) / 100},
                                    {(field_width * 44) / 100,
                                     (field_height * 55) / 100},
                                    {(field_width * 7) / 10,
                                     (field_height * 3) / 10},
                                    {(field_width * 88) / 100,
                                     (field_height * 11) / 100},
                                    {(field_width * 95) / 100,
                                     (field_height * 95) / 100}};
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
          zlink_spot_pub_publish (spot_pubs[src_idx], topics[src_idx].c_str (), &part, 1, 0));

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
                  spot_subs[dst_idx], topics[src_idx].c_str (),
                  (const char *) &src_idx, sizeof (int), recv_timeout_ms));
            }
        }
    }

    for (int i = 0; i < zone_count; ++i)
        TEST_ASSERT_SUCCESS_ERRNO (
          destroy_spot_pub_sub (&spot_pubs[i], &spot_subs[i]));

    for (int i = 0; i < spot_node_count; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_unregister (nodes[i], "spot-field-mmorpg"));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&nodes[i]));
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

int main (int, char **)
{
    setup_test_environment (300);

    UNITY_BEGIN ();
    RUN_TEST (test_spot_peer_ipc);
    RUN_TEST (test_spot_peer_tcp);
    RUN_TEST (test_spot_peer_ws);
    RUN_TEST (test_spot_peer_tls);
    RUN_TEST (test_spot_peer_wss);
    RUN_TEST (test_spot_multi_publisher);
    RUN_TEST (test_spot_mmorpg_zone_adjacency_scale);
    RUN_TEST (test_spot_mmorpg_zone_adjacency_scale_multi_node_discovery);
    return UNITY_END ();
}
