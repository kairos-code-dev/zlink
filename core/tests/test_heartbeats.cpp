/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"
#include "testutil_monitoring.hpp"

#include <mutex>
#include <string.h>

#if !defined ZLINK_HAVE_WINDOWS
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

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
    uint64_t events[8];
};

monitor_probe_t *g_monitor_probe = NULL;

void record_monitor_event (const zlink_monitor_event_t *event_)
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

void close_monitor_handle (void **monitor_p_)
{
    if (!monitor_p_ || !*monitor_p_)
        return;

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (*monitor_p_, ZLINK_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (*monitor_p_));
    *monitor_p_ = NULL;
}
}

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
    void *server = test_context_socket (ZLINK_ROUTER);
    char endpoint[MAX_SOCKET_STRING];
    monitor_probe_t probe;
    g_monitor_probe = &probe;

    // Set a very short handshake timeout (100ms)
    int timeout = 100;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_HANDSHAKE_IVL, &timeout, sizeof (timeout)));

    int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (server, ZLINK_LINGER, &linger, sizeof (linger)));

    bind_loopback_ipv4 (server, endpoint, sizeof (endpoint));

    void *monitor = zlink_socket_monitor_open (
      server, ZLINK_EVENT_ACCEPTED | ZLINK_EVENT_DISCONNECTED,
      &record_monitor_event);
    TEST_ASSERT_NOT_NULL (monitor);

    // Connect a raw socket but don't send ZMTP greeting
    fd_t fd = connect_socket (endpoint);

    TEST_ASSERT_TRUE (wait_for_monitor_event_count (&probe, 2, 3000));
    TEST_ASSERT_EQUAL_UINT64 (ZLINK_EVENT_ACCEPTED, monitor_event_at (&probe, 0));
    TEST_ASSERT_EQUAL_UINT64 (ZLINK_EVENT_DISCONNECTED,
                              monitor_event_at (&probe, 1));

    close (fd);
    close_monitor_handle (&monitor);
    test_context_socket_close (server);
    g_monitor_probe = NULL;
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_handshake_timeout);
    return UNITY_END ();
}
