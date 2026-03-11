/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <algorithm>
#include <atomic>
#include <string.h>

namespace
{
struct raw_handler_probe_t
{
    raw_handler_probe_t () :
        calls (0),
        part_count (0),
        close_failures (0),
        rid_size (0)
    {
        memset (rid, 0, sizeof (rid));
        memset (parts, 0, sizeof (parts));
    }

    std::atomic<int> calls;
    std::atomic<int> part_count;
    std::atomic<int> close_failures;
    unsigned char rid[255];
    size_t rid_size;
    char parts[4][64];
};

raw_handler_probe_t *g_probe = NULL;
raw_handler_probe_t *g_replacement_probe = NULL;

bool wait_for_calls (std::atomic<int> *calls_, int expected_, int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;

    for (int i = 0; i < attempts; ++i) {
        if (calls_->load () >= expected_)
            return true;
        msleep (step_ms);
    }

    return calls_->load () >= expected_;
}

void close_parts (raw_handler_probe_t *probe_,
                  zlink_msg_t *parts_,
                  size_t part_count_)
{
    for (size_t i = 0; i < part_count_; ++i) {
        const int rc = zlink_msg_close (&parts_[i]);
        if (rc != 0 && probe_)
            probe_->close_failures.fetch_add (1);
    }
}

void capture_raw_message_into (raw_handler_probe_t *probe_,
                               const zlink_routing_id_t *source_rid_,
                               zlink_msg_t *parts_,
                               size_t part_count_)
{
    if (!probe_) {
        close_parts (probe_, parts_, part_count_);
        return;
    }

    probe_->part_count.store (static_cast<int> (part_count_));
    probe_->rid_size = source_rid_ ? source_rid_->size : 0;
    if (source_rid_ && source_rid_->size > 0) {
        memcpy (probe_->rid, source_rid_->data, source_rid_->size);
    }

    const size_t copy_count = std::min (part_count_, size_t (4));
    for (size_t i = 0; i < copy_count; ++i) {
        const size_t size = zlink_msg_size (&parts_[i]);
        const size_t copy_size =
          std::min (size, sizeof (probe_->parts[i]) - 1);
        if (copy_size > 0)
            memcpy (probe_->parts[i], zlink_msg_data (&parts_[i]), copy_size);
        probe_->parts[i][copy_size] = '\0';
    }

    close_parts (probe_, parts_, part_count_);
    probe_->calls.fetch_add (1);
}

void capture_raw_message (const zlink_routing_id_t *source_rid_,
                          zlink_msg_t *parts_,
                          size_t part_count_)
{
    capture_raw_message_into (g_probe, source_rid_, parts_, part_count_);
}

void capture_replacement_raw_message (const zlink_routing_id_t *source_rid_,
                                      zlink_msg_t *parts_,
                                      size_t part_count_)
{
    capture_raw_message_into (g_replacement_probe, source_rid_, parts_,
                              part_count_);
}

void discard_raw_message (const zlink_routing_id_t *,
                          zlink_msg_t *parts_,
                          size_t part_count_)
{
    close_parts (NULL, parts_, part_count_);
}

void test_recv_capable_sockets_require_handlers_on_create ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    TEST_ASSERT_NULL (zlink_socket (ctx, ZLINK_PAIR));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_NULL (zlink_socket (ctx, ZLINK_SUB));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_NULL (zlink_socket (ctx, ZLINK_DEALER));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_NULL (zlink_socket (ctx, ZLINK_ROUTER));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    TEST_ASSERT_NULL (zlink_socket_with_handler (ctx, ZLINK_PAIR, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_NULL (
      zlink_socket_with_handler (ctx, ZLINK_PUB, &capture_raw_message));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_NULL (
      zlink_socket_with_handler (ctx, ZLINK_XPUB, &capture_raw_message));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_NULL (
      zlink_socket_with_handler (ctx, ZLINK_STREAM, &capture_raw_message));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_pair_socket_with_handler_dispatches_multipart ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    raw_handler_probe_t probe;
    g_probe = &probe;

    void *server = zlink_socket_with_handler (ctx, ZLINK_PAIR,
                                              &capture_raw_message);
    TEST_ASSERT_NOT_NULL (server);

    void *client = zlink_socket_with_handler (ctx, ZLINK_PAIR,
                                              &discard_raw_message);
    TEST_ASSERT_NOT_NULL (client);

    const unsigned char client_rid[] = {'p', 'a', 'i', 'r', 'A'};
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      client, ZLINK_ROUTING_ID, client_rid, sizeof (client_rid)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server, ENDPOINT_0));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, ENDPOINT_0));
    msleep (SETTLE_TIME);

    s_send_seq (client, "alpha", "beta", SEQ_END);

    TEST_ASSERT_TRUE (wait_for_calls (&probe.calls, 1, 2000));
    TEST_ASSERT_EQUAL_INT (2, probe.part_count.load ());
    TEST_ASSERT_EQUAL_UINT (sizeof (client_rid), probe.rid_size);
    TEST_ASSERT_EQUAL_MEMORY (client_rid, probe.rid, sizeof (client_rid));
    TEST_ASSERT_EQUAL_STRING ("alpha", probe.parts[0]);
    TEST_ASSERT_EQUAL_STRING ("beta", probe.parts[1]);
    TEST_ASSERT_EQUAL_INT (0, probe.close_failures.load ());

    close_zero_linger (client);
    close_zero_linger (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    g_probe = NULL;
}

void test_router_socket_with_handler_strips_routing_frame ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    raw_handler_probe_t probe;
    g_probe = &probe;

    void *server = zlink_socket_with_handler (ctx, ZLINK_ROUTER,
                                              &capture_raw_message);
    TEST_ASSERT_NOT_NULL (server);

    void *client = zlink_socket_with_handler (ctx, ZLINK_DEALER,
                                              &discard_raw_message);
    TEST_ASSERT_NOT_NULL (client);

    const unsigned char client_rid[] = {'d', 'e', 'a', 'l', 'e', 'r'};
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      client, ZLINK_ROUTING_ID, client_rid, sizeof (client_rid)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server, ENDPOINT_1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, ENDPOINT_1));
    msleep (SETTLE_TIME);

    s_send_seq (client, "first", "second", SEQ_END);

    TEST_ASSERT_TRUE (wait_for_calls (&probe.calls, 1, 2000));
    TEST_ASSERT_EQUAL_INT (2, probe.part_count.load ());
    TEST_ASSERT_EQUAL_UINT (sizeof (client_rid), probe.rid_size);
    TEST_ASSERT_EQUAL_MEMORY (client_rid, probe.rid, sizeof (client_rid));
    TEST_ASSERT_EQUAL_STRING ("first", probe.parts[0]);
    TEST_ASSERT_EQUAL_STRING ("second", probe.parts[1]);
    TEST_ASSERT_EQUAL_INT (0, probe.close_failures.load ());

    close_zero_linger (client);
    close_zero_linger (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    g_probe = NULL;
}

void test_socket_set_msg_handler_replaces_dispatch ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    raw_handler_probe_t primary_probe;
    raw_handler_probe_t replacement_probe;
    g_probe = &primary_probe;
    g_replacement_probe = &replacement_probe;

    void *server = zlink_socket_with_handler (ctx, ZLINK_PAIR,
                                              &capture_raw_message);
    TEST_ASSERT_NOT_NULL (server);

    void *client = zlink_socket_with_handler (ctx, ZLINK_PAIR,
                                              &discard_raw_message);
    TEST_ASSERT_NOT_NULL (client);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server, ENDPOINT_3));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, ENDPOINT_3));
    msleep (SETTLE_TIME);

    s_send_seq (client, "first", SEQ_END);
    TEST_ASSERT_TRUE (wait_for_calls (&primary_probe.calls, 1, 2000));
    TEST_ASSERT_EQUAL_STRING ("first", primary_probe.parts[0]);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_socket_set_msg_handler (server, &capture_replacement_raw_message));
    TEST_ASSERT_EQUAL_INT (-1, zlink_socket_set_msg_handler (server, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    s_send_seq (client, "second", SEQ_END);
    TEST_ASSERT_TRUE (wait_for_calls (&replacement_probe.calls, 1, 2000));
    TEST_ASSERT_EQUAL_STRING ("second", replacement_probe.parts[0]);

    close_zero_linger (client);
    close_zero_linger (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    g_probe = NULL;
    g_replacement_probe = NULL;
}

void test_sub_socket_with_handler_applies_filter_before_dispatch ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    raw_handler_probe_t probe;
    g_probe = &probe;

    void *sub = zlink_socket_with_handler (ctx, ZLINK_SUB,
                                           &capture_raw_message);
    TEST_ASSERT_NOT_NULL (sub);

    void *pub = zlink_socket (ctx, ZLINK_PUB);
    TEST_ASSERT_NOT_NULL (pub);

    const unsigned char pub_rid[] = {'p', 'u', 'b', 'A'};
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      pub, ZLINK_ROUTING_ID, pub_rid, sizeof (pub_rid)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (sub, ZLINK_SUBSCRIBE, "topic", 5));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (pub, ENDPOINT_2));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sub, ENDPOINT_2));
    msleep (SETTLE_TIME);

    s_send_seq (pub, "other", "skip", SEQ_END);
    msleep (100);
    TEST_ASSERT_EQUAL_INT (0, probe.calls.load ());

    s_send_seq (pub, "topic", "payload", SEQ_END);

    TEST_ASSERT_TRUE (wait_for_calls (&probe.calls, 1, 2000));
    TEST_ASSERT_EQUAL_INT (2, probe.part_count.load ());
    TEST_ASSERT_EQUAL_STRING ("topic", probe.parts[0]);
    TEST_ASSERT_EQUAL_STRING ("payload", probe.parts[1]);
    TEST_ASSERT_EQUAL_INT (0, probe.close_failures.load ());

    close_zero_linger (sub);
    close_zero_linger (pub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    g_probe = NULL;
}
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_recv_capable_sockets_require_handlers_on_create);
    RUN_TEST (test_pair_socket_with_handler_dispatches_multipart);
    RUN_TEST (test_router_socket_with_handler_strips_routing_frame);
    RUN_TEST (test_socket_set_msg_handler_replaces_dispatch);
    RUN_TEST (test_sub_socket_with_handler_applies_filter_before_dispatch);
    return UNITY_END ();
}
