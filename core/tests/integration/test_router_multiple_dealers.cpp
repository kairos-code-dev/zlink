/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"
#include "../../src/core/ctx.hpp"

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

namespace
{
void *create_sync_socket (int type_)
{
    void *socket =
      static_cast<zlink::ctx_t *> (get_test_context ())->create_socket (type_);
    TEST_ASSERT_NOT_NULL (socket);
    return socket;
}

void close_sync_socket (void *socket_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (socket_));
}
}

void test_router_multiple_dealers_tcp ()
{
    void *router = create_sync_socket (ZLINK_ROUTER);
    void *dealer1 = create_sync_socket (ZLINK_DEALER);
    void *dealer2 = create_sync_socket (ZLINK_DEALER);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (dealer1, ZLINK_ROUTING_ID, "D1", 2));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (dealer2, ZLINK_ROUTING_ID, "D2", 2));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, "tcp://127.0.0.1:*"));

    char endpoint[MAX_SOCKET_STRING];
    size_t len = sizeof (endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (router, ZLINK_SOCKOPT_LAST_ENDPOINT, endpoint, &len));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer1, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer2, endpoint));

    msleep (SETTLE_TIME);

    // Both dealers send messages
    send_string_expect_success (dealer1, "from_dealer1", 0);
    recv_string_expect_success (router, "D1", 0);
    recv_string_expect_success (router, "from_dealer1", 0);
    send_string_expect_success (dealer2, "from_dealer2", 0);
    recv_string_expect_success (router, "D2", 0);
    recv_string_expect_success (router, "from_dealer2", 0);

    // Router can reply to specific dealer
    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (router, "D1", 2, ZLINK_SNDMORE));
    send_string_expect_success (router, "reply_to_d1", 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (router, "D2", 2, ZLINK_SNDMORE));
    send_string_expect_success (router, "reply_to_d2", 0);

    // Dealers receive their specific replies
    recv_string_expect_success (dealer1, "reply_to_d1", 0);
    recv_string_expect_success (dealer2, "reply_to_d2", 0);

    close_sync_socket (dealer2);
    close_sync_socket (dealer1);
    close_sync_socket (router);
}

void test_router_multiple_dealers_ipc ()
{
#if defined(ZLINK_HAVE_IPC)
    void *router = create_sync_socket (ZLINK_ROUTER);
    void *dealer1 = create_sync_socket (ZLINK_DEALER);
    void *dealer2 = create_sync_socket (ZLINK_DEALER);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (dealer1, ZLINK_ROUTING_ID, "D1", 2));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (dealer2, ZLINK_ROUTING_ID, "D2", 2));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, "ipc://*"));

    char endpoint[MAX_SOCKET_STRING];
    size_t len = sizeof (endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (router, ZLINK_SOCKOPT_LAST_ENDPOINT, endpoint, &len));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer1, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer2, endpoint));

    msleep (SETTLE_TIME);

    // Both dealers send messages
    send_string_expect_success (dealer1, "from_dealer1", 0);
    recv_string_expect_success (router, "D1", 0);
    recv_string_expect_success (router, "from_dealer1", 0);
    send_string_expect_success (dealer2, "from_dealer2", 0);
    recv_string_expect_success (router, "D2", 0);
    recv_string_expect_success (router, "from_dealer2", 0);

    // Router replies to specific dealers
    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (router, "D1", 2, ZLINK_SNDMORE));
    send_string_expect_success (router, "reply_to_d1", 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (router, "D2", 2, ZLINK_SNDMORE));
    send_string_expect_success (router, "reply_to_d2", 0);

    recv_string_expect_success (dealer1, "reply_to_d1", 0);
    recv_string_expect_success (dealer2, "reply_to_d2", 0);

    close_sync_socket (dealer2);
    close_sync_socket (dealer1);
    close_sync_socket (router);
#else
    TEST_IGNORE_MESSAGE ("IPC not supported on this platform");
#endif
}

void test_router_multiple_dealers_inproc ()
{
    void *router = create_sync_socket (ZLINK_ROUTER);
    void *dealer1 = create_sync_socket (ZLINK_DEALER);
    void *dealer2 = create_sync_socket (ZLINK_DEALER);

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
    recv_string_expect_success (router, "D1", 0);
    recv_string_expect_success (router, "from_dealer1", 0);
    send_string_expect_success (dealer2, "from_dealer2", 0);
    recv_string_expect_success (router, "D2", 0);
    recv_string_expect_success (router, "from_dealer2", 0);

    // Router replies to specific dealers
    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (router, "D1", 2, ZLINK_SNDMORE));
    send_string_expect_success (router, "reply_to_d1", 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (router, "D2", 2, ZLINK_SNDMORE));
    send_string_expect_success (router, "reply_to_d2", 0);

    recv_string_expect_success (dealer1, "reply_to_d1", 0);
    recv_string_expect_success (dealer2, "reply_to_d2", 0);

    close_sync_socket (dealer2);
    close_sync_socket (dealer1);
    close_sync_socket (router);
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
