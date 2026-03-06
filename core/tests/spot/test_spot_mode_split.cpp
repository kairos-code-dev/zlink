/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <vector>
#include <string.h>

void setUp ()
{
}

void tearDown ()
{
}

static void test_spot_pub_socket_mode_rejects_facade_publish ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22100));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node, endpoint));

    void *pub = zlink_spot_pub_new (node);
    TEST_ASSERT_NOT_NULL (pub);
    void *pollable_pub = zlink_spot_node_pub_socket (node);
    TEST_ASSERT_NOT_NULL (pollable_pub);

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "test", 4);

    TEST_ASSERT_EQUAL_INT (-1,
                           zlink_spot_pub_publish (pub, "mode:pub", &part, 1,
                                                   0));
    TEST_ASSERT_EQUAL_INT (EFSM, errno);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&part));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_pub_destroy (&pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_sub_socket_mode_rejects_facade_recv ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = zlink_spot_node_new (ctx);
    void *sub_node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22101));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (pub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_connect_peer_pub (sub_node,
                                                                 endpoint));

    void *sub = zlink_spot_sub_new (sub_node);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_subscribe (sub, "mode:sub"));
    msleep (100);

    void *pollable_sub = zlink_spot_node_sub_socket (sub_node);
    TEST_ASSERT_NOT_NULL (pollable_sub);

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_sub_recv (sub, &parts, &part_count, ZLINK_DONTWAIT, topic,
                               &topic_len));
    TEST_ASSERT_EQUAL_INT (EFSM, errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_sub_socket_mode_allows_raw_socket_recv ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = zlink_spot_node_new (ctx);
    void *sub_node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22102));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (pub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (sub_node, endpoint));

    void *sub = zlink_spot_sub_new (sub_node);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_subscribe (sub, "mode:raw"));
    msleep (100);

    void *pollable_sub = zlink_spot_node_sub_socket (sub_node);
    TEST_ASSERT_NOT_NULL (pollable_sub);

    void *pub = zlink_spot_pub_new (pub_node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_publish_bytes (pub, "mode:raw", "pong", 4, 0));

    zlink_msg_t topic;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&topic));
    const int topic_rc = zlink_msg_recv (&topic, pollable_sub, 0);
    TEST_ASSERT_EQUAL_INT (8, topic_rc);
    TEST_ASSERT_EQUAL_UINT (8, zlink_msg_size (&topic));
    TEST_ASSERT_EQUAL_MEMORY ("mode:raw", zlink_msg_data (&topic), 8);
    TEST_ASSERT_TRUE (zlink_msg_more (&topic) != 0);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&topic));

    zlink_msg_t payload;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&payload));
    const int payload_rc = zlink_msg_recv (&payload, pollable_sub, 0);
    TEST_ASSERT_EQUAL_INT (4, payload_rc);
    TEST_ASSERT_EQUAL_UINT (4, zlink_msg_size (&payload));
    TEST_ASSERT_EQUAL_MEMORY ("pong", zlink_msg_data (&payload), 4);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&payload));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_pub_destroy (&pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void recv_raw_spot_message (void *sub_socket_,
                                   const char *topic_,
                                   const char *payload_)
{
    zlink_pollitem_t item = {sub_socket_, 0, ZLINK_POLLIN, 0};
    TEST_ASSERT_EQUAL_INT (1, zlink_poll (&item, 1, 2000));
    TEST_ASSERT_TRUE ((item.revents & ZLINK_POLLIN) != 0);

    zlink_msg_t topic;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&topic));
    const int topic_rc = zlink_msg_recv (&topic, sub_socket_, ZLINK_DONTWAIT);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (strlen (topic_)), topic_rc);
    TEST_ASSERT_EQUAL_UINT (strlen (topic_), zlink_msg_size (&topic));
    TEST_ASSERT_EQUAL_MEMORY (topic_, zlink_msg_data (&topic), strlen (topic_));
    TEST_ASSERT_TRUE (zlink_msg_more (&topic) != 0);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&topic));

    zlink_msg_t payload;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&payload));
    const int payload_rc =
      zlink_msg_recv (&payload, sub_socket_, ZLINK_DONTWAIT);
    TEST_ASSERT_EQUAL_INT (static_cast<int> (strlen (payload_)), payload_rc);
    TEST_ASSERT_EQUAL_UINT (strlen (payload_), zlink_msg_size (&payload));
    TEST_ASSERT_EQUAL_MEMORY (payload_, zlink_msg_data (&payload),
                              strlen (payload_));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&payload));
}

static void test_spot_sub_socket_mode_rejects_sub_mutation_after_handover ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *pollable_sub = zlink_spot_node_sub_socket (node);
    TEST_ASSERT_NOT_NULL (pollable_sub);

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_connect_peer_pub (node, "tcp://127.0.0.1:29999"));
    TEST_ASSERT_EQUAL_INT (EFSM, errno);

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_disconnect_peer_pub (node, "tcp://127.0.0.1:29999"));
    TEST_ASSERT_EQUAL_INT (EFSM, errno);

    const int recv_timeout_ms = 10;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_setsockopt (node, ZLINK_SPOT_NODE_SOCKET_SUB,
                                      ZLINK_RCVTIMEO, &recv_timeout_ms,
                                      sizeof (recv_timeout_ms)));
    TEST_ASSERT_EQUAL_INT (EFSM, errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_sub_socket_mode_raw_multi_client_dontwait_loop ()
{
    enum
    {
        client_count = 10,
        message_count = 32
    };

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (pub_node);

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22103));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (pub_node, endpoint));

    std::vector<void *> sub_nodes (client_count, NULL);
    std::vector<void *> sub_sockets (client_count, NULL);
    for (int i = 0; i < client_count; ++i) {
        sub_nodes[i] = zlink_spot_node_new (ctx);
        TEST_ASSERT_NOT_NULL (sub_nodes[i]);
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_connect_peer_pub (sub_nodes[i], endpoint));
        sub_sockets[i] = zlink_spot_node_sub_socket (sub_nodes[i]);
        TEST_ASSERT_NOT_NULL (sub_sockets[i]);
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_setsockopt (sub_sockets[i], ZLINK_SUBSCRIBE, "mode:fanout", 11));
    }

    msleep (200);

    void *pub = zlink_spot_pub_new (pub_node);
    TEST_ASSERT_NOT_NULL (pub);

    for (int msg_index = 0; msg_index < message_count; ++msg_index) {
        char payload[32];
        snprintf (payload, sizeof (payload), "msg-%02d", msg_index);
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_pub_publish_bytes (pub, "mode:fanout", payload,
                                        strlen (payload), 0));
        for (int client_index = 0; client_index < client_count; ++client_index)
            recv_raw_spot_message (sub_sockets[client_index], "mode:fanout",
                                   payload);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_pub_destroy (&pub));
    for (int i = 0; i < client_count; ++i)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_nodes[i]));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

int main (int, char **)
{
    setup_test_environment (300);

    UNITY_BEGIN ();
    RUN_TEST (test_spot_pub_socket_mode_rejects_facade_publish);
    RUN_TEST (test_spot_sub_socket_mode_rejects_facade_recv);
    RUN_TEST (test_spot_sub_socket_mode_allows_raw_socket_recv);
    RUN_TEST (test_spot_sub_socket_mode_rejects_sub_mutation_after_handover);
    RUN_TEST (test_spot_sub_socket_mode_raw_multi_client_dontwait_loop);
    return UNITY_END ();
}
