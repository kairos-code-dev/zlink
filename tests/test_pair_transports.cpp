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

static void test_pair_transport (const char *endpoint_)
{
    void *sb = test_context_socket (ZMQ_PAIR);
    void *sc = test_context_socket (ZMQ_PAIR);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (sb, endpoint_));

    char connect_endpoint[MAX_SOCKET_STRING];
    if (strncmp (endpoint_, "tcp://", 6) == 0
        || strncmp (endpoint_, "ipc://", 6) == 0) {
        size_t len = sizeof (connect_endpoint);
        TEST_ASSERT_SUCCESS_ERRNO (
          zmq_getsockopt (sb, ZMQ_LAST_ENDPOINT, connect_endpoint, &len));
    } else {
        strcpy (connect_endpoint, endpoint_);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (sc, connect_endpoint));

    msleep (SETTLE_TIME);

    // Test basic send/receive
    const char *msg = "test_pair_message";
    send_string_expect_success (sc, msg, 0);
    recv_string_expect_success (sb, msg, 0);

    // Test reverse direction
    send_string_expect_success (sb, msg, 0);
    recv_string_expect_success (sc, msg, 0);

    // Test multipart message
    send_string_expect_success (sc, "part1", ZMQ_SNDMORE);
    send_string_expect_success (sc, "part2", 0);
    recv_string_expect_success (sb, "part1", 0);
    int more;
    size_t more_size = sizeof (more);
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_getsockopt (sb, ZMQ_RCVMORE, &more, &more_size));
    TEST_ASSERT_TRUE (more);
    recv_string_expect_success (sb, "part2", 0);

    // Test bounce (roundtrip)
    bounce (sb, sc);

    test_context_socket_close (sc);
    test_context_socket_close (sb);
}

void test_pair_tcp ()
{
    test_pair_transport ("tcp://127.0.0.1:*");
}

void test_pair_ipc ()
{
#if defined(ZMQ_HAVE_IPC)
    test_pair_transport ("ipc://*");
#else
    TEST_IGNORE_MESSAGE ("IPC not supported on this platform");
#endif
}

void test_pair_inproc ()
{
    test_pair_transport ("inproc://test_pair_transports");
}

void test_pair_multiple_messages_tcp ()
{
    void *sb = test_context_socket (ZMQ_PAIR);
    void *sc = test_context_socket (ZMQ_PAIR);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (sb, "tcp://127.0.0.1:*"));

    char endpoint[MAX_SOCKET_STRING];
    size_t len = sizeof (endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_getsockopt (sb, ZMQ_LAST_ENDPOINT, endpoint, &len));

    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (sc, endpoint));

    msleep (SETTLE_TIME);

    // Send multiple messages
    const int msg_count = 100;
    char buf[32];
    for (int i = 0; i < msg_count; ++i) {
        snprintf (buf, sizeof (buf), "message_%d", i);
        send_string_expect_success (sc, buf, 0);
    }

    // Receive all messages
    for (int i = 0; i < msg_count; ++i) {
        snprintf (buf, sizeof (buf), "message_%d", i);
        recv_string_expect_success (sb, buf, 0);
    }

    test_context_socket_close (sc);
    test_context_socket_close (sb);
}

void test_pair_multiple_messages_ipc ()
{
#if defined(ZMQ_HAVE_IPC)
    void *sb = test_context_socket (ZMQ_PAIR);
    void *sc = test_context_socket (ZMQ_PAIR);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (sb, "ipc://*"));

    char endpoint[MAX_SOCKET_STRING];
    size_t len = sizeof (endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_getsockopt (sb, ZMQ_LAST_ENDPOINT, endpoint, &len));

    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (sc, endpoint));

    msleep (SETTLE_TIME);

    // Send multiple messages
    const int msg_count = 100;
    char buf[32];
    for (int i = 0; i < msg_count; ++i) {
        snprintf (buf, sizeof (buf), "message_%d", i);
        send_string_expect_success (sc, buf, 0);
    }

    // Receive all messages
    for (int i = 0; i < msg_count; ++i) {
        snprintf (buf, sizeof (buf), "message_%d", i);
        recv_string_expect_success (sb, buf, 0);
    }

    test_context_socket_close (sc);
    test_context_socket_close (sb);
#else
    TEST_IGNORE_MESSAGE ("IPC not supported on this platform");
#endif
}

void test_pair_multiple_messages_inproc ()
{
    void *sb = test_context_socket (ZMQ_PAIR);
    void *sc = test_context_socket (ZMQ_PAIR);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (sb, "inproc://test_pair_multi"));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (sc, "inproc://test_pair_multi"));

    // Send multiple messages
    const int msg_count = 100;
    char buf[32];
    for (int i = 0; i < msg_count; ++i) {
        snprintf (buf, sizeof (buf), "message_%d", i);
        send_string_expect_success (sc, buf, 0);
    }

    // Receive all messages
    for (int i = 0; i < msg_count; ++i) {
        snprintf (buf, sizeof (buf), "message_%d", i);
        recv_string_expect_success (sb, buf, 0);
    }

    test_context_socket_close (sc);
    test_context_socket_close (sb);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_pair_tcp);
    RUN_TEST (test_pair_ipc);
    RUN_TEST (test_pair_inproc);
    RUN_TEST (test_pair_multiple_messages_tcp);
    RUN_TEST (test_pair_multiple_messages_ipc);
    RUN_TEST (test_pair_multiple_messages_inproc);
    return UNITY_END ();
}
