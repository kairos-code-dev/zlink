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

void test_router_multiple_dealers_tcp ()
{
    void *router = test_context_socket (ZLINK_ROUTER);
    void *dealer1 = test_context_socket (ZLINK_DEALER);
    void *dealer2 = test_context_socket (ZLINK_DEALER);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (dealer1, ZLINK_ROUTING_ID, "D1", 2));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (dealer2, ZLINK_ROUTING_ID, "D2", 2));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, "tcp://127.0.0.1:*"));

    char endpoint[MAX_SOCKET_STRING];
    size_t len = sizeof (endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (router, ZLINK_LAST_ENDPOINT, endpoint, &len));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer1, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer2, endpoint));

    msleep (SETTLE_TIME);

    // Both dealers send messages
    send_string_expect_success (dealer1, "from_dealer1", 0);
    send_string_expect_success (dealer2, "from_dealer2", 0);

    // Router receives both messages with their identities
    char identity[32];
    char msg[64];

    // First message
    int id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (router, identity, sizeof (identity), 0));
    int msg_size =
      TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (router, msg, sizeof (msg), 0));
    msg[msg_size] = 0;

    // Second message
    id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (router, identity, sizeof (identity), 0));
    msg_size =
      TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (router, msg, sizeof (msg), 0));
    msg[msg_size] = 0;

    // Router can reply to specific dealer
    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (router, "D1", 2, ZLINK_SNDMORE));
    send_string_expect_success (router, "reply_to_d1", 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (router, "D2", 2, ZLINK_SNDMORE));
    send_string_expect_success (router, "reply_to_d2", 0);

    // Dealers receive their specific replies
    recv_string_expect_success (dealer1, "reply_to_d1", 0);
    recv_string_expect_success (dealer2, "reply_to_d2", 0);

    test_context_socket_close (dealer2);
    test_context_socket_close (dealer1);
    test_context_socket_close (router);
}

void test_router_multiple_dealers_ipc ()
{
#if defined(ZLINK_HAVE_IPC)
    void *router = test_context_socket (ZLINK_ROUTER);
    void *dealer1 = test_context_socket (ZLINK_DEALER);
    void *dealer2 = test_context_socket (ZLINK_DEALER);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (dealer1, ZLINK_ROUTING_ID, "D1", 2));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (dealer2, ZLINK_ROUTING_ID, "D2", 2));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, "ipc://*"));

    char endpoint[MAX_SOCKET_STRING];
    size_t len = sizeof (endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (router, ZLINK_LAST_ENDPOINT, endpoint, &len));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer1, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer2, endpoint));

    msleep (SETTLE_TIME);

    // Both dealers send messages
    send_string_expect_success (dealer1, "from_dealer1", 0);
    send_string_expect_success (dealer2, "from_dealer2", 0);

    // Router receives both
    char identity[32];
    char msg[64];
    int id_size, msg_size;

    id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (router, identity, sizeof (identity), 0));
    msg_size =
      TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (router, msg, sizeof (msg), 0));

    id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (router, identity, sizeof (identity), 0));
    msg_size =
      TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (router, msg, sizeof (msg), 0));
    (void) id_size;
    (void) msg_size;

    // Router replies to specific dealers
    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (router, "D1", 2, ZLINK_SNDMORE));
    send_string_expect_success (router, "reply_to_d1", 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (router, "D2", 2, ZLINK_SNDMORE));
    send_string_expect_success (router, "reply_to_d2", 0);

    recv_string_expect_success (dealer1, "reply_to_d1", 0);
    recv_string_expect_success (dealer2, "reply_to_d2", 0);

    test_context_socket_close (dealer2);
    test_context_socket_close (dealer1);
    test_context_socket_close (router);
#else
    TEST_IGNORE_MESSAGE ("IPC not supported on this platform");
#endif
}

void test_router_multiple_dealers_inproc ()
{
    void *router = test_context_socket (ZLINK_ROUTER);
    void *dealer1 = test_context_socket (ZLINK_DEALER);
    void *dealer2 = test_context_socket (ZLINK_DEALER);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (dealer1, ZLINK_ROUTING_ID, "D1", 2));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (dealer2, ZLINK_ROUTING_ID, "D2", 2));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (router, "inproc://test_router_multi_dealers"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (dealer1, "inproc://test_router_multi_dealers"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (dealer2, "inproc://test_router_multi_dealers"));

    // Both dealers send messages
    send_string_expect_success (dealer1, "from_dealer1", 0);
    send_string_expect_success (dealer2, "from_dealer2", 0);

    // Router receives both
    char identity[32];
    char msg[64];
    int id_size, msg_size;

    id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (router, identity, sizeof (identity), 0));
    msg_size =
      TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (router, msg, sizeof (msg), 0));

    id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (router, identity, sizeof (identity), 0));
    msg_size =
      TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (router, msg, sizeof (msg), 0));
    (void) id_size;
    (void) msg_size;

    // Router replies to specific dealers
    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (router, "D1", 2, ZLINK_SNDMORE));
    send_string_expect_success (router, "reply_to_d1", 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (router, "D2", 2, ZLINK_SNDMORE));
    send_string_expect_success (router, "reply_to_d2", 0);

    recv_string_expect_success (dealer1, "reply_to_d1", 0);
    recv_string_expect_success (dealer2, "reply_to_d2", 0);

    test_context_socket_close (dealer2);
    test_context_socket_close (dealer1);
    test_context_socket_close (router);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_router_multiple_dealers_tcp);
    RUN_TEST (test_router_multiple_dealers_ipc);
    RUN_TEST (test_router_multiple_dealers_inproc);
    return UNITY_END ();
}
