/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <stdio.h>

SETUP_TEARDOWN_TESTCONTEXT

void test_inproc_pubsub_alternating_does_not_timeout ()
{
    void *pub = test_context_socket (ZLINK_PUB);
    void *sub = test_context_socket (ZLINK_SUB);

    const int linger = 0;
    const int timeout_ms = 1000;
    const int hwm = 10;

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (pub, ZLINK_LINGER, &linger, sizeof (linger)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (sub, ZLINK_LINGER, &linger, sizeof (linger)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (pub, ZLINK_SNDHWM, &hwm, sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (sub, ZLINK_RCVHWM, &hwm, sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (pub, ZLINK_SNDTIMEO, &timeout_ms, sizeof (timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (sub, ZLINK_RCVTIMEO, &timeout_ms, sizeof (timeout_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (sub, ZLINK_SUBSCRIBE, "", 0));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (pub, "inproc://pubsub-alternating"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (sub, "inproc://pubsub-alternating"));

    msleep (SETTLE_TIME);

    const char send_ch = 'a';
    char recv_ch = 0;
    bool ready = false;

    for (int i = 0; i < 2000; ++i) {
        TEST_ASSERT_EQUAL_INT (1, zlink_send (pub, &send_ch, 1, 0));

        const int n = zlink_recv (sub, &recv_ch, 1, ZLINK_DONTWAIT);
        if (n == 1) {
            TEST_ASSERT_EQUAL_UINT8 ((uint8_t) send_ch, (uint8_t) recv_ch);
            ready = true;
            break;
        }

        TEST_ASSERT_EQUAL_INT (-1, n);
        TEST_ASSERT_EQUAL_INT (EAGAIN, errno);
    }

    TEST_ASSERT_TRUE_MESSAGE (ready, "subscriber never became ready");

    for (int i = 0; i < 5000; ++i) {
        TEST_ASSERT_EQUAL_INT (1, zlink_send (pub, &send_ch, 1, 0));

        const int n = zlink_recv (sub, &recv_ch, 1, 0);
        if (n != 1) {
            char msg[128];
            snprintf (msg, sizeof (msg), "recv failed at iter=%d errno=%d", i,
                      errno);
            TEST_FAIL_MESSAGE (msg);
        }

        TEST_ASSERT_EQUAL_UINT8 ((uint8_t) send_ch, (uint8_t) recv_ch);
    }

    test_context_socket_close (sub);
    test_context_socket_close (pub);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_inproc_pubsub_alternating_does_not_timeout);
    return UNITY_END ();
}
