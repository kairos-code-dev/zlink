/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

SETUP_TEARDOWN_TESTCONTEXT

static void test_stats_ex_send_recv ()
{
    void *sender = test_context_socket (ZLINK_PAIR);
    void *receiver = test_context_socket (ZLINK_PAIR);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (sender, "inproc://stats_ex"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (receiver, "inproc://stats_ex"));
    msleep (SETTLE_TIME);

    const char payload[] = "hello";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_send (sender, payload, sizeof (payload), 0));

    char buffer[16] = {0};
    const int rc = zlink_recv (receiver, buffer, sizeof (buffer), 0);
    TEST_ASSERT_EQUAL_INT ((int) sizeof (payload), rc);

    zlink_socket_stats_ex_t sender_stats;
    zlink_socket_stats_ex_t receiver_stats;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_stats_ex (sender, &sender_stats));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_stats_ex (receiver, &receiver_stats));

    TEST_ASSERT_TRUE (sender_stats.msgs_sent >= 1);
    TEST_ASSERT_TRUE (sender_stats.bytes_sent >= sizeof (payload));
    TEST_ASSERT_TRUE (sender_stats.last_send_ms > 0);

    TEST_ASSERT_TRUE (receiver_stats.msgs_received >= 1);
    TEST_ASSERT_TRUE (receiver_stats.bytes_received >= sizeof (payload));
    TEST_ASSERT_TRUE (receiver_stats.last_recv_ms > 0);

    test_context_socket_close_zero_linger (receiver);
    test_context_socket_close_zero_linger (sender);
}

static void test_stats_ex_drop_no_peers ()
{
    void *sender = test_context_socket (ZLINK_DEALER);
    const int sndtimeo = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (sender, ZLINK_SNDTIMEO, &sndtimeo, sizeof (sndtimeo)));

    const char payload[] = "x";
    const int rc = zlink_send (sender, payload, sizeof (payload), ZLINK_DONTWAIT);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);

    zlink_socket_stats_ex_t stats;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_stats_ex (sender, &stats));
    TEST_ASSERT_TRUE (stats.msgs_dropped >= 1);
    TEST_ASSERT_TRUE (stats.drops_no_peers >= 1);

    test_context_socket_close_zero_linger (sender);
}

static void test_stats_ex_drop_hwm ()
{
    void *sender = test_context_socket (ZLINK_PAIR);
    void *receiver = test_context_socket (ZLINK_PAIR);

    const int hwm = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (sender, ZLINK_SNDHWM, &hwm, sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (receiver, ZLINK_RCVHWM, &hwm, sizeof (hwm)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (sender, "inproc://stats_ex_hwm"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (receiver, "inproc://stats_ex_hwm"));
    msleep (SETTLE_TIME);

    const char payload[] = "x";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_send (sender, payload, sizeof (payload), ZLINK_DONTWAIT));

    bool dropped = false;
    for (int i = 0; i < 1000; ++i) {
        if (zlink_send (sender, payload, sizeof (payload), ZLINK_DONTWAIT) == -1) {
            TEST_ASSERT_EQUAL_INT (EAGAIN, errno);
            dropped = true;
            break;
        }
    }
    TEST_ASSERT_TRUE (dropped);

    zlink_socket_stats_ex_t stats;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_stats_ex (sender, &stats));
    TEST_ASSERT_TRUE (stats.drops_hwm >= 1);

    test_context_socket_close_zero_linger (receiver);
    test_context_socket_close_zero_linger (sender);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_stats_ex_send_recv);
    RUN_TEST (test_stats_ex_drop_no_peers);
    RUN_TEST (test_stats_ex_drop_hwm);
    return UNITY_END ();
}
