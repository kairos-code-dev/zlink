/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"
#include "core/object.hpp"
#include "core/pipe.hpp"
#include "sockets/internal/lb.hpp"

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
    void *socket = zlink_socket (get_test_context (), static_cast<zlink_socket_type_t> (type_));
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
    void *router = create_sync_socket (ZLINK_SOCKET_ROUTER);
    void *dealer1 = create_sync_socket (ZLINK_SOCKET_DEALER);
    void *dealer2 = create_sync_socket (ZLINK_SOCKET_DEALER);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer1, "D1", 2));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer2, "D2", 2));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, "tcp://127.0.0.1:*"));

    char endpoint[MAX_SOCKET_STRING];
    size_t len = sizeof (endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_option (router, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len));

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
    void *router = create_sync_socket (ZLINK_SOCKET_ROUTER);
    void *dealer1 = create_sync_socket (ZLINK_SOCKET_DEALER);
    void *dealer2 = create_sync_socket (ZLINK_SOCKET_DEALER);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer1, "D1", 2));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer2, "D2", 2));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, "ipc://*"));

    char endpoint[MAX_SOCKET_STRING];
    size_t len = sizeof (endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_option (router, ZLINK_OPT_LAST_ENDPOINT, endpoint, &len));

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
    void *router = create_sync_socket (ZLINK_SOCKET_ROUTER);
    void *dealer1 = create_sync_socket (ZLINK_SOCKET_DEALER);
    void *dealer2 = create_sync_socket (ZLINK_SOCKET_DEALER);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer1, "D1", 2));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer2, "D2", 2));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, "inproc://test_router_multi_dealers"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer1, "inproc://test_router_multi_dealers"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer2, "inproc://test_router_multi_dealers"));

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

void test_weighted_dealer_preserves_peer_weight_after_backpressure ()
{
    void *dealer = create_sync_socket (ZLINK_SOCKET_DEALER);
    void *router1 = create_sync_socket (ZLINK_SOCKET_ROUTER);
    void *router2 = create_sync_socket (ZLINK_SOCKET_ROUTER);

    const int hwm = 1;
    const int timeout = 0;
    const int weight1 = 25;
    const int weight2 = 100;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (dealer, ZLINK_OPT_SNDHWM, &hwm, sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (dealer, ZLINK_OPT_SNDTIMEO, &timeout, sizeof (timeout)));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_set_router_option (
        router1, ZLINK_ROUTER_OPT_WEIGHT, &weight1, sizeof (weight1)));
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_OK,
      zlink_set_router_option (
        router2, ZLINK_ROUTER_OPT_WEIGHT, &weight2, sizeof (weight2)));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (router1, "inproc://weighted-backpressure-1"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (router2, "inproc://weighted-backpressure-2"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (dealer, "inproc://weighted-backpressure-1"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (dealer, "inproc://weighted-backpressure-2"));
    msleep (SETTLE_TIME);

    bool backpressured = false;
    for (int i = 0; i < 1000; ++i) {
        if (zlink_send (dealer, "fill", 4, ZLINK_DONTWAIT) == -1) {
            TEST_ASSERT_TRUE (errno == EAGAIN || errno == ECONNREFUSED);
            backpressured = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE (
      backpressured, "weighted dealer did not reach the backpressure path");

    char buffer[16];
    while (zlink_recv (router1, buffer, sizeof (buffer), ZLINK_DONTWAIT) >= 0) {
    }
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);
    while (zlink_recv (router2, buffer, sizeof (buffer), ZLINK_DONTWAIT) >= 0) {
    }
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);
    msleep (SETTLE_TIME);
    while (zlink_recv (router1, buffer, sizeof (buffer), ZLINK_DONTWAIT) >= 0) {
    }
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);
    while (zlink_recv (router2, buffer, sizeof (buffer), ZLINK_DONTWAIT) >= 0) {
    }
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);

    int router1_received = 0;
    int router2_received = 0;
    for (int i = 0; i < 40; ++i) {
        int send_rc = -1;
        for (int attempt = 0; attempt < 1000 && send_rc == -1; ++attempt) {
            send_rc = zlink_send (dealer, "next", 4, ZLINK_DONTWAIT);
            if (send_rc == -1)
                msleep (1);
        }
        TEST_ASSERT_EQUAL_INT (4, send_rc);

        bool received = false;
        for (int attempt = 0; attempt < 1000 && !received; ++attempt) {
            void *routers[] = {router1, router2};
            int *counts[] = {&router1_received, &router2_received};
            for (size_t router_index = 0; router_index < 2; ++router_index) {
                const int rid_size =
                  zlink_recv (routers[router_index], buffer, sizeof (buffer), ZLINK_DONTWAIT);
                if (rid_size < 0)
                    continue;
                TEST_ASSERT_GREATER_THAN_INT (0, rid_size);
                TEST_ASSERT_EQUAL_INT (
                  4, zlink_recv (routers[router_index], buffer, sizeof (buffer), 0));
                TEST_ASSERT_EQUAL_MEMORY ("next", buffer, 4);
                *counts[router_index] += 1;
                received = true;
                break;
            }
            if (!received)
                msleep (1);
        }
        TEST_ASSERT_TRUE (received);
    }

    TEST_ASSERT_GREATER_THAN_INT (
      0, router1_received);
    TEST_ASSERT_GREATER_THAN_INT (
      0, router2_received);

    close_sync_socket (router2);
    close_sync_socket (router1);
    close_sync_socket (dealer);
}

void test_weighted_lb_reactivation_keeps_configured_weight ()
{
    zlink::object_t parent (NULL, 0);
    zlink::object_t *parents[] = {&parent, &parent};
    const int hwms[] = {1, 1};
    const bool conflate[] = {false, false};
    zlink::pipe_t *first_pair[2];
    zlink::pipe_t *second_pair[2];
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::pipepair (parents, first_pair, hwms, conflate));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::pipepair (parents, second_pair, hwms, conflate));

    zlink::lb_t lb;
    lb.attach (first_pair[0]);
    lb.attach (second_pair[0]);
    lb.set_weight (first_pair[0], 25);
    lb.set_weight (second_pair[0], 100);

    zlink::msg_t message;
    TEST_ASSERT_SUCCESS_ERRNO (message.init_size (1));
    bool backpressured = false;
    for (int i = 0; i < 16; ++i) {
        if (lb.send (&message) != 0) {
            backpressured = true;
            break;
        }
    }
    TEST_ASSERT_TRUE (backpressured);

    // A failed write changes only writability. The configured routing policy
    // must remain available when the pipe reports fresh write credit.
    TEST_ASSERT_EQUAL_UINT32 (25, lb.weight (first_pair[0]));
    TEST_ASSERT_EQUAL_UINT32 (100, lb.weight (second_pair[0]));
    first_pair[0]->refresh_write_credit (1);
    second_pair[0]->refresh_write_credit (1);
    lb.activated (first_pair[0]);
    lb.activated (second_pair[0]);
    TEST_ASSERT_SUCCESS_ERRNO (lb.send (&message));

    TEST_ASSERT_SUCCESS_ERRNO (message.close ());
    lb.pipe_terminated (first_pair[0]);
    lb.pipe_terminated (second_pair[0]);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_router_multiple_dealers_tcp);
    RUN_TEST (test_router_multiple_dealers_ipc);
    RUN_TEST (test_router_multiple_dealers_inproc);
    RUN_TEST (test_weighted_dealer_preserves_peer_weight_after_backpressure);
    RUN_TEST (test_weighted_lb_reactivation_keeps_configured_weight);
    return UNITY_END ();
}
