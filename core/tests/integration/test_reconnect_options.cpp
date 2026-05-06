/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_monitoring.hpp"
#include "testutil_unity.hpp"

#include <unity.h>

namespace
{
void expect_monitor_sequence (test_monitor_probe_t *probe_,
                              const uint64_t *expected_,
                              int count_,
                              int timeout_ms_)
{
    TEST_ASSERT_TRUE (
      test_monitor_probe_wait_count (probe_, count_, timeout_ms_));
    for (int i = 0; i < count_; ++i)
        TEST_ASSERT_EQUAL_UINT64 (
          expected_[i], test_monitor_probe_event_at (probe_, i));
}

}

// test behavior with (mostly) default values
void reconnect_default ()
{
    void *pub = test_context_socket (ZLINK_SOCKET_PUB);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (pub, ENDPOINT_0));

    void *sub = test_context_socket (ZLINK_SOCKET_SUB);
    test_monitor_probe_t probe;
    void *monitor = open_test_monitor_probe (sub, ZLINK_EVENT_ALL, &probe);

    int interval = 60 * 1000;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (sub, ZLINK_OPT_RECONNECT_IVL, &interval, sizeof (interval)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sub, ENDPOINT_0));

    const uint64_t initial_events[] = {ZLINK_EVENT_CONNECT_DELAYED,
                                       ZLINK_EVENT_CONNECTED,
                                       ZLINK_EVENT_CONNECTION_READY};
    expect_monitor_sequence (&probe, initial_events,
                             sizeof (initial_events) / sizeof (initial_events[0]),
                             3000);

    test_context_socket_close_zero_linger (pub);

    const uint64_t disconnect_events[] = {
      ZLINK_EVENT_CONNECT_DELAYED,  ZLINK_EVENT_CONNECTED,
      ZLINK_EVENT_CONNECTION_READY, ZLINK_EVENT_DISCONNECTED,
      ZLINK_EVENT_CONNECTION_READY, ZLINK_EVENT_CONNECT_RETRIED};
    expect_monitor_sequence (
      &probe, disconnect_events,
      sizeof (disconnect_events) / sizeof (disconnect_events[0]), 3000);
    TEST_ASSERT_TRUE (
      test_monitor_probe_wait_no_additional (&probe, 6, 2000));

    close_test_monitor_probe (&monitor, &probe);
    test_context_socket_close_zero_linger (sub);
}

// test successful reconnect
void reconnect_success ()
{
    void *pub = test_context_socket (ZLINK_SOCKET_PUB);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (pub, ENDPOINT_0));

    void *sub = test_context_socket (ZLINK_SOCKET_SUB);
    test_monitor_probe_t probe;
    void *monitor = open_test_monitor_probe (sub, ZLINK_EVENT_ALL, &probe);

    int interval = 1 * 1000;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (sub, ZLINK_OPT_RECONNECT_IVL, &interval, sizeof (interval)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sub, ENDPOINT_0));

    const uint64_t initial_events[] = {ZLINK_EVENT_CONNECT_DELAYED,
                                       ZLINK_EVENT_CONNECTED,
                                       ZLINK_EVENT_CONNECTION_READY};
    expect_monitor_sequence (&probe, initial_events,
                             sizeof (initial_events) / sizeof (initial_events[0]),
                             3000);

    test_context_socket_close_zero_linger (pub);

    const uint64_t reconnect_wait_events[] = {
      ZLINK_EVENT_CONNECT_DELAYED,  ZLINK_EVENT_CONNECTED,
      ZLINK_EVENT_CONNECTION_READY, ZLINK_EVENT_DISCONNECTED,
      ZLINK_EVENT_CONNECTION_READY, ZLINK_EVENT_CONNECT_RETRIED};
    expect_monitor_sequence (
      &probe, reconnect_wait_events,
      sizeof (reconnect_wait_events) / sizeof (reconnect_wait_events[0]), 3000);
    TEST_ASSERT_TRUE (
      test_monitor_probe_wait_no_additional (&probe, 6, SETTLE_TIME));

    pub = test_context_socket (ZLINK_SOCKET_PUB);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (pub, ENDPOINT_0));

    const uint64_t reconnect_success_events[] = {
      ZLINK_EVENT_CONNECT_DELAYED,  ZLINK_EVENT_CONNECTED,
      ZLINK_EVENT_CONNECTION_READY, ZLINK_EVENT_DISCONNECTED,
      ZLINK_EVENT_CONNECTION_READY, ZLINK_EVENT_CONNECT_RETRIED,
      ZLINK_EVENT_CONNECT_DELAYED,  ZLINK_EVENT_CONNECTED,
      ZLINK_EVENT_CONNECTION_READY};
    expect_monitor_sequence (
      &probe, reconnect_success_events,
      sizeof (reconnect_success_events)
        / sizeof (reconnect_success_events[0]),
      3000);
    TEST_ASSERT_TRUE (
      test_monitor_probe_wait_no_additional (&probe, 9, SETTLE_TIME));

    close_test_monitor_probe (&monitor, &probe);
    test_context_socket_close_zero_linger (sub);
    test_context_socket_close_zero_linger (pub);
}

void setUp ()
{
    setup_test_context ();
}

void tearDown ()
{
    teardown_test_context ();
}

int main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();

    RUN_TEST (reconnect_default);
    RUN_TEST (reconnect_success);
    return UNITY_END ();
}
