/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <errno.h>

SETUP_TEARDOWN_TESTCONTEXT

void test_stream_invalid_sockopt_values ()
{
    void *stream = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (stream);

    int value = 2;
    TEST_ASSERT_FAILURE_ERRNO (
      EINVAL, zlink_setsockopt (stream, ZLINK_STREAM_SINGLE_FRAME_RECV, &value,
                                sizeof (value)));

    value = 7;
    TEST_ASSERT_FAILURE_ERRNO (
      EINVAL,
      zlink_setsockopt (stream, ZLINK_TCP_NODELAY, &value, sizeof (value)));

    const short short_value = 1;
    TEST_ASSERT_FAILURE_ERRNO (
      EINVAL, zlink_setsockopt (stream, ZLINK_STREAM_SINGLE_FRAME_RECV,
                                &short_value, sizeof (short_value)));

    int out = 0;
    size_t out_size = 1;
    TEST_ASSERT_FAILURE_ERRNO (
      EINVAL, zlink_getsockopt (stream, ZLINK_TCP_NODELAY, &out, &out_size));

    test_context_socket_close_zero_linger (stream);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_stream_invalid_sockopt_values);
    return UNITY_END ();
}
