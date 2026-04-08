/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"

#include <cstring>
#include <unity.h>

void setUp ()
{
}

void tearDown ()
{
}

void test_request_reply_defaults ()
{
    zlink_msg_t msg;
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_init (&msg));

    uint8_t type = 0xff;
    uint64_t correlation_id = UINT64_MAX;
    TEST_ASSERT_EQUAL_INT (
      0, zlink_msg_get_request_info (&msg, &type, &correlation_id));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_MSG_TYPE_DATA, type);
    TEST_ASSERT_EQUAL_UINT64 (0, correlation_id);

    TEST_ASSERT_EQUAL_INT (0, zlink_msg_close (&msg));
}

void test_request_reply_setters_round_trip ()
{
    zlink_msg_t msg;
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_init_size (&msg, 4));
    std::memcpy (zlink_msg_data (&msg), "ping", 4);

    TEST_ASSERT_EQUAL_INT (0, zlink_msg_set_request (&msg, 42));
    uint8_t type = 0;
    uint64_t correlation_id = 0;
    TEST_ASSERT_EQUAL_INT (
      0, zlink_msg_get_request_info (&msg, &type, &correlation_id));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_MSG_TYPE_REQUEST, type);
    TEST_ASSERT_EQUAL_UINT64 (42, correlation_id);
    TEST_ASSERT_EQUAL_UINT64 (4, zlink_msg_size (&msg));
    TEST_ASSERT_EQUAL_MEMORY ("ping", zlink_msg_data (&msg), 4);

    TEST_ASSERT_EQUAL_INT (0, zlink_msg_set_reply (&msg, 77));
    TEST_ASSERT_EQUAL_INT (
      0, zlink_msg_get_request_info (&msg, &type, &correlation_id));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_MSG_TYPE_REPLY, type);
    TEST_ASSERT_EQUAL_UINT64 (77, correlation_id);

    TEST_ASSERT_EQUAL_INT (0, zlink_msg_close (&msg));
}

void test_request_reply_copy_and_move_preserve_metadata ()
{
    zlink_msg_t src;
    zlink_msg_t copy;
    zlink_msg_t moved;
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_init_size (&src, 5));
    std::memcpy (zlink_msg_data (&src), "hello", 5);
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_set_request (&src, 1234));
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_init (&copy));
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_init (&moved));

    TEST_ASSERT_EQUAL_INT (0, zlink_msg_copy (&copy, &src));
    uint8_t type = 0;
    uint64_t correlation_id = 0;
    TEST_ASSERT_EQUAL_INT (
      0, zlink_msg_get_request_info (&copy, &type, &correlation_id));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_MSG_TYPE_REQUEST, type);
    TEST_ASSERT_EQUAL_UINT64 (1234, correlation_id);
    TEST_ASSERT_EQUAL_MEMORY ("hello", zlink_msg_data (&copy), 5);

    TEST_ASSERT_EQUAL_INT (0, zlink_msg_move (&moved, &src));
    TEST_ASSERT_EQUAL_INT (
      0, zlink_msg_get_request_info (&moved, &type, &correlation_id));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_MSG_TYPE_REQUEST, type);
    TEST_ASSERT_EQUAL_UINT64 (1234, correlation_id);
    TEST_ASSERT_EQUAL_MEMORY ("hello", zlink_msg_data (&moved), 5);

    TEST_ASSERT_EQUAL_INT (
      0, zlink_msg_get_request_info (&src, &type, &correlation_id));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_MSG_TYPE_DATA, type);
    TEST_ASSERT_EQUAL_UINT64 (0, correlation_id);

    TEST_ASSERT_EQUAL_INT (0, zlink_msg_close (&copy));
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_close (&moved));
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_close (&src));
}

void test_request_reply_info_rejects_invalid_messages ()
{
    uint8_t type = 0;
    uint64_t correlation_id = 0;

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_msg_set_request (NULL, 1));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_msg_set_reply (NULL, 1));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_msg_get_request_info (NULL, &type, &correlation_id));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
}

void test_request_reply_info_allows_null_outputs ()
{
    zlink_msg_t msg;
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_init (&msg));
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_set_reply (&msg, 99));

    uint8_t type = 0;
    uint64_t correlation_id = 0;
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_get_request_info (&msg, &type, NULL));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_MSG_TYPE_REPLY, type);
    TEST_ASSERT_EQUAL_INT (
      0, zlink_msg_get_request_info (&msg, NULL, &correlation_id));
    TEST_ASSERT_EQUAL_UINT64 (99, correlation_id);

    TEST_ASSERT_EQUAL_INT (0, zlink_msg_close (&msg));
}

int main (void)
{
    UNITY_BEGIN ();

    setup_test_environment ();

    RUN_TEST (test_request_reply_defaults);
    RUN_TEST (test_request_reply_setters_round_trip);
    RUN_TEST (test_request_reply_copy_and_move_preserve_metadata);
    RUN_TEST (test_request_reply_info_rejects_invalid_messages);
    RUN_TEST (test_request_reply_info_allows_null_outputs);

    return UNITY_END ();
}
