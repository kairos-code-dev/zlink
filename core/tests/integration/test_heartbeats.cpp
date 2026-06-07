/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"
#include "testutil_monitoring.hpp"

#if !defined ZLINK_HAVE_WINDOWS
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
    void *server = test_context_socket (ZLINK_SOCKET_ROUTER);
    char endpoint[MAX_SOCKET_STRING];
    test_monitor_probe_t probe;

    // Set a very short handshake timeout (100ms)
    int timeout = 100;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (server, ZLINK_OPT_HANDSHAKE_IVL, &timeout, sizeof (timeout)));

    int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (server, ZLINK_OPT_LINGER, &linger, sizeof (linger)));

    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    void *monitor =
      open_test_monitor_probe (server, ZLINK_EVENT_ACCEPTED | ZLINK_EVENT_DISCONNECTED, &probe);

    // Connect a raw socket but don't send ZMTP greeting
    fd_t fd = connect_socket (endpoint);

    TEST_ASSERT_TRUE (test_monitor_probe_wait_count (&probe, 2, 3000));
    TEST_ASSERT_EQUAL_UINT64 (ZLINK_EVENT_ACCEPTED, test_monitor_probe_event_at (&probe, 0));
    TEST_ASSERT_EQUAL_UINT64 (ZLINK_EVENT_DISCONNECTED, test_monitor_probe_event_at (&probe, 1));

    close (fd);
    close_test_monitor_probe (&monitor, &probe);
    test_context_socket_close (server);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_handshake_timeout);
    return UNITY_END ();
}
