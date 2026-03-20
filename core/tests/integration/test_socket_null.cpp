/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"

#include <unity.h>

void setUp ()
{
}

void tearDown ()
{
}

namespace
{
void discard_monitor_event (const zlink_monitor_event_t *, void *)
{
}
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
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);
}

void test_zlink_setsockopt_null_socket ()
{
    int hwm = 100;
    size_t hwm_size = sizeof hwm;
    int rc = zlink_set_option (NULL, ZLINK_OPT_SNDHWM, &hwm, hwm_size);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);
}

void test_zlink_getsockopt_null_socket ()
{
    int hwm;
    size_t hwm_size = sizeof hwm;
    int rc = zlink_get_option (NULL, ZLINK_OPT_SNDHWM, &hwm, &hwm_size);
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);
}

void test_zlink_socket_monitor_open_null_socket ()
{
    zlink_socket_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = ZLINK_EVENT_ALL;
    void *monitor = zlink_socket_monitor_open (NULL, &opts);
    TEST_ASSERT_NULL (monitor);
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);
}



void test_zlink_bind_null_socket ()
{
    int rc = zlink_bind (NULL, "inproc://socket");
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);
}

void test_zlink_connect_null_socket ()
{
    int rc = zlink_connect (NULL, "inproc://socket");
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);
}

void test_zlink_unbind_null_socket ()
{
    int rc = zlink_unbind (NULL, "inproc://socket");
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);
}

void test_zlink_disconnect_null_socket ()
{
    int rc = zlink_disconnect (NULL, "inproc://socket");
    TEST_ASSERT_EQUAL_INT (-1, rc);
    TEST_ASSERT_EQUAL_INT (EFAULT, errno);
}

int main (void)
{
    UNITY_BEGIN ();
    RUN_TEST (test_zlink_socket_null_context);
    RUN_TEST (test_zlink_close_null_socket);
    RUN_TEST (test_zlink_setsockopt_null_socket);
    RUN_TEST (test_zlink_getsockopt_null_socket);
    RUN_TEST (test_zlink_socket_monitor_open_null_socket);
    RUN_TEST (test_zlink_bind_null_socket);
    RUN_TEST (test_zlink_connect_null_socket);
    RUN_TEST (test_zlink_unbind_null_socket);
    RUN_TEST (test_zlink_disconnect_null_socket);


    return UNITY_END ();
}
