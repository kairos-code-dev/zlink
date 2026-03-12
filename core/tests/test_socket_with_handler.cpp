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

struct spot_handler_probe_t
{
    spot_handler_probe_t () :
        calls (0),
        part_count (0),
        close_failures (0),
        rid_size (0),
        topic_len (0)
    {
        memset (rid, 0, sizeof (rid));
        memset (topic, 0, sizeof (topic));
        memset (parts, 0, sizeof (parts));
    }

    std::atomic<int> calls;
    std::atomic<int> part_count;
    std::atomic<int> close_failures;
    unsigned char rid[255];
    size_t rid_size;
    char topic[64];
    size_t topic_len;
    char parts[4][64];
};

struct xpub_handler_probe_t
{
    xpub_handler_probe_t () : calls (0), subscribed (0), topic_len (0)
    {
        memset (topic, 0, sizeof (topic));
    }

    std::atomic<int> calls;
    std::atomic<int> subscribed;
    char topic[64];
    size_t topic_len;
};

raw_handler_probe_t *g_probe = NULL;
raw_handler_probe_t *g_replacement_probe = NULL;
spot_handler_probe_t *g_spot_probe = NULL;
xpub_handler_probe_t *g_xpub_probe = NULL;

zlink_socket_handler_t make_msg_handler (zlink_socket_msg_handler_fn fn_)
{
    zlink_socket_handler_t handler;
    memset (&handler, 0, sizeof (handler));
    handler.kind = ZLINK_SOCKET_HANDLER_MSG;
    handler.fn.msg = fn_;
    return handler;
}

zlink_socket_handler_t make_spot_handler (zlink_spot_handler_fn fn_)
{
    zlink_socket_handler_t handler;
    memset (&handler, 0, sizeof (handler));
    handler.kind = ZLINK_SOCKET_HANDLER_SPOT;
    handler.fn.spot = fn_;
    return handler;
}

zlink_socket_handler_t make_xpub_handler (zlink_xpub_handler_fn fn_)
{
    zlink_socket_handler_t handler;
    memset (&handler, 0, sizeof (handler));
    handler.kind = ZLINK_SOCKET_HANDLER_XPUB;
    handler.fn.xpub = fn_;
    return handler;
}

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

void capture_spot_message (const zlink_routing_id_t *source_rid_,
                           const char *topic_,
                           size_t topic_len_,
                           zlink_msg_t *parts_,
                           size_t part_count_)
{
    if (!g_spot_probe) {
        close_parts (NULL, parts_, part_count_);
        return;
    }

    g_spot_probe->part_count.store (static_cast<int> (part_count_));
    g_spot_probe->rid_size = source_rid_ ? source_rid_->size : 0;
    if (source_rid_ && source_rid_->size > 0)
        memcpy (g_spot_probe->rid, source_rid_->data, source_rid_->size);

    const size_t copy_topic =
      std::min (topic_len_, sizeof (g_spot_probe->topic) - 1);
    if (copy_topic > 0)
        memcpy (g_spot_probe->topic, topic_, copy_topic);
    g_spot_probe->topic[copy_topic] = '\0';
    g_spot_probe->topic_len = topic_len_;

    const size_t copy_count = std::min (part_count_, size_t (4));
    for (size_t i = 0; i < copy_count; ++i) {
        const size_t size = zlink_msg_size (&parts_[i]);
        const size_t copy_size =
          std::min (size, sizeof (g_spot_probe->parts[i]) - 1);
        if (copy_size > 0)
            memcpy (g_spot_probe->parts[i], zlink_msg_data (&parts_[i]),
                    copy_size);
        g_spot_probe->parts[i][copy_size] = '\0';
    }

    close_parts (NULL, parts_, part_count_);
    g_spot_probe->calls.fetch_add (1);
}

void capture_xpub_event (int subscribed_,
                         const uint8_t *topic_,
                         size_t topic_len_)
{
    if (!g_xpub_probe)
        return;

    g_xpub_probe->subscribed.store (subscribed_);
    const size_t copy_size =
      std::min (topic_len_, sizeof (g_xpub_probe->topic) - 1);
    if (copy_size > 0)
        memcpy (g_xpub_probe->topic, topic_, copy_size);
    g_xpub_probe->topic[copy_size] = '\0';
    g_xpub_probe->topic_len = topic_len_;
    g_xpub_probe->calls.fetch_add (1);
}

void discard_raw_message (const zlink_routing_id_t *,
                          zlink_msg_t *parts_,
                          size_t part_count_)
{
    close_parts (NULL, parts_, part_count_);
}

void test_socket_handler_family_validation_on_create ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    const zlink_socket_handler_t msg_handler =
      make_msg_handler (&capture_raw_message);
    const zlink_socket_handler_t spot_handler =
      make_spot_handler (&capture_spot_message);
    const zlink_socket_handler_t xpub_handler =
      make_xpub_handler (&capture_xpub_event);

    void *pair = zlink_socket (ctx, ZLINK_PAIR, NULL);
    TEST_ASSERT_NOT_NULL (pair);
    close_zero_linger (pair);
    TEST_ASSERT_NULL (zlink_socket (ctx, ZLINK_PUB, &msg_handler));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_NULL (zlink_socket (ctx, ZLINK_XPUB, &msg_handler));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_NULL (zlink_socket (ctx, ZLINK_PAIR, &spot_handler));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_NULL (zlink_socket (ctx, ZLINK_SUB, &msg_handler));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_NULL (zlink_socket (ctx, ZLINK_XPUB, &spot_handler));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_NULL (zlink_socket (ctx, ZLINK_STREAM, &xpub_handler));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_pair_socket_with_handler_dispatches_multipart ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    raw_handler_probe_t probe;
    g_probe = &probe;

    const zlink_socket_handler_t msg_handler =
      make_msg_handler (&capture_raw_message);
    const zlink_socket_handler_t discard_handler =
      make_msg_handler (&discard_raw_message);

    void *server = zlink_socket (ctx, ZLINK_PAIR, &msg_handler);
    TEST_ASSERT_NOT_NULL (server);

    void *client = zlink_socket (ctx, ZLINK_PAIR, &discard_handler);
    TEST_ASSERT_NOT_NULL (client);

    const unsigned char client_rid[] = {'p', 'a', 'i', 'r', 'A'};
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      client, ZLINK_ROUTING_ID, client_rid, sizeof (client_rid)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));
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

    const zlink_socket_handler_t msg_handler =
      make_msg_handler (&capture_raw_message);
    const zlink_socket_handler_t discard_handler =
      make_msg_handler (&discard_raw_message);

    void *server = zlink_socket (ctx, ZLINK_ROUTER, &msg_handler);
    TEST_ASSERT_NOT_NULL (server);

    void *client = zlink_socket (ctx, ZLINK_DEALER, &discard_handler);
    TEST_ASSERT_NOT_NULL (client);

    const unsigned char client_rid[] = {'d', 'e', 'a', 'l', 'e', 'r'};
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      client, ZLINK_ROUTING_ID, client_rid, sizeof (client_rid)));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));
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

void test_sub_socket_with_handler_applies_filter_before_dispatch ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    spot_handler_probe_t probe;
    g_spot_probe = &probe;
    const zlink_socket_handler_t spot_handler =
      make_spot_handler (&capture_spot_message);

    void *sub = zlink_socket (ctx, ZLINK_SUB, &spot_handler);
    TEST_ASSERT_NOT_NULL (sub);

    void *pub = zlink_socket (ctx, ZLINK_PUB, NULL);
    TEST_ASSERT_NOT_NULL (pub);

    const unsigned char pub_rid[] = {'p', 'u', 'b', 'A'};
    TEST_ASSERT_SUCCESS_ERRNO (zlink_setsockopt (
      pub, ZLINK_ROUTING_ID, pub_rid, sizeof (pub_rid)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (sub, ZLINK_SUBSCRIBE, "topic", 5));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (pub, endpoint, sizeof endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sub, endpoint));
    msleep (SETTLE_TIME);

    s_send_seq (pub, "other", "skip", SEQ_END);
    msleep (100);
    TEST_ASSERT_EQUAL_INT (0, probe.calls.load ());

    s_send_seq (pub, "topic", "payload", SEQ_END);

    TEST_ASSERT_TRUE (wait_for_calls (&probe.calls, 1, 2000));
    TEST_ASSERT_EQUAL_INT (1, probe.part_count.load ());
    TEST_ASSERT_EQUAL_STRING ("topic", probe.topic);
    TEST_ASSERT_EQUAL_STRING ("payload", probe.parts[0]);
    TEST_ASSERT_EQUAL_INT (0, probe.close_failures.load ());

    close_zero_linger (sub);
    close_zero_linger (pub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    g_spot_probe = NULL;
}

void test_xpub_socket_with_handler_receives_subscription_events ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    xpub_handler_probe_t probe;
    g_xpub_probe = &probe;
    const zlink_socket_handler_t xpub_handler =
      make_xpub_handler (&capture_xpub_event);
    const zlink_socket_handler_t spot_handler =
      make_spot_handler (&capture_spot_message);

    void *xpub = zlink_socket (ctx, ZLINK_XPUB, &xpub_handler);
    TEST_ASSERT_NOT_NULL (xpub);
    void *sub = zlink_socket (ctx, ZLINK_SUB, &spot_handler);
    TEST_ASSERT_NOT_NULL (sub);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (xpub, endpoint, sizeof endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sub, endpoint));
    msleep (SETTLE_TIME);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (sub, ZLINK_SUBSCRIBE, "topic", 5));

    TEST_ASSERT_TRUE (wait_for_calls (&probe.calls, 1, 2000));
    TEST_ASSERT_EQUAL_INT (1, probe.subscribed.load ());
    TEST_ASSERT_EQUAL_STRING ("topic", probe.topic);

    close_zero_linger (sub);
    close_zero_linger (xpub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    g_xpub_probe = NULL;
}
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_socket_handler_family_validation_on_create);
    RUN_TEST (test_pair_socket_with_handler_dispatches_multipart);
    RUN_TEST (test_router_socket_with_handler_strips_routing_frame);
    RUN_TEST (test_sub_socket_with_handler_applies_filter_before_dispatch);
    RUN_TEST (test_xpub_socket_with_handler_receives_subscription_events);
    return UNITY_END ();
}
