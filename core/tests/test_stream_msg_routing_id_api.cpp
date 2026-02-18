/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil_unity.hpp"

#include <errno.h>
#include <stdint.h>

void test_stream_msg_routing_id_api ()
{
    zlink_msg_t msg;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&msg));

    TEST_ASSERT_EQUAL_UINT32 (0, zlink_msg_get_routing_id (&msg));
    TEST_ASSERT_EQUAL_UINT32 (0, zlink_msg_routing_id (&msg));

    const uint32_t routing_id = 12345U;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_set_routing_id (&msg, routing_id));
    TEST_ASSERT_EQUAL_UINT32 (routing_id, zlink_msg_get_routing_id (&msg));
    TEST_ASSERT_EQUAL_UINT32 (routing_id, zlink_msg_routing_id (&msg));

    TEST_ASSERT_FAILURE_ERRNO (EINVAL, zlink_msg_set_routing_id (&msg, 0));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&msg));
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_stream_msg_routing_id_api);
    return UNITY_END ();
}
