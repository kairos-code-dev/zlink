/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <atomic>
#include <cstring>

SETUP_TEARDOWN_TESTCONTEXT

void test_with_handover ()
{
    char my_endpoint[MAX_SOCKET_STRING];
    void *router = test_context_socket (ZLINK_SOCKET_ROUTER);
    bind_loopback_ipv4 (router, my_endpoint, sizeof my_endpoint);

    const int duplicate_policy = ZLINK_RID_DUPLICATE_HANDOVER;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_option (router, ZLINK_OPT_RID_DUPLICATE_POLICY,
                                                 &duplicate_policy, sizeof (duplicate_policy)));

    void *dealer_one = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer_one, "X", 1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer_one, my_endpoint));

    char buffer[255];
    send_string_expect_success (dealer_one, "Hello", 0);
    recv_string_expect_success (router, "X", 0);
    recv_string_expect_success (router, "Hello", 0);

    void *dealer_two = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer_two, "X", 1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer_two, my_endpoint));
    send_string_expect_success (dealer_two, "Hello", 0);
    recv_string_expect_success (router, "X", 0);
    recv_string_expect_success (router, "Hello", 0);

    send_string_expect_success (router, "X", ZLINK_SNDMORE);
    send_string_expect_success (router, "Hello", 0);

    const int timeout = SETTLE_TIME;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (dealer_one, ZLINK_OPT_RCVTIMEO, &timeout, sizeof timeout));
    TEST_ASSERT_FAILURE_ERRNO (EAGAIN, zlink_recv (dealer_one, buffer, 255, 0));
    recv_string_expect_success (dealer_two, "Hello", 0);

    test_context_socket_close (router);
    test_context_socket_close (dealer_one);
    test_context_socket_close (dealer_two);
}

void test_without_handover ()
{
    size_t len = MAX_SOCKET_STRING;
    char my_endpoint[MAX_SOCKET_STRING];
    void *router = test_context_socket (ZLINK_SOCKET_ROUTER);
    const int duplicate_policy = ZLINK_RID_DUPLICATE_REJECT;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_option (router, ZLINK_OPT_RID_DUPLICATE_POLICY,
                                                 &duplicate_policy, sizeof (duplicate_policy)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (router, "tcp://127.0.0.1:*"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_option (router, ZLINK_OPT_LAST_ENDPOINT, my_endpoint, &len));

    void *dealer_one = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer_one, "X", 1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer_one, my_endpoint));

    char buffer[255];
    send_string_expect_success (dealer_one, "Hello", 0);
    recv_string_expect_success (router, "X", 0);
    recv_string_expect_success (router, "Hello", 0);

    void *dealer_two = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (dealer_two, "X", 1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (dealer_two, my_endpoint));
    send_string_expect_success (dealer_two, "Hello", 0);

    const int timeout = SETTLE_TIME;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (router, ZLINK_OPT_RCVTIMEO, &timeout, sizeof timeout));
    TEST_ASSERT_FAILURE_ERRNO (EAGAIN, zlink_recv (router, buffer, 255, 0));

    send_string_expect_success (router, "X", ZLINK_SNDMORE);
    send_string_expect_success (router, "Hello", 0);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (dealer_two, ZLINK_OPT_RCVTIMEO, &timeout, sizeof timeout));
    TEST_ASSERT_FAILURE_ERRNO (EAGAIN, zlink_recv (dealer_two, buffer, 255, 0));
    recv_string_expect_success (dealer_one, "Hello", 0);

    test_context_socket_close (router);
    test_context_socket_close (dealer_one);
    test_context_socket_close (dealer_two);
}

namespace
{
std::atomic<bool> reply_completed (false);

void ignore_reply (zlink_request_result_t, zlink_msg_t *, size_t, void *)
{
    reply_completed.store (true);
}

void set_connect_routing_id (void *router_, const char *routing_id_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_router_option (
      router_, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID, routing_id_, strlen (routing_id_)));
}

void send_request_to_activate_callback_dispatch (void *client_, void *server_)
{
    reply_completed.store (false);
    zlink_routing_id_t peer_rid;
    memset (&peer_rid, 0, sizeof peer_rid);
    peer_rid.size = 1;
    peer_rid.data[0] = 'S';

    zlink_msg_t request;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&request, 4));
    memcpy (zlink_msg_data (&request), "ping", 4);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_SUBMIT_OK,
      zlink_router_request (client_, &peer_rid, &request, 1, &ignore_reply, NULL, 0, 30000));

    const zlink_routing_id_t *source_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_EQUAL_INT (ZLINK_RECV_OK,
                           zlink_router_recv (server_, &source_rid, &source_spot_rid,
                                              &request_seq, &parts, &part_count, 0));
    TEST_ASSERT_EQUAL_UINT64 (1, part_count);

    zlink_msg_t reply;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&reply, 4));
    memcpy (zlink_msg_data (&reply), "pong", 4);
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_router_reply (server_, source_rid, request_seq, &reply, 1));
    zlink_multipart_close (parts, part_count);

    const int attempts = 1000;
    for (int i = 0; i < attempts && !reply_completed.load (); ++i) {
        const zlink_routing_id_t *ignored_source = NULL;
        const zlink_routing_id_t *ignored_spot = NULL;
        uint64_t ignored_seq = 0;
        zlink_msg_t *ignored_parts = NULL;
        size_t ignored_count = 0;
        if (zlink_router_recv (client_, &ignored_source, &ignored_spot, &ignored_seq,
                               &ignored_parts, &ignored_count, ZLINK_DONTWAIT)
              == ZLINK_RECV_OK)
            zlink_multipart_close (ignored_parts, ignored_count);
        msleep (1);
    }
    TEST_ASSERT_TRUE (reply_completed.load ());
}
}

void test_callback_dispatch_same_direction_reconnect_handover ()
{
    const int handover = ZLINK_RID_DUPLICATE_HANDOVER;
    const int zero = 0;
    const int recovery_timeout = 5000;
    char endpoint_one[MAX_SOCKET_STRING];
    char endpoint_two[MAX_SOCKET_STRING];

    void *server_one = test_context_socket (ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (server_one, "S", 1));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (server_one, ZLINK_OPT_LINGER, &zero, sizeof zero));
    bind_loopback_ipv4 (server_one, endpoint_one, sizeof endpoint_one);

    void *server_two = test_context_socket (ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (server_two, "S", 1));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (server_two, ZLINK_OPT_LINGER, &zero, sizeof zero));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (server_two, ZLINK_OPT_RCVTIMEO, &recovery_timeout,
                        sizeof recovery_timeout));
    bind_loopback_ipv4 (server_two, endpoint_two, sizeof endpoint_two);

    void *client = test_context_socket (ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (client, "C", 1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_option (client, ZLINK_OPT_LINGER, &zero, sizeof zero));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (client, ZLINK_OPT_RID_DUPLICATE_POLICY, &handover, sizeof handover));
    set_connect_routing_id (client, "S");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint_one));
    msleep (SETTLE_TIME);

    // This installs callback dispatch and records traffic on the original pipe.
    send_request_to_activate_callback_dispatch (client, server_one);

    // A freshly established same-direction pipe with the same routing ID must
    // replace the prior pipe even though the prior pipe has traffic history.
    set_connect_routing_id (client, "S");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint_two));
    msleep (SETTLE_TIME);

    send_string_expect_success (client, "S", ZLINK_SNDMORE);
    send_string_expect_success (client, "recovered", 0);
    recv_string_expect_success (server_two, "C", 0);
    recv_string_expect_success (server_two, "recovered", 0);

    test_context_socket_close_zero_linger (client);
    test_context_socket_close_zero_linger (server_two);
    test_context_socket_close_zero_linger (server_one);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_with_handover);
    RUN_TEST (test_without_handover);
    RUN_TEST (test_callback_dispatch_same_direction_reconnect_handover);
    return UNITY_END ();
}
