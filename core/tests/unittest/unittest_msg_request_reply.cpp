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

void test_message_metadata_round_trip_and_copy_move ()
{
    zlink_msg_t src;
    zlink_msg_t copy;
    zlink_msg_t moved;
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_init_size (&src, 5));
    std::memcpy (zlink_msg_data (&src), "hello", 5);
    TEST_ASSERT_EQUAL_INT (
      0, zlink_msg_set_metadata (&src, 0x0100, "trace", 5));
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_set_metadata (&src, 0x0101, NULL, 9));
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_init (&copy));
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_init (&moved));

    size_t size = 0;
    const void *trace = zlink_msg_get_metadata (&src, 0x0100, &size);
    TEST_ASSERT_NOT_NULL (trace);
    TEST_ASSERT_EQUAL_UINT64 (5, size);
    TEST_ASSERT_EQUAL_MEMORY ("trace", trace, 5);
    const void *marker = zlink_msg_get_metadata (&src, 0x0101, &size);
    TEST_ASSERT_NOT_NULL (marker);
    TEST_ASSERT_EQUAL_UINT64 (0, size);

    TEST_ASSERT_EQUAL_INT (0, zlink_msg_copy (&copy, &src));
    trace = zlink_msg_get_metadata (&copy, 0x0100, &size);
    TEST_ASSERT_NOT_NULL (trace);
    TEST_ASSERT_EQUAL_UINT64 (5, size);
    TEST_ASSERT_EQUAL_MEMORY ("trace", trace, 5);

    TEST_ASSERT_EQUAL_INT (0, zlink_msg_move (&moved, &src));
    trace = zlink_msg_get_metadata (&moved, 0x0100, &size);
    TEST_ASSERT_NOT_NULL (trace);
    TEST_ASSERT_EQUAL_UINT64 (5, size);
    TEST_ASSERT_EQUAL_MEMORY ("trace", trace, 5);
    TEST_ASSERT_NULL (zlink_msg_get_metadata (&src, 0x0100, &size));

    TEST_ASSERT_EQUAL_INT (0, zlink_msg_close (&copy));
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_close (&moved));
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_close (&src));
}

void test_message_metadata_rejects_invalid_inputs ()
{
    zlink_msg_t msg;
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_init (&msg));

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_msg_set_metadata (NULL, 0x0100, "x", 1));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_msg_set_metadata (&msg, 0x00ff, "x", 1));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1,
      zlink_msg_set_metadata (&msg, 0x0100, "x",
                              ZLINK_MSG_METADATA_VALUE_MAX + 1u));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    TEST_ASSERT_NULL (zlink_msg_get_metadata (NULL, 0x0100, NULL));

    TEST_ASSERT_EQUAL_INT (0, zlink_msg_close (&msg));
}

void test_message_metadata_and_request_reply_survive_socket_roundtrip ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *server = zlink_socket (ctx, ZLINK_SOCKET_PAIR);
    TEST_ASSERT_NOT_NULL (server);
    void *client = zlink_socket (ctx, ZLINK_SOCKET_PAIR);
    TEST_ASSERT_NOT_NULL (client);

    TEST_ASSERT_EQUAL_INT (0, zlink_bind (server, ENDPOINT_0));
    TEST_ASSERT_EQUAL_INT (0, zlink_connect (client, ENDPOINT_0));
    msleep (SETTLE_TIME);

    zlink_msg_t msg;
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_init_size (&msg, 4));
    std::memcpy (zlink_msg_data (&msg), "ping", 4);
    TEST_ASSERT_EQUAL_INT (0, zlink_msg_set_request (&msg, 42));
    TEST_ASSERT_EQUAL_INT (
      0, zlink_msg_set_metadata (&msg, 0x0100, "trace-id", 8));

    TEST_ASSERT_EQUAL_INT (0, zlink_send (client, &msg, 1, 0));

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_EQUAL_INT (0, zlink_recv (server, NULL, &parts, &part_count, 0));
    TEST_ASSERT_EQUAL_UINT64 (1, part_count);
    TEST_ASSERT_EQUAL_UINT64 (4, zlink_msg_size (&parts[0]));
    TEST_ASSERT_EQUAL_MEMORY ("ping", zlink_msg_data (&parts[0]), 4);

    uint8_t type = 0;
    uint64_t correlation_id = 0;
    TEST_ASSERT_EQUAL_INT (
      0, zlink_msg_get_request_info (&parts[0], &type, &correlation_id));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_MSG_TYPE_REQUEST, type);
    TEST_ASSERT_EQUAL_UINT64 (42, correlation_id);

    size_t metadata_size = 0;
    const void *trace = zlink_msg_get_metadata (&parts[0], 0x0100, &metadata_size);
    TEST_ASSERT_NOT_NULL (trace);
    TEST_ASSERT_EQUAL_UINT64 (8, metadata_size);
    TEST_ASSERT_EQUAL_MEMORY ("trace-id", trace, 8);

    zlink_multipart_close (parts, part_count);
    close_zero_linger (client);
    close_zero_linger (server);
    TEST_ASSERT_EQUAL_INT (0, zlink_ctx_term (ctx));
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
    RUN_TEST (test_message_metadata_round_trip_and_copy_move);
    RUN_TEST (test_message_metadata_rejects_invalid_inputs);
    RUN_TEST (test_message_metadata_and_request_reply_survive_socket_roundtrip);

    return UNITY_END ();
}
