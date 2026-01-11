/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT


void test_get_peer_state ()
{
}

void test_get_peer_state_corner_cases ()
{
}

void test_basic ()
{
    char my_endpoint[MAX_SOCKET_STRING];
    void *router = test_context_socket (ZMQ_ROUTER);
    bind_loopback_ipv4 (router, my_endpoint, sizeof my_endpoint);

    //  Send a message to an unknown peer with the default setting
    //  This will not report any error
    send_string_expect_success (router, "UNKNOWN", ZMQ_SNDMORE);
    send_string_expect_success (router, "DATA", 0);

    //  Send a message to an unknown peer with mandatory routing
    //  This will fail
    int mandatory = 1;
    TEST_ASSERT_SUCCESS_ERRNO (zmq_setsockopt (router, ZMQ_ROUTER_MANDATORY,
                                               &mandatory, sizeof (mandatory)));
    int rc = zmq_send (router, "UNKNOWN", 7, ZMQ_SNDMORE);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EHOSTUNREACH, errno);

    //  Create dealer called "X" and connect it to our router
    void *dealer = test_context_socket (ZMQ_DEALER);
    TEST_ASSERT_SUCCESS_ERRNO (zmq_setsockopt (dealer, ZMQ_ROUTING_ID, "X", 1));
    TEST_ASSERT_SUCCESS_ERRNO (zmq_connect (dealer, my_endpoint));

    //  Get message from dealer to know when connection is ready
    send_string_expect_success (dealer, "Hello", 0);
    recv_string_expect_success (router, "X", 0);

    //  Send a message to connected dealer now
    //  It should work
    send_string_expect_success (router, "X", ZMQ_SNDMORE);
    send_string_expect_success (router, "Hello", 0);

    test_context_socket_close (router);
    test_context_socket_close (dealer);
}

int main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_basic);
    RUN_TEST (test_get_peer_state);
    RUN_TEST (test_get_peer_state_corner_cases);

    return UNITY_END ();
}
