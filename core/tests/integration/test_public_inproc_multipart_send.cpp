/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <string.h>

SETUP_TEARDOWN_TESTCONTEXT

void test_public_inproc_pair_send_single_part ()
{
    void *left = test_context_socket (ZLINK_SOCKET_PAIR);
    void *right = test_context_socket (ZLINK_SOCKET_PAIR);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (left, "inproc://public_inproc_pair_send_single_part"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (right, "inproc://public_inproc_pair_send_single_part"));

    int sndtimeo = 0;
    size_t sndtimeo_size = sizeof (sndtimeo);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_option (right, ZLINK_OPT_SNDTIMEO, &sndtimeo, &sndtimeo_size));
    TEST_ASSERT_EQUAL_INT (static_cast<int> (sizeof (sndtimeo)),
                           static_cast<int> (sndtimeo_size));

    zlink_msg_t part;
    const char payload[] = "ping";
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, sizeof (payload) - 1));
    memcpy (zlink_msg_data (&part), payload, sizeof (payload) - 1);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (right, &part, 1, 0));

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_recv (left, NULL, &parts, &part_count, 0));
    TEST_ASSERT_EQUAL_UINT64 (1, part_count);
    TEST_ASSERT_EQUAL_UINT64 (sizeof (payload) - 1, zlink_msg_size (&parts[0]));
    TEST_ASSERT_EQUAL_MEMORY (payload, zlink_msg_data (&parts[0]),
                              sizeof (payload) - 1);
    zlink_multipart_close (parts, part_count);
    free (parts);
}

void test_public_inproc_pair_send_multipart_blocking ()
{
    void *left = test_context_socket (ZLINK_SOCKET_PAIR);
    void *right = test_context_socket (ZLINK_SOCKET_PAIR);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (left, "inproc://public_inproc_pair_send_multipart_blocking"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (right,
                     "inproc://public_inproc_pair_send_multipart_blocking"));

    zlink_msg_t parts[2];
    const char header[] = "head";
    const char body[] = "body";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_init_size (&parts[0], sizeof (header) - 1));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_init_size (&parts[1], sizeof (body) - 1));
    memcpy (zlink_msg_data (&parts[0]), header, sizeof (header) - 1);
    memcpy (zlink_msg_data (&parts[1]), body, sizeof (body) - 1);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (right, parts, 2, 0));

    zlink_msg_t *received = NULL;
    size_t part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_recv (left, NULL, &received, &part_count, 0));
    TEST_ASSERT_EQUAL_UINT64 (2, part_count);
    TEST_ASSERT_EQUAL_UINT64 (sizeof (header) - 1,
                              zlink_msg_size (&received[0]));
    TEST_ASSERT_EQUAL_MEMORY (header, zlink_msg_data (&received[0]),
                              sizeof (header) - 1);
    TEST_ASSERT_EQUAL_UINT64 (sizeof (body) - 1,
                              zlink_msg_size (&received[1]));
    TEST_ASSERT_EQUAL_MEMORY (body, zlink_msg_data (&received[1]),
                              sizeof (body) - 1);
    zlink_multipart_close (received, part_count);
    free (received);
}

void test_public_inproc_router_send_rid_blocking ()
{
    void *router = test_context_socket (ZLINK_SOCKET_ROUTER);
    void *dealer = test_context_socket (ZLINK_SOCKET_DEALER);

    const char routing_id[] = "D1";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (dealer, routing_id, sizeof (routing_id) - 1));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_bind (router, "inproc://public_inproc_router_send_rid_blocking"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (dealer, "inproc://public_inproc_router_send_rid_blocking"));

    zlink_msg_t outbound;
    const char payload[] = "ping";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_init_size (&outbound, sizeof (payload) - 1));
    memcpy (zlink_msg_data (&outbound), payload, sizeof (payload) - 1);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_send (dealer, &outbound, 1, 0));

    zlink_routing_id_t source_rid;
    zlink_msg_t *received = NULL;
    size_t part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_recv (router, &source_rid, &received, &part_count, 0));
    TEST_ASSERT_EQUAL_UINT64 (1, part_count);
    TEST_ASSERT_EQUAL_UINT64 (sizeof (routing_id) - 1, source_rid.size);
    TEST_ASSERT_EQUAL_MEMORY (routing_id, source_rid.data,
                              sizeof (routing_id) - 1);
    zlink_multipart_close (received, part_count);
    free (received);

    zlink_msg_t reply;
    const char reply_payload[] = "pong";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_msg_init_size (&reply, sizeof (reply_payload) - 1));
    memcpy (zlink_msg_data (&reply), reply_payload, sizeof (reply_payload) - 1);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_send_rid (router, &source_rid, &reply, 1, 0));

    zlink_msg_t *reply_parts = NULL;
    size_t reply_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_recv (dealer, NULL, &reply_parts, &reply_count, 0));
    TEST_ASSERT_EQUAL_UINT64 (1, reply_count);
    TEST_ASSERT_EQUAL_UINT64 (sizeof (reply_payload) - 1,
                              zlink_msg_size (&reply_parts[0]));
    TEST_ASSERT_EQUAL_MEMORY (reply_payload, zlink_msg_data (&reply_parts[0]),
                              sizeof (reply_payload) - 1);
    zlink_multipart_close (reply_parts, reply_count);
    free (reply_parts);
}

int main (void)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_public_inproc_pair_send_single_part);
    RUN_TEST (test_public_inproc_pair_send_multipart_blocking);
    RUN_TEST (test_public_inproc_router_send_rid_blocking);
    return UNITY_END ();
}
