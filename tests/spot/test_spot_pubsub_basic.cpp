/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"
#include "../testutil.hpp"

#include <string.h>

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

int main (int, char **)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_spot_local_pubsub);
    RUN_TEST (test_spot_pattern_subscribe);
    RUN_TEST (test_spot_publish_no_subscribers);
    RUN_TEST (test_spot_multipart_publish);
    RUN_TEST (test_spot_peer_pubsub);
    return UNITY_END ();
}
