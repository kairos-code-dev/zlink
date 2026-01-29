/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

const char *rconn1routing_id = "conn1";
const char *x_routing_id = "X";
const char *y_routing_id = "Y";
const char *z_routing_id = "Z";


void test_router_2_router (bool named_)
{
    char buff[256];
    const char msg[] = "hi 1";
    const int zero = 0;
    char my_endpoint[MAX_SOCKET_STRING];

    //  Create bind socket.
    void *rbind = test_context_socket (ZLINK_ROUTER);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (rbind, ZLINK_LINGER, &zero, sizeof (zero)));
    bind_loopback_ipv4 (rbind, my_endpoint, sizeof my_endpoint);

    //  Create connection socket.
    void *rconn1 = test_context_socket (ZLINK_ROUTER);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (rconn1, ZLINK_LINGER, &zero, sizeof (zero)));

    //  If we're in named mode, set some identities.
    if (named_) {
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_setsockopt (rbind, ZLINK_ROUTING_ID, x_routing_id, 1));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_setsockopt (rconn1, ZLINK_ROUTING_ID, y_routing_id, 1));
    }

    //  Make call to connect using a connect_routing_id.
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (rconn1, ZLINK_CONNECT_ROUTING_ID,
                                               rconn1routing_id,
                                               strlen (rconn1routing_id)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (rconn1, my_endpoint));
    /*  Uncomment to test assert on duplicate routing id
    //  Test duplicate connect attempt.
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (rconn1, ZLINK_CONNECT_ROUTING_ID, rconn1routing_id, strlen (rconn1routing_id)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (rconn1, bindip));
*/
    //  Send some data.

    send_string_expect_success (rconn1, rconn1routing_id, ZLINK_SNDMORE);
    send_string_expect_success (rconn1, msg, 0);

    //  Receive the name.
    const int routing_id_len = zlink_recv (rbind, buff, 256, 0);
    if (named_) {
        TEST_ASSERT_EQUAL_INT (strlen (y_routing_id), routing_id_len);
        TEST_ASSERT_EQUAL_STRING_LEN (y_routing_id, buff, routing_id_len);
    } else {
        TEST_ASSERT_TRUE (routing_id_len && 0 == buff[0]);
    }

    //  Receive the data.
    recv_string_expect_success (rbind, msg, 0);

    //  Send some data back.
    const int ret = zlink_send (rbind, buff, routing_id_len, ZLINK_SNDMORE);
    TEST_ASSERT_EQUAL_INT (routing_id_len, ret);
    send_string_expect_success (rbind, "ok", 0);

    //  If bound socket identity naming a problem, we'll likely see something funky here.
    recv_string_expect_success (rconn1, rconn1routing_id, 0);
    recv_string_expect_success (rconn1, "ok", 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_unbind (rbind, my_endpoint));
    test_context_socket_close (rbind);
    test_context_socket_close (rconn1);
}

void test_router_2_router_while_receiving ()
{
    char buff[256];
    const char msg[] = "hi 1";
    const int zero = 0;
    char x_endpoint[MAX_SOCKET_STRING];
    char z_endpoint[MAX_SOCKET_STRING];

    //  Create xbind socket.
    void *xbind = test_context_socket (ZLINK_ROUTER);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (xbind, ZLINK_LINGER, &zero, sizeof (zero)));
    bind_loopback_ipv4 (xbind, x_endpoint, sizeof x_endpoint);

    //  Create zbind socket.
    void *zbind = test_context_socket (ZLINK_ROUTER);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (zbind, ZLINK_LINGER, &zero, sizeof (zero)));
    bind_loopback_ipv4 (zbind, z_endpoint, sizeof z_endpoint);

    //  Create connection socket.
    void *yconn = test_context_socket (ZLINK_ROUTER);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (yconn, ZLINK_LINGER, &zero, sizeof (zero)));

    // set identities for each socket
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      xbind, ZLINK_ROUTING_ID, x_routing_id, strlen (x_routing_id)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (yconn, ZLINK_ROUTING_ID, y_routing_id, 2));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      zbind, ZLINK_ROUTING_ID, z_routing_id, strlen (z_routing_id)));

    //  Connect Y to X using a routing id
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      yconn, ZLINK_CONNECT_ROUTING_ID, x_routing_id, strlen (x_routing_id)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (yconn, x_endpoint));

    //  Send some data from Y to X.
    send_string_expect_success (yconn, x_routing_id, ZLINK_SNDMORE);
    send_string_expect_success (yconn, msg, 0);

    // wait for the Y->X message to be received
    msleep (SETTLE_TIME);

    // Now X tries to connect to Z and send a message
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      xbind, ZLINK_CONNECT_ROUTING_ID, z_routing_id, strlen (z_routing_id)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (xbind, z_endpoint));

    //  Try to send some data from X to Z.
    send_string_expect_success (xbind, z_routing_id, ZLINK_SNDMORE);
    send_string_expect_success (xbind, msg, 0);

    // wait for the X->Z message to be received (so that our non-blocking check will actually
    // fail if the message is routed to Y)
    msleep (SETTLE_TIME);

    // nothing should have been received on the Y socket
    TEST_ASSERT_FAILURE_ERRNO (EAGAIN,
                               zlink_recv (yconn, buff, 256, ZLINK_DONTWAIT));

    // the message should have been received on the Z socket
    recv_string_expect_success (zbind, x_routing_id, 0);
    recv_string_expect_success (zbind, msg, 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_unbind (xbind, x_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_unbind (zbind, z_endpoint));

    test_context_socket_close (yconn);
    test_context_socket_close (xbind);
    test_context_socket_close (zbind);
}

void test_router_2_router_unnamed ()
{
    test_router_2_router (false);
}

void test_router_2_router_named ()
{
    test_router_2_router (true);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_router_2_router_unnamed);
    RUN_TEST (test_router_2_router_named);
    RUN_TEST (test_router_2_router_while_receiving);
    return UNITY_END ();
}
