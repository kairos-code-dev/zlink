/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <errno.h>

SETUP_TEARDOWN_TESTCONTEXT

void test_tcp_nodelay_sockopt_set_get ()
{
    void *stream = test_context_socket (ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (stream);

    int value = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (stream, ZLINK_TCP_NODELAY, &value, sizeof (value)));

    int out = -1;
    size_t out_size = sizeof (out);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (stream, ZLINK_TCP_NODELAY, &out, &out_size));
    TEST_ASSERT_EQUAL_INT (sizeof (out), static_cast<int> (out_size));
    TEST_ASSERT_EQUAL_INT (0, out);

    value = -1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (stream, ZLINK_TCP_NODELAY, &value, sizeof (value)));
    out_size = sizeof (out);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_getsockopt (stream, ZLINK_TCP_NODELAY, &out, &out_size));
    TEST_ASSERT_EQUAL_INT (-1, out);

    value = 2;
    TEST_ASSERT_FAILURE_ERRNO (
      EINVAL, zlink_setsockopt (stream, ZLINK_TCP_NODELAY, &value, sizeof (value)));

    test_context_socket_close_zero_linger (stream);
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_tcp_nodelay_sockopt_set_get);
    return UNITY_END ();
}
