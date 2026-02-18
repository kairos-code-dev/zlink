/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <errno.h>

SETUP_TEARDOWN_TESTCONTEXT

void test_stream_option_change_after_bind_rejected ()
{
    void *stream = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (stream);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (stream, ZLINK_LINGER, &zero, sizeof (zero)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (stream, endpoint, sizeof (endpoint));

    int value = 1;
    TEST_ASSERT_FAILURE_ERRNO (
      EINVAL, zlink_setsockopt (stream, ZLINK_STREAM_SINGLE_FRAME_RECV, &value,
                                sizeof (value)));

    value = 0;
    TEST_ASSERT_FAILURE_ERRNO (
      EINVAL, zlink_setsockopt (stream, ZLINK_TCP_NODELAY, &value, sizeof (value)));

    test_context_socket_close_zero_linger (stream);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_stream_option_change_after_bind_rejected);
    return UNITY_END ();
}
