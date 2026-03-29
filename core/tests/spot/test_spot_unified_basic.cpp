/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <string.h>

void setUp ()
{
}

void tearDown ()
{
}

static void test_spot_unified_publish_subscribe_roundtrip ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (spot, "chat:lobby"));

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 5));
    memcpy (zlink_msg_data (&part), "hello", 5);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot, "chat:lobby", &part, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    char topic[64] = {0};
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot, &recv_parts, &recv_count, 0, topic, &topic_len));

    TEST_ASSERT_EQUAL_INT (1, (int) recv_count);
    TEST_ASSERT_EQUAL_STRING ("chat:lobby", topic);
    TEST_ASSERT_EQUAL_UINT (10, topic_len);
    TEST_ASSERT_EQUAL_UINT (5, zlink_msg_size (&recv_parts[0]));
    TEST_ASSERT_EQUAL_MEMORY ("hello", zlink_msg_data (&recv_parts[0]), 5);

    zlink_multipart_close (recv_parts, recv_count);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_unified_pattern_unsubscribe ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_subscribe_pattern (spot, "zone:42:*"));

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "ping", 4);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot, "zone:42:state", &part, 1, 0));

    zlink_msg_t *recv_parts = NULL;
    size_t recv_count = 0;
    char topic[64] = {0};
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (spot, &recv_parts, &recv_count, 0, topic, NULL));
    zlink_multipart_close (recv_parts, recv_count);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_unsubscribe (spot, "zone:42:*"));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "pong", 4);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_publish (spot, "zone:42:state", &part, 1, 0));

    TEST_ASSERT_FAILURE_ERRNO (
      EAGAIN, zlink_spot_recv (spot, &recv_parts, &recv_count, ZLINK_DONTWAIT,
                               topic, NULL));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_unified_setsockopt_and_borrowed_node_lifecycle ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    int sndhwm = 321;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_setsockopt (node, ZLINK_SPOT_NODE_SOCKET_PUB,
                                  ZLINK_SNDHWM, &sndhwm, sizeof (sndhwm)));

    void *spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot);

    int rcvhwm = 654;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_setsockopt (
      spot, ZLINK_SPOT_SOCKET_SUB, ZLINK_RCVHWM, &rcvhwm, sizeof (rcvhwm)));
    TEST_ASSERT_FAILURE_ERRNO (
      EINVAL, zlink_spot_setsockopt (spot, 99, ZLINK_RCVHWM, &rcvhwm,
                                     sizeof (rcvhwm)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));

    void *spot_again = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (spot_again);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot_again));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

int main (int, char **)
{
    setup_test_environment (60);

    UNITY_BEGIN ();
    RUN_TEST (test_spot_unified_publish_subscribe_roundtrip);
    RUN_TEST (test_spot_unified_pattern_unsubscribe);
    RUN_TEST (test_spot_unified_setsockopt_and_borrowed_node_lifecycle);
    return UNITY_END ();
}
