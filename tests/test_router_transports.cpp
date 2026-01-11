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

static void test_router_dealer_transport (const char *endpoint_)
{
    void *router = test_context_socket (ZMQ_ROUTER);
    void *dealer = test_context_socket (ZMQ_DEALER);

    // Set identity on dealer
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (dealer, ZMQ_ROUTING_ID, "DEALER1", 7));

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (router, endpoint_));

    char connect_endpoint[MAX_SOCKET_STRING];
    if (strncmp (endpoint_, "tcp://", 6) == 0
        || strncmp (endpoint_, "ipc://", 6) == 0) {
        size_t len = sizeof (connect_endpoint);
        TEST_ASSERT_SUCCESS_ERRNO (
          zmq_getsockopt (router, ZMQ_LAST_ENDPOINT, connect_endpoint, &len));
    } else {
        strcpy (connect_endpoint, endpoint_);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (dealer, connect_endpoint));

    msleep (SETTLE_TIME);

    // Dealer sends message to router
    const char *msg = "hello_from_dealer";
    send_string_expect_success (dealer, msg, 0);

    // Router receives identity frame + message
    char identity[32];
    int id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zmq_recv (router, identity, sizeof (identity), 0));
    TEST_ASSERT_EQUAL_INT (7, id_size);
    TEST_ASSERT_EQUAL_STRING_LEN ("DEALER1", identity, 7);

    recv_string_expect_success (router, msg, 0);

    // Router sends reply back to dealer using identity
    TEST_ASSERT_SUCCESS_ERRNO (zmq_send (router, "DEALER1", 7, ZMQ_SNDMORE));
    const char *reply = "hello_from_router";
    send_string_expect_success (router, reply, 0);

    // Dealer receives reply
    recv_string_expect_success (dealer, reply, 0);

    test_context_socket_close (dealer);
    test_context_socket_close (router);
}

static void test_router_router_transport (const char *endpoint_)
{
    void *server = test_context_socket (ZMQ_ROUTER);
    void *client = test_context_socket (ZMQ_ROUTER);

    // Set identities
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (server, ZMQ_ROUTING_ID, "SERVER", 6));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (client, ZMQ_ROUTING_ID, "CLIENT", 6));

    // Enable handover to handle reconnection
    int handover = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (server, ZMQ_ROUTER_HANDOVER, &handover, sizeof (handover)));

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (server, endpoint_));

    char connect_endpoint[MAX_SOCKET_STRING];
    if (strncmp (endpoint_, "tcp://", 6) == 0
        || strncmp (endpoint_, "ipc://", 6) == 0) {
        size_t len = sizeof (connect_endpoint);
        TEST_ASSERT_SUCCESS_ERRNO (
          zmq_getsockopt (server, ZMQ_LAST_ENDPOINT, connect_endpoint, &len));
    } else {
        strcpy (connect_endpoint, endpoint_);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (client, connect_endpoint));

    msleep (SETTLE_TIME);

    // Client sends to server using server's identity
    TEST_ASSERT_SUCCESS_ERRNO (zmq_send (client, "SERVER", 6, ZMQ_SNDMORE));
    const char *msg = "hello_router_router";
    send_string_expect_success (client, msg, 0);

    // Server receives from client
    char identity[32];
    int id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zmq_recv (server, identity, sizeof (identity), 0));
    TEST_ASSERT_EQUAL_INT (6, id_size);
    TEST_ASSERT_EQUAL_STRING_LEN ("CLIENT", identity, 6);

    recv_string_expect_success (server, msg, 0);

    // Server replies to client
    TEST_ASSERT_SUCCESS_ERRNO (zmq_send (server, "CLIENT", 6, ZMQ_SNDMORE));
    const char *reply = "reply_from_server";
    send_string_expect_success (server, reply, 0);

    // Client receives reply
    id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zmq_recv (client, identity, sizeof (identity), 0));
    TEST_ASSERT_EQUAL_INT (6, id_size);
    TEST_ASSERT_EQUAL_STRING_LEN ("SERVER", identity, 6);

    recv_string_expect_success (client, reply, 0);

    test_context_socket_close (client);
    test_context_socket_close (server);
}

void test_router_dealer_tcp ()
{
    test_router_dealer_transport ("tcp://127.0.0.1:*");
}

void test_router_dealer_ipc ()
{
#if defined(ZMQ_HAVE_IPC)
    test_router_dealer_transport ("ipc://*");
#else
    TEST_IGNORE_MESSAGE ("IPC not supported on this platform");
#endif
}

void test_router_dealer_inproc ()
{
    test_router_dealer_transport ("inproc://test_router_dealer");
}

void test_router_router_tcp ()
{
    test_router_router_transport ("tcp://127.0.0.1:*");
}

void test_router_router_ipc ()
{
#if defined(ZMQ_HAVE_IPC)
    test_router_router_transport ("ipc://*");
#else
    TEST_IGNORE_MESSAGE ("IPC not supported on this platform");
#endif
}

void test_router_router_inproc ()
{
    test_router_router_transport ("inproc://test_router_router");
}

void test_router_multiple_dealers_tcp ()
{
    void *router = test_context_socket (ZMQ_ROUTER);
    void *dealer1 = test_context_socket (ZMQ_DEALER);
    void *dealer2 = test_context_socket (ZMQ_DEALER);

    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (dealer1, ZMQ_ROUTING_ID, "D1", 2));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (dealer2, ZMQ_ROUTING_ID, "D2", 2));

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (router, "tcp://127.0.0.1:*"));

    char endpoint[MAX_SOCKET_STRING];
    size_t len = sizeof (endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_getsockopt (router, ZMQ_LAST_ENDPOINT, endpoint, &len));

    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (dealer1, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (dealer2, endpoint));

    msleep (SETTLE_TIME);

    // Both dealers send messages
    send_string_expect_success (dealer1, "from_dealer1", 0);
    send_string_expect_success (dealer2, "from_dealer2", 0);

    // Router receives both messages with their identities
    char identity[32];
    char msg[64];

    // First message
    int id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zmq_recv (router, identity, sizeof (identity), 0));
    int msg_size =
      TEST_ASSERT_SUCCESS_ERRNO (zmq_recv (router, msg, sizeof (msg), 0));
    msg[msg_size] = 0;

    // Second message
    id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zmq_recv (router, identity, sizeof (identity), 0));
    msg_size =
      TEST_ASSERT_SUCCESS_ERRNO (zmq_recv (router, msg, sizeof (msg), 0));
    msg[msg_size] = 0;

    // Router can reply to specific dealer
    TEST_ASSERT_SUCCESS_ERRNO (zmq_send (router, "D1", 2, ZMQ_SNDMORE));
    send_string_expect_success (router, "reply_to_d1", 0);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_send (router, "D2", 2, ZMQ_SNDMORE));
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
#if defined(ZMQ_HAVE_IPC)
    void *router = test_context_socket (ZMQ_ROUTER);
    void *dealer1 = test_context_socket (ZMQ_DEALER);
    void *dealer2 = test_context_socket (ZMQ_DEALER);

    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (dealer1, ZMQ_ROUTING_ID, "D1", 2));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (dealer2, ZMQ_ROUTING_ID, "D2", 2));

    TEST_ASSERT_SUCCESS_ERRNO (zmq_bind (router, "ipc://*"));

    char endpoint[MAX_SOCKET_STRING];
    size_t len = sizeof (endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_getsockopt (router, ZMQ_LAST_ENDPOINT, endpoint, &len));

    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (dealer1, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (dealer2, endpoint));

    msleep (SETTLE_TIME);

    // Both dealers send messages
    send_string_expect_success (dealer1, "from_dealer1", 0);
    send_string_expect_success (dealer2, "from_dealer2", 0);

    // Router receives both
    char identity[32];
    char msg[64];
    int id_size, msg_size;

    id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zmq_recv (router, identity, sizeof (identity), 0));
    msg_size =
      TEST_ASSERT_SUCCESS_ERRNO (zmq_recv (router, msg, sizeof (msg), 0));

    id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zmq_recv (router, identity, sizeof (identity), 0));
    msg_size =
      TEST_ASSERT_SUCCESS_ERRNO (zmq_recv (router, msg, sizeof (msg), 0));
    (void) id_size;
    (void) msg_size;

    // Router replies to specific dealers
    TEST_ASSERT_SUCCESS_ERRNO (zmq_send (router, "D1", 2, ZMQ_SNDMORE));
    send_string_expect_success (router, "reply_to_d1", 0);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_send (router, "D2", 2, ZMQ_SNDMORE));
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
    void *router = test_context_socket (ZMQ_ROUTER);
    void *dealer1 = test_context_socket (ZMQ_DEALER);
    void *dealer2 = test_context_socket (ZMQ_DEALER);

    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (dealer1, ZMQ_ROUTING_ID, "D1", 2));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (dealer2, ZMQ_ROUTING_ID, "D2", 2));

    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_bind (router, "inproc://test_router_multi_dealers"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_connect (dealer1, "inproc://test_router_multi_dealers"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_connect (dealer2, "inproc://test_router_multi_dealers"));

    // Both dealers send messages
    send_string_expect_success (dealer1, "from_dealer1", 0);
    send_string_expect_success (dealer2, "from_dealer2", 0);

    // Router receives both
    char identity[32];
    char msg[64];
    int id_size, msg_size;

    id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zmq_recv (router, identity, sizeof (identity), 0));
    msg_size =
      TEST_ASSERT_SUCCESS_ERRNO (zmq_recv (router, msg, sizeof (msg), 0));

    id_size =
      TEST_ASSERT_SUCCESS_ERRNO (zmq_recv (router, identity, sizeof (identity), 0));
    msg_size =
      TEST_ASSERT_SUCCESS_ERRNO (zmq_recv (router, msg, sizeof (msg), 0));
    (void) id_size;
    (void) msg_size;

    // Router replies to specific dealers
    TEST_ASSERT_SUCCESS_ERRNO (zmq_send (router, "D1", 2, ZMQ_SNDMORE));
    send_string_expect_success (router, "reply_to_d1", 0);

    TEST_ASSERT_SUCCESS_ERRNO (zmq_send (router, "D2", 2, ZMQ_SNDMORE));
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
    RUN_TEST (test_router_dealer_tcp);
    RUN_TEST (test_router_dealer_ipc);
    RUN_TEST (test_router_dealer_inproc);
    RUN_TEST (test_router_router_tcp);
    RUN_TEST (test_router_router_ipc);
    RUN_TEST (test_router_router_inproc);
    RUN_TEST (test_router_multiple_dealers_tcp);
    RUN_TEST (test_router_multiple_dealers_ipc);
    RUN_TEST (test_router_multiple_dealers_inproc);
    return UNITY_END ();
}
