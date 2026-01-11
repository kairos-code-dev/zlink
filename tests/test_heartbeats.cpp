/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"
#include "testutil_monitoring.hpp"

#include <string.h>

#if !defined ZMQ_HAVE_WINDOWS
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

void setUp ()
{
    setup_test_context ();
}

void tearDown ()
{
    teardown_test_context ();
}

void test_handshake_timeout ()
{
    void *server = test_context_socket (ZMQ_ROUTER);
    void *mon = test_context_socket (ZMQ_PAIR);
    char endpoint[MAX_SOCKET_STRING];

    // Set a very short handshake timeout (100ms)
    int timeout = 100;
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (server, ZMQ_HANDSHAKE_IVL, &timeout, sizeof (timeout)));

    int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_setsockopt (server, ZMQ_LINGER, &linger, sizeof (linger)));

    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    TEST_ASSERT_SUCCESS_ERRNO (zmq_socket_monitor (
      server, "inproc://monitor-handshake",
      ZMQ_EVENT_ACCEPTED | ZMQ_EVENT_DISCONNECTED));

    TEST_ASSERT_SUCCESS_ERRNO (
      zmq_connect (mon, "inproc://monitor-handshake"));

    // Connect a raw socket but don't send ZMTP greeting
    fd_t fd = connect_socket (endpoint);

    // Expect accepted event
    expect_monitor_event (mon, ZMQ_EVENT_ACCEPTED);

    // Expect disconnected event due to handshake timeout
    expect_monitor_event (mon, ZMQ_EVENT_DISCONNECTED);

    close (fd);
    test_context_socket_close (server);
    test_context_socket_close (mon);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_handshake_timeout);
    return UNITY_END ();
}