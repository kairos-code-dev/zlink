/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <mutex>
#include <string.h>

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

bool endpoint_uses_ipv6 (const char *endpoint_)
{
    return endpoint_ && strchr (endpoint_, '[') != NULL;
}

void configure_socket_family_for_endpoint (void *socket_, const char *endpoint_)
{
    TEST_ASSERT_NOT_NULL (socket_);
    const int ipv6 = endpoint_uses_ipv6 (endpoint_) ? 1 : 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (socket_, ZLINK_OPT_IPV6, &ipv6, sizeof (ipv6)));
}

void run_reconnect_ivl_case (const char *bind_endpoint_,
                             const char *connect_endpoint_)
{
    void *server = test_context_socket (ZLINK_SOCKET_PAIR);
    configure_socket_family_for_endpoint (server, bind_endpoint_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server, bind_endpoint_));

    void *client = test_context_socket (ZLINK_SOCKET_PAIR);
    configure_socket_family_for_endpoint (client, connect_endpoint_);
    monitor_probe_t probe;
    void *monitor = open_monitor (client, &probe);

    int interval = 100;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (client, ZLINK_OPT_RECONNECT_IVL, &interval, sizeof (interval)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, connect_endpoint_));

    const uint64_t initial_events[] = {ZLINK_EVENT_CONNECT_DELAYED,
                                       ZLINK_EVENT_CONNECTED,
                                       ZLINK_EVENT_CONNECTION_READY};
    expect_monitor_sequence (&probe, initial_events,
                             sizeof (initial_events) / sizeof (initial_events[0]),
                             3000);

    test_context_socket_close_zero_linger (server);

    const uint64_t reconnect_wait_events[] = {
      ZLINK_EVENT_CONNECT_DELAYED,  ZLINK_EVENT_CONNECTED,
      ZLINK_EVENT_CONNECTION_READY, ZLINK_EVENT_DISCONNECTED,
      ZLINK_EVENT_CONNECT_RETRIED};
    expect_monitor_sequence (
      &probe, reconnect_wait_events,
      sizeof (reconnect_wait_events) / sizeof (reconnect_wait_events[0]), 3000);

    server = test_context_socket (ZLINK_SOCKET_PAIR);
    configure_socket_family_for_endpoint (server, connect_endpoint_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server, connect_endpoint_));

    const uint64_t reconnect_success_events[] = {
      ZLINK_EVENT_CONNECT_DELAYED,  ZLINK_EVENT_CONNECTED,
      ZLINK_EVENT_CONNECTION_READY, ZLINK_EVENT_DISCONNECTED,
      ZLINK_EVENT_CONNECT_RETRIED,  ZLINK_EVENT_CONNECT_DELAYED,
      ZLINK_EVENT_CONNECTED,        ZLINK_EVENT_CONNECTION_READY};
    expect_monitor_sequence (
      &probe, reconnect_success_events,
      sizeof (reconnect_success_events)
        / sizeof (reconnect_success_events[0]),
      5000);

    // Ignore teardown events from the completed case before the next case
    // reuses the global monitor callback target.
    g_monitor_probe = NULL;
    close_monitor_handle (&monitor);
    test_context_socket_close_zero_linger (client);
    test_context_socket_close_zero_linger (server);
}
}

SETUP_TEARDOWN_TESTCONTEXT

#if defined(ZLINK_HAVE_IPC) && !defined(ZLINK_HAVE_GNU)
void test_reconnect_ivl_ipc (void)
{
    char my_endpoint[256];
    make_random_ipc_endpoint (my_endpoint);
    run_reconnect_ivl_case (my_endpoint, my_endpoint);
}
#endif

void test_reconnect_ivl_tcp_ipv4 ()
{
    char endpoint[MAX_SOCKET_STRING];
    void *server = test_context_socket (ZLINK_SOCKET_PAIR);
    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));
    test_context_socket_close_zero_linger (server);
    run_reconnect_ivl_case (endpoint, endpoint);
}

void test_reconnect_ivl_tcp_ipv6 ()
{
    if (!is_ipv6_available ())
        TEST_FAIL_MESSAGE ("IPv6 must be available in this environment");

    char endpoint[MAX_SOCKET_STRING];
    void *server = test_context_socket (ZLINK_SOCKET_PAIR);
    bind_loopback_ipv6 (server, endpoint, sizeof (endpoint));
    test_context_socket_close_zero_linger (server);
    run_reconnect_ivl_case (endpoint, endpoint);
}

int main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();
#if defined(ZLINK_HAVE_IPC) && !defined(ZLINK_HAVE_GNU)
    RUN_TEST (test_reconnect_ivl_ipc);
#endif
    RUN_TEST (test_reconnect_ivl_tcp_ipv4);
    RUN_TEST (test_reconnect_ivl_tcp_ipv6);

    return UNITY_END ();
}
