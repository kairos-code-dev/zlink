/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"

#include <unity.h>

void setUp ()
{
}

void tearDown ()
{
}

//  tests all socket-related functions with a NULL socket argument
void test_zlink_socket_null_context ()
{
    TEST_ASSERT_NULL (zlink_socket (NULL, ZLINK_PAIR));
    TEST_ASSERT_EQUAL_INT (EFAULT, errno); // TODO use EINVAL instead?
}

void test_zlink_close_null_socket ()
{
    int rc = zlink_close (NULL);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (ENOTSOCK, errno); // TODO use EINVAL instead?
}

void test_zlink_setsockopt_null_socket ()
{
    int hwm = 100;
    size_t hwm_size = sizeof hwm;
    int rc = zlink_setsockopt (NULL, ZLINK_SNDHWM, &hwm, hwm_size);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (ENOTSOCK, errno); // TODO use EINVAL instead?
}

void test_zlink_getsockopt_null_socket ()
{
    int hwm;
    size_t hwm_size = sizeof hwm;
    int rc = zlink_getsockopt (NULL, ZLINK_SNDHWM, &hwm, &hwm_size);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (ENOTSOCK, errno); // TODO use EINVAL instead?
}

void test_zlink_socket_monitor_null_socket ()
{
    int rc = zlink_socket_monitor (NULL, "inproc://monitor", ZLINK_EVENT_ALL);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (ENOTSOCK, errno); // TODO use EINVAL instead?
}



void test_zlink_bind_null_socket ()
{
    int rc = zlink_bind (NULL, "inproc://socket");
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (ENOTSOCK, errno); // TODO use EINVAL instead?
}

void test_zlink_connect_null_socket ()
{
    int rc = zlink_connect (NULL, "inproc://socket");
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (ENOTSOCK, errno); // TODO use EINVAL instead?
}

void test_zlink_unbind_null_socket ()
{
    int rc = zlink_unbind (NULL, "inproc://socket");
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (ENOTSOCK, errno); // TODO use EINVAL instead?
}

void test_zlink_disconnect_null_socket ()
{
    int rc = zlink_disconnect (NULL, "inproc://socket");
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (ENOTSOCK, errno); // TODO use EINVAL instead?
}

int main (void)
{
    UNITY_BEGIN ();
    RUN_TEST (test_zlink_socket_null_context);
    RUN_TEST (test_zlink_close_null_socket);
    RUN_TEST (test_zlink_setsockopt_null_socket);
    RUN_TEST (test_zlink_getsockopt_null_socket);
    RUN_TEST (test_zlink_socket_monitor_null_socket);
    RUN_TEST (test_zlink_bind_null_socket);
    RUN_TEST (test_zlink_connect_null_socket);
    RUN_TEST (test_zlink_unbind_null_socket);
    RUN_TEST (test_zlink_disconnect_null_socket);


    return UNITY_END ();
}
