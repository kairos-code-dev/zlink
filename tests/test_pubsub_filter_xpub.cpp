/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <unity.h>
#include <cstring>

void setUp ()
{
    setup_test_context ();
}

void tearDown ()
{
    teardown_test_context ();
}

static void test_pubsub_filter_transport (const char *endpoint_)
{
    void *pub = test_context_socket (ZLINK_PUB);
    void *sub = test_context_socket (ZLINK_SUB);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (pub, endpoint_));

    char connect_endpoint[MAX_SOCKET_STRING];
    if (strncmp (endpoint_, "tcp://", 6) == 0
        || strncmp (endpoint_, "ipc://", 6) == 0) {
        size_t len = sizeof (connect_endpoint);
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_getsockopt (pub, ZLINK_LAST_ENDPOINT, connect_endpoint, &len));
    } else {
        strcpy (connect_endpoint, endpoint_);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sub, connect_endpoint));

    // Subscribe only to "topicA"
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (sub, ZLINK_SUBSCRIBE, "topicA", 6));

    msleep (SETTLE_TIME);

    // Send messages with different topics
    send_string_expect_success (pub, "topicA hello", 0);
    send_string_expect_success (pub, "topicB world", 0);
    send_string_expect_success (pub, "topicA test", 0);

    // Should only receive topicA messages
    recv_string_expect_success (sub, "topicA hello", 0);
    recv_string_expect_success (sub, "topicA test", 0);

    test_context_socket_close (sub);
    test_context_socket_close (pub);
}

void test_pubsub_filter_tcp ()
{
    test_pubsub_filter_transport ("tcp://127.0.0.1:*");
}

void test_pubsub_filter_ipc ()
{
#if defined(ZLINK_HAVE_IPC)
    test_pubsub_filter_transport ("ipc://*");
#else
    TEST_IGNORE_MESSAGE ("IPC not supported on this platform");
#endif
}

void test_pubsub_filter_inproc ()
{
    test_pubsub_filter_transport ("inproc://test_pubsub_filter");
}

void test_pubsub_xpub_xsub_tcp ()
{
    void *xpub = test_context_socket (ZLINK_XPUB);
    void *xsub = test_context_socket (ZLINK_XSUB);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (xpub, "tcp://127.0.0.1:*"));

    char endpoint[MAX_SOCKET_STRING];
    size_t len = sizeof (endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (xpub, ZLINK_LAST_ENDPOINT, endpoint, &len));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (xsub, endpoint));

    // XSUB subscribe (first byte 0x01 means subscribe)
    char sub_msg[] = {0x01, 0};
    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (xsub, sub_msg, 1, 0));

    // Wait for subscription to propagate
    char sub_recv[16];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (xpub, sub_recv, sizeof (sub_recv), 0));

    msleep (SETTLE_TIME);

    // Test message flow
    const char *msg = "xpub_xsub_test";
    send_string_expect_success (xpub, msg, 0);
    recv_string_expect_success (xsub, msg, 0);

    test_context_socket_close (xsub);
    test_context_socket_close (xpub);
}

void test_pubsub_xpub_xsub_ipc ()
{
#if defined(ZLINK_HAVE_IPC)
    void *xpub = test_context_socket (ZLINK_XPUB);
    void *xsub = test_context_socket (ZLINK_XSUB);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (xpub, "ipc://*"));

    char endpoint[MAX_SOCKET_STRING];
    size_t len = sizeof (endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (xpub, ZLINK_LAST_ENDPOINT, endpoint, &len));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (xsub, endpoint));

    // XSUB subscribe
    char sub_msg[] = {0x01, 0};
    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (xsub, sub_msg, 1, 0));

    // Wait for subscription to propagate
    char sub_recv[16];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (xpub, sub_recv, sizeof (sub_recv), 0));

    msleep (SETTLE_TIME);

    // Test message flow
    const char *msg = "xpub_xsub_test";
    send_string_expect_success (xpub, msg, 0);
    recv_string_expect_success (xsub, msg, 0);

    test_context_socket_close (xsub);
    test_context_socket_close (xpub);
#else
    TEST_IGNORE_MESSAGE ("IPC not supported on this platform");
#endif
}

void test_pubsub_xpub_xsub_inproc ()
{
    void *xpub = test_context_socket (ZLINK_XPUB);
    void *xsub = test_context_socket (ZLINK_XSUB);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (xpub, "inproc://test_xpub_xsub"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (xsub, "inproc://test_xpub_xsub"));

    // XSUB subscribe
    char sub_msg[] = {0x01, 0};
    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (xsub, sub_msg, 1, 0));

    // Wait for subscription to propagate
    char sub_recv[16];
    TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (xpub, sub_recv, sizeof (sub_recv), 0));

    // Test message flow
    const char *msg = "xpub_xsub_test";
    send_string_expect_success (xpub, msg, 0);
    recv_string_expect_success (xsub, msg, 0);

    test_context_socket_close (xsub);
    test_context_socket_close (xpub);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_pubsub_filter_tcp);
    RUN_TEST (test_pubsub_filter_ipc);
    RUN_TEST (test_pubsub_filter_inproc);
    RUN_TEST (test_pubsub_xpub_xsub_tcp);
    RUN_TEST (test_pubsub_xpub_xsub_ipc);
    RUN_TEST (test_pubsub_xpub_xsub_inproc);
    return UNITY_END ();
}
