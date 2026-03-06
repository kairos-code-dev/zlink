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

int main (int, char **)
{
    setup_test_environment (300);

    UNITY_BEGIN ();
    RUN_TEST (test_spot_pub_socket_mode_rejects_facade_publish);
    RUN_TEST (test_spot_sub_socket_mode_rejects_facade_recv);
    return UNITY_END ();
}
