/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

SETUP_TEARDOWN_TESTCONTEXT

void test_more ()
{
    //  Create the infrastructure
    void *sb = test_context_socket (ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (sb, "inproc://a"));

    void *sc = test_context_socket (ZLINK_SOCKET_DEALER);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sc, "inproc://a"));

    //  Send 2-part message.
    send_string_expect_success (sc, "A", ZLINK_SNDMORE);
    send_string_expect_success (sc, "B", 0);

    //  Routing id comes first.
    zlink_msg_t msg;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&msg));
    TEST_ASSERT_SUCCESS_ERRNO (test_recv_single_msg (&msg, sb, 0));
    TEST_ASSERT_TRUE (test_msg_has_more (&msg));

    //  Then the first part of the message body.
    TEST_ASSERT_EQUAL_INT (
      1, TEST_ASSERT_SUCCESS_ERRNO (test_recv_single_msg (&msg, sb, 0)));
    TEST_ASSERT_TRUE (test_msg_has_more (&msg));

    //  And finally, the second part of the message body.
    TEST_ASSERT_EQUAL_INT (
      1, TEST_ASSERT_SUCCESS_ERRNO (test_recv_single_msg (&msg, sb, 0)));
    TEST_ASSERT_FALSE (test_msg_has_more (&msg));

    //  Deallocate the infrastructure.
    test_context_socket_close (sc);
    test_context_socket_close (sb);
}

void test_shared_refcounted ()
{
    // Test shared storage query (case 1, refcounted messages)
    zlink_msg_t msg_a;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_init_size (&msg_a, 1024)); // large enough to be a type_lmsg

    // Single-owner reference-counted storage reports refcount 1.
    TEST_ASSERT_EQUAL_INT (1, zlink_msg_refcnt (&msg_a));

    zlink_msg_t msg_b;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&msg_b));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_copy (&msg_b, &msg_a));

    // Both handles now share the same storage.
    TEST_ASSERT_EQUAL_INT (2, zlink_msg_refcnt (&msg_a));
    TEST_ASSERT_EQUAL_INT (2, zlink_msg_refcnt (&msg_b));

    // cleanup
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&msg_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&msg_b));
}

void test_shared_const ()
{
    zlink_msg_t msg_a;
    // Test shared storage query (case 2, constant data messages)
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_init_data (&msg_a, (void *) "TEST", 5, 0, 0));

    // Constant messages are not internally refcounted; they report 1.
    TEST_ASSERT_EQUAL_INT (1, zlink_msg_refcnt (&msg_a));

    // cleanup
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&msg_a));
}

void test_request_reply_defaults_and_setters ()
{
    zlink_msg_t msg;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&msg));

    uint8_t type = 0xff;
    uint64_t correlation_id = UINT64_MAX;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_get_request_info (&msg, &type, &correlation_id));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_MSG_TYPE_DATA, type);
    TEST_ASSERT_EQUAL_UINT64 (0, correlation_id);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_set_request (&msg, 42));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_get_request_info (&msg, &type, &correlation_id));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_MSG_TYPE_REQUEST, type);
    TEST_ASSERT_EQUAL_UINT64 (42, correlation_id);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_set_reply (&msg, 77));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_get_request_info (&msg, &type, &correlation_id));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_MSG_TYPE_REPLY, type);
    TEST_ASSERT_EQUAL_UINT64 (77, correlation_id);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&msg));
}

void test_request_reply_copy_and_move ()
{
    zlink_msg_t src;
    zlink_msg_t copy;
    zlink_msg_t moved;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&src, 5));
    memcpy (zlink_msg_data (&src), "hello", 5);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_set_request (&src, 1234));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&copy));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_copy (&copy, &src));

    uint8_t type = 0;
    uint64_t correlation_id = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_get_request_info (&copy, &type, &correlation_id));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_MSG_TYPE_REQUEST, type);
    TEST_ASSERT_EQUAL_UINT64 (1234, correlation_id);
    TEST_ASSERT_EQUAL_UINT64 (5, zlink_msg_size (&copy));
    TEST_ASSERT_EQUAL_MEMORY ("hello", zlink_msg_data (&copy), 5);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&moved));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_move (&moved, &src));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_get_request_info (&moved, &type, &correlation_id));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_MSG_TYPE_REQUEST, type);
    TEST_ASSERT_EQUAL_UINT64 (1234, correlation_id);
    TEST_ASSERT_EQUAL_UINT64 (5, zlink_msg_size (&moved));
    TEST_ASSERT_EQUAL_MEMORY ("hello", zlink_msg_data (&moved), 5);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_get_request_info (&src, &type, &correlation_id));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_MSG_TYPE_DATA, type);
    TEST_ASSERT_EQUAL_UINT64 (0, correlation_id);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&copy));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&moved));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&src));
}

void test_request_reply_info_rejects_invalid_msg ()
{
    uint8_t type = 0;
    uint64_t correlation_id = 0;
    TEST_ASSERT_FAILURE_ERRNO (
      EINVAL, zlink_msg_set_request (NULL, 1));
    TEST_ASSERT_FAILURE_ERRNO (
      EINVAL, zlink_msg_set_reply (NULL, 1));
    TEST_ASSERT_FAILURE_ERRNO (
      EINVAL, zlink_msg_get_request_info (NULL, &type, &correlation_id));
}

void test_request_reply_info_allows_null_outputs ()
{
    zlink_msg_t msg;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init (&msg));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_set_reply (&msg, 99));

    uint8_t type = 0;
    uint64_t correlation_id = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_get_request_info (&msg, &type, NULL));
    TEST_ASSERT_EQUAL_UINT8 (ZLINK_MSG_TYPE_REPLY, type);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_get_request_info (&msg, NULL, &correlation_id));
    TEST_ASSERT_EQUAL_UINT64 (99, correlation_id);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&msg));
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_more);
    RUN_TEST (test_shared_refcounted);
    RUN_TEST (test_shared_const);
    RUN_TEST (test_request_reply_defaults_and_setters);
    RUN_TEST (test_request_reply_copy_and_move);
    RUN_TEST (test_request_reply_info_rejects_invalid_msg);
    RUN_TEST (test_request_reply_info_allows_null_outputs);
    return UNITY_END ();
}
