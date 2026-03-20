/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <atomic>
#include <mutex>
#include <string.h>
#include <unity.h>

namespace
{
struct monitor_probe_t
{
    monitor_probe_t () : count (0)
    {
        memset (events, 0, sizeof (events));
    }

    std::mutex sync;
    int count;
    uint64_t events[16];
};

monitor_probe_t *g_monitor_probe = NULL;

void record_monitor_event (const zlink_monitor_event_t *event_, void *)
{
    if (!g_monitor_probe || !event_)
        return;

    std::lock_guard<std::mutex> lock (g_monitor_probe->sync);
    if (g_monitor_probe->count
        < static_cast<int> (sizeof (g_monitor_probe->events)
                            / sizeof (g_monitor_probe->events[0]))) {
        g_monitor_probe->events[g_monitor_probe->count] = event_->event;
    }
    ++g_monitor_probe->count;
}

int monitor_event_count (monitor_probe_t *probe_)
{
    std::lock_guard<std::mutex> lock (probe_->sync);
    return probe_->count;
}

uint64_t monitor_event_at (monitor_probe_t *probe_, int index_)
{
    std::lock_guard<std::mutex> lock (probe_->sync);
    return probe_->events[index_];
}

bool wait_for_monitor_event_count (monitor_probe_t *probe_,
                                   int expected_,
                                   int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;

    for (int i = 0; i < attempts; ++i) {
        if (monitor_event_count (probe_) >= expected_)
            return true;
        msleep (step_ms);
    }
    return monitor_event_count (probe_) >= expected_;
}

bool wait_for_no_additional_monitor_events (monitor_probe_t *probe_,
                                            int baseline_,
                                            int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;

    for (int i = 0; i < attempts; ++i) {
        if (monitor_event_count (probe_) > baseline_)
            return false;
        msleep (step_ms);
    }
    return monitor_event_count (probe_) == baseline_;
}

void expect_monitor_sequence (monitor_probe_t *probe_,
                              const uint64_t *expected_,
                              int count_,
                              int timeout_ms_)
{
    TEST_ASSERT_TRUE (wait_for_monitor_event_count (probe_, count_, timeout_ms_));
    for (int i = 0; i < count_; ++i)
        TEST_ASSERT_EQUAL_UINT64 (expected_[i], monitor_event_at (probe_, i));
}

void close_monitor_handle (void **monitor_p_)
{
    if (!monitor_p_ || !*monitor_p_)
        return;

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (*monitor_p_, ZLINK_OPT_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (monitor_p_));
}

void *open_monitor (void *socket_, monitor_probe_t *probe_)
{
    g_monitor_probe = probe_;
    zlink_socket_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = ZLINK_EVENT_ALL;
    void *monitor = zlink_socket_monitor_open (socket_, &opts);
    TEST_ASSERT_NOT_NULL (monitor);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_socket_monitor_handler (monitor, &record_monitor_event, NULL));
    return monitor;
}
}

// test behavior with (mostly) default values
void reconnect_default ()
{
    void *pub = test_context_socket (ZLINK_SOCKET_PUB);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (pub, ENDPOINT_0));

    void *sub = test_context_socket (ZLINK_SOCKET_SUB);
    monitor_probe_t probe;
    void *monitor = open_monitor (sub, &probe);

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
      ZLINK_EVENT_CONNECT_RETRIED};
    expect_monitor_sequence (
      &probe, disconnect_events,
      sizeof (disconnect_events) / sizeof (disconnect_events[0]), 3000);
    TEST_ASSERT_TRUE (wait_for_no_additional_monitor_events (&probe, 5, 2000));

    close_monitor_handle (&monitor);
    test_context_socket_close_zero_linger (sub);
    g_monitor_probe = NULL;
}

// test successful reconnect
void reconnect_success ()
{
    void *pub = test_context_socket (ZLINK_SOCKET_PUB);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (pub, ENDPOINT_0));

    void *sub = test_context_socket (ZLINK_SOCKET_SUB);
    monitor_probe_t probe;
    void *monitor = open_monitor (sub, &probe);

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
      ZLINK_EVENT_CONNECT_RETRIED};
    expect_monitor_sequence (
      &probe, reconnect_wait_events,
      sizeof (reconnect_wait_events) / sizeof (reconnect_wait_events[0]), 3000);
    TEST_ASSERT_TRUE (
      wait_for_no_additional_monitor_events (&probe, 5, SETTLE_TIME));

    pub = test_context_socket (ZLINK_SOCKET_PUB);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (pub, ENDPOINT_0));

    const uint64_t reconnect_success_events[] = {
      ZLINK_EVENT_CONNECT_DELAYED,  ZLINK_EVENT_CONNECTED,
      ZLINK_EVENT_CONNECTION_READY, ZLINK_EVENT_DISCONNECTED,
      ZLINK_EVENT_CONNECT_RETRIED,  ZLINK_EVENT_CONNECT_DELAYED,
      ZLINK_EVENT_CONNECTED,        ZLINK_EVENT_CONNECTION_READY};
    expect_monitor_sequence (
      &probe, reconnect_success_events,
      sizeof (reconnect_success_events)
        / sizeof (reconnect_success_events[0]),
      3000);
    TEST_ASSERT_TRUE (
      wait_for_no_additional_monitor_events (&probe, 8, SETTLE_TIME));

    close_monitor_handle (&monitor);
    test_context_socket_close_zero_linger (sub);
    test_context_socket_close_zero_linger (pub);
    g_monitor_probe = NULL;
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
