/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_monitoring.hpp"
#include "testutil_unity.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <vector>

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
    xpub_handler_probe_t () : calls (0), subscribed (0), rid_size (0), topic_len (0)
    {
        memset (rid, 0, sizeof (rid));
        memset (topic, 0, sizeof (topic));
    }

    std::atomic<int> calls;
    std::atomic<int> subscribed;
    unsigned char rid[255];
    size_t rid_size;
    char topic[64];
    size_t topic_len;
};

struct concurrent_send_probe_t
{
    concurrent_send_probe_t () : calls (0), close_failures (0) {}

    std::atomic<int> calls;
    std::atomic<int> close_failures;
    std::mutex mutex;
    std::condition_variable cv;
};

struct raw_monitor_probe_t
{
    raw_monitor_probe_t () : calls (0), last_event (0) {}

    std::atomic<int> calls;
    uint64_t last_event;
    std::mutex mutex;
    std::condition_variable cv;
};

raw_handler_probe_t *g_probe = NULL;
raw_handler_probe_t *g_replacement_probe = NULL;
spot_handler_probe_t *g_spot_probe = NULL;
xpub_handler_probe_t *g_xpub_probe = NULL;
concurrent_send_probe_t *g_concurrent_send_probe = NULL;
raw_monitor_probe_t *g_raw_monitor_probe = NULL;

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
                               size_t part_count_,
                          void *)
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
                          size_t part_count_,
                          void *)
{
    capture_raw_message_into (g_probe, source_rid_, parts_, part_count_, NULL);
}

void capture_replacement_raw_message (const zlink_routing_id_t *source_rid_,
                                      zlink_msg_t *parts_,
                                      size_t part_count_,
                          void *)
{
    capture_raw_message_into (g_replacement_probe, source_rid_, parts_,
                              part_count_, NULL);
}

void capture_spot_message (const zlink_routing_id_t *source_rid_,
                           const char *topic_,
                           size_t topic_len_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                          void *)
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

void capture_xpub_event (const zlink_routing_id_t *source_rid_,
                         int subscribed_,
                         const char *topic_,
                         size_t topic_len_,
                         void *)
{
    if (!g_xpub_probe)
        return;

    g_xpub_probe->subscribed.store (subscribed_);
    g_xpub_probe->rid_size = source_rid_ ? source_rid_->size : 0;
    if (source_rid_ && source_rid_->size > 0)
        memcpy (g_xpub_probe->rid, source_rid_->data, source_rid_->size);
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
                          size_t part_count_,
                          void *)
{
    close_parts (NULL, parts_, part_count_);
}

void count_concurrent_raw_message (const zlink_routing_id_t *,
                                   zlink_msg_t *parts_,
                                   size_t part_count_,
                          void *)
{
    concurrent_send_probe_t *probe = g_concurrent_send_probe;
    for (size_t i = 0; i < part_count_; ++i) {
        const int rc = zlink_msg_close (&parts_[i]);
        if (rc != 0 && probe)
            probe->close_failures.fetch_add (1);
    }

    if (!probe)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->calls.fetch_add (1);
    }
    probe->cv.notify_all ();
}

bool wait_for_concurrent_calls (concurrent_send_probe_t *probe_,
                                int expected_,
                                int timeout_ms_)
{
    if (!probe_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_, expected_]() { return probe_->calls.load () >= expected_; });
}

void raw_monitor_ready_handler (const zlink_monitor_event_t *event_, void *)
{
    raw_monitor_probe_t *probe = g_raw_monitor_probe;
    if (!probe || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->last_event = event_->event;
        probe->calls.fetch_add (1);
    }
    probe->cv.notify_all ();
}

bool wait_for_raw_monitor_event (raw_monitor_probe_t *probe_,
                                 int expected_calls_,
                                 uint64_t expected_event_,
                                 int timeout_ms_)
{
    if (!probe_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_, expected_calls_, expected_event_]() {
          return probe_->calls.load () >= expected_calls_
                 && probe_->last_event == expected_event_;
      });
}

void test_socket_handler_family_validation_on_create ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pair = zlink_socket (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (pair);
    void *pub = zlink_socket (ctx, ZLINK_PUB);
    TEST_ASSERT_NOT_NULL (pub);
    void *xpub = zlink_socket (ctx, ZLINK_XPUB);
    TEST_ASSERT_NOT_NULL (xpub);
    void *sub = zlink_socket (ctx, ZLINK_SUB);
    TEST_ASSERT_NOT_NULL (sub);
    void *stream = zlink_socket (ctx, ZLINK_STREAM);
    TEST_ASSERT_NOT_NULL (stream);

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_recv_handler (pub, &capture_raw_message, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_recv_handler (xpub, &capture_raw_message, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_subscribe_handler (pair, &capture_spot_message, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_subscribe_handler (xpub, &capture_spot_message, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_subscription_event_handler (stream, &capture_xpub_event, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    close_zero_linger (pair);
    close_zero_linger (pub);
    close_zero_linger (xpub);
    close_zero_linger (sub);
    close_zero_linger (stream);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_socket_attach_handler_is_one_way ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    raw_handler_probe_t probe;
    g_probe = &probe;

    void *server = zlink_socket (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_recv_handler (server, &capture_raw_message, NULL));

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_recv_handler (server, &capture_raw_message, NULL));
    TEST_ASSERT_EQUAL_INT (EBUSY, errno);

    unsigned char recv_buf[8];
    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_recv (server, recv_buf, sizeof (recv_buf),
                                           ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_INT (EBUSY, errno);

    close_zero_linger (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    g_probe = NULL;
}

void test_pair_socket_with_handler_dispatches_multipart ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    raw_handler_probe_t probe;
    g_probe = &probe;

    void *server = zlink_socket (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_recv_handler (server, &capture_raw_message, NULL));

    void *client = zlink_socket (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_recv_handler (client, &discard_raw_message, NULL));

    const unsigned char client_rid[] = {'p', 'a', 'i', 'r', 'A'};
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (client, client_rid, sizeof (client_rid)));

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

void test_pair_socket_with_handler_dispatches_multipart_over_inproc ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    raw_handler_probe_t probe;
    g_probe = &probe;

    void *receiver = zlink_socket (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (receiver);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_recv_handler (receiver, &capture_raw_message, NULL));

    void *sender = zlink_socket (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (sender);

    const unsigned char sender_rid[] = {'p', 'i', 'n', 'p', 'r', 'o', 'c'};
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (sender, sender_rid, sizeof (sender_rid)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (receiver, "inproc://pair-handler"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sender, "inproc://pair-handler"));
    msleep (SETTLE_TIME);

    s_send_seq (sender, "alpha", "beta", SEQ_END);

    TEST_ASSERT_TRUE (wait_for_calls (&probe.calls, 1, 2000));
    TEST_ASSERT_EQUAL_INT (2, probe.part_count.load ());
    TEST_ASSERT_TRUE (probe.rid_size > 0);
    TEST_ASSERT_EQUAL_STRING ("alpha", probe.parts[0]);
    TEST_ASSERT_EQUAL_STRING ("beta", probe.parts[1]);
    TEST_ASSERT_EQUAL_INT (0, probe.close_failures.load ());

    close_zero_linger (sender);
    close_zero_linger (receiver);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    g_probe = NULL;
}

void test_dealer_socket_with_handler_dispatches_multipart_over_inproc ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    raw_handler_probe_t probe;
    g_probe = &probe;

    void *receiver = zlink_socket (ctx, ZLINK_DEALER);
    TEST_ASSERT_NOT_NULL (receiver);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_recv_handler (receiver, &capture_raw_message, NULL));

    void *sender = zlink_socket (ctx, ZLINK_DEALER);
    TEST_ASSERT_NOT_NULL (sender);

    const unsigned char sender_rid[] = {'d', 'i', 'n', 'p', 'r', 'o', 'c'};
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (sender, sender_rid, sizeof (sender_rid)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (receiver, "inproc://dealer-handler"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (sender, "inproc://dealer-handler"));
    msleep (SETTLE_TIME);

    s_send_seq (sender, "left", "right", SEQ_END);

    TEST_ASSERT_TRUE (wait_for_calls (&probe.calls, 1, 2000));
    TEST_ASSERT_EQUAL_INT (2, probe.part_count.load ());
    TEST_ASSERT_TRUE (probe.rid_size > 0);
    TEST_ASSERT_EQUAL_STRING ("left", probe.parts[0]);
    TEST_ASSERT_EQUAL_STRING ("right", probe.parts[1]);
    TEST_ASSERT_EQUAL_INT (0, probe.close_failures.load ());

    close_zero_linger (sender);
    close_zero_linger (receiver);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    g_probe = NULL;
}

void test_router_socket_with_handler_strips_routing_frame ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    raw_handler_probe_t probe;
    g_probe = &probe;

    void *server = zlink_socket (ctx, ZLINK_ROUTER);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_recv_handler (server, &capture_raw_message, NULL));

    void *client = zlink_socket (ctx, ZLINK_DEALER);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_recv_handler (client, &discard_raw_message, NULL));

    const unsigned char client_rid[] = {'d', 'e', 'a', 'l', 'e', 'r'};
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (client, client_rid, sizeof (client_rid)));

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

void test_router_socket_with_handler_strips_routing_frame_over_inproc ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    raw_handler_probe_t probe;
    g_probe = &probe;

    void *receiver = zlink_socket (ctx, ZLINK_ROUTER);
    TEST_ASSERT_NOT_NULL (receiver);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_recv_handler (receiver, &capture_raw_message, NULL));

    void *sender = zlink_socket (ctx, ZLINK_DEALER);
    TEST_ASSERT_NOT_NULL (sender);

    const unsigned char sender_rid[] = {'r', 'i', 'n', 'p', 'r', 'o', 'c'};
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (sender, sender_rid, sizeof (sender_rid)));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (receiver, "inproc://router-handler"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_connect (sender, "inproc://router-handler"));
    msleep (SETTLE_TIME);

    s_send_seq (sender, "first", "second", SEQ_END);

    TEST_ASSERT_TRUE (wait_for_calls (&probe.calls, 1, 2000));
    TEST_ASSERT_EQUAL_INT (2, probe.part_count.load ());
    TEST_ASSERT_TRUE (probe.rid_size > 0);
    TEST_ASSERT_EQUAL_STRING ("first", probe.parts[0]);
    TEST_ASSERT_EQUAL_STRING ("second", probe.parts[1]);
    TEST_ASSERT_EQUAL_INT (0, probe.close_failures.load ());

    close_zero_linger (sender);
    close_zero_linger (receiver);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    g_probe = NULL;
}

void test_sub_socket_with_handler_applies_filter_before_dispatch ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    spot_handler_probe_t probe;
    g_spot_probe = &probe;

    void *sub = zlink_socket (ctx, ZLINK_SUB);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (sub, &capture_spot_message, NULL));

    void *pub = zlink_socket (ctx, ZLINK_PUB);
    TEST_ASSERT_NOT_NULL (pub);

    const unsigned char pub_rid[] = {'p', 'u', 'b', 'A'};
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (pub, pub_rid, sizeof (pub_rid)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "topic"));

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

void test_sub_socket_with_raw_handler_dispatches_topic_and_payload ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    raw_handler_probe_t probe;
    g_probe = &probe;

    void *sub = zlink_socket (ctx, ZLINK_SUB);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_recv_handler (sub, &capture_raw_message, NULL));

    void *pub = zlink_socket (ctx, ZLINK_PUB);
    TEST_ASSERT_NOT_NULL (pub);

    const unsigned char pub_rid[] = {'p', 'u', 'b', 'R', 'A', 'W'};
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (pub, pub_rid, sizeof (pub_rid)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "topic"));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (pub, endpoint, sizeof endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sub, endpoint));
    msleep (SETTLE_TIME);

    s_send_seq (pub, "other", "skip", SEQ_END);
    msleep (100);
    TEST_ASSERT_EQUAL_INT (0, probe.calls.load ());

    s_send_seq (pub, "topic", "payload", SEQ_END);

    TEST_ASSERT_TRUE (wait_for_calls (&probe.calls, 1, 2000));
    TEST_ASSERT_EQUAL_INT (2, probe.part_count.load ());
    TEST_ASSERT_EQUAL_UINT (sizeof (pub_rid), probe.rid_size);
    TEST_ASSERT_EQUAL_MEMORY (pub_rid, probe.rid, sizeof (pub_rid));
    TEST_ASSERT_EQUAL_STRING ("topic", probe.parts[0]);
    TEST_ASSERT_EQUAL_STRING ("payload", probe.parts[1]);
    TEST_ASSERT_EQUAL_INT (0, probe.close_failures.load ());

    close_zero_linger (sub);
    close_zero_linger (pub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    g_probe = NULL;
}

void test_sub_socket_with_handler_applies_filter_before_dispatch_over_inproc ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    spot_handler_probe_t probe;
    g_spot_probe = &probe;

    void *sub = zlink_socket (ctx, ZLINK_SUB);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (sub, &capture_spot_message, NULL));

    void *pub = zlink_socket (ctx, ZLINK_PUB);
    TEST_ASSERT_NOT_NULL (pub);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "topic"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (pub, "inproc://sub-handler"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sub, "inproc://sub-handler"));
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

void test_raw_subscribe_recv_returns_topic_and_payload ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *sub = zlink_socket (ctx, ZLINK_SUB);
    TEST_ASSERT_NOT_NULL (sub);
    void *pub = zlink_socket (ctx, ZLINK_PUB);
    TEST_ASSERT_NOT_NULL (pub);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub, "topic"));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (pub, endpoint, sizeof endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sub, endpoint));
    msleep (SETTLE_TIME);

    s_send_seq (pub, "topic", "payload", SEQ_END);

    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[32];
    memset (topic, 0, sizeof (topic));
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_SUCCESS_ERRNO (::zlink_subscribe (
      sub, &source_rid, &parts, &part_count, topic, &topic_len, 0));
    TEST_ASSERT_EQUAL_UINT (0, source_rid.size);
    TEST_ASSERT_TRUE (topic_len >= 5);
    TEST_ASSERT_EQUAL_MEMORY ("topic", topic, 5);
    TEST_ASSERT_EQUAL_UINT (1, part_count);
    TEST_ASSERT_EQUAL_STRING ("payload",
                              static_cast<const char *> (
                                zlink_msg_data (&parts[0])));
    zlink_multipart_close (parts, part_count);

    close_zero_linger (sub);
    close_zero_linger (pub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_pubsub_generic_surface_accepts_raw_prefix_filters_and_raw_publish ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *sub = zlink_socket (ctx, ZLINK_SUB);
    TEST_ASSERT_NOT_NULL (sub);
    void *pub = zlink_socket (ctx, ZLINK_PUB);
    TEST_ASSERT_NOT_NULL (pub);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub, "*"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub, "bad*mid"));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub, ""));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (pub, endpoint, sizeof endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sub, endpoint));
    msleep (SETTLE_TIME);

    zlink_msg_t part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 7));
    memcpy (zlink_msg_data (&part), "payload", 7);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_publish (pub, NULL, &part, 1, 0));

    char buffer[32];
    memset (buffer, 0, sizeof (buffer));
    TEST_ASSERT_EQUAL_INT (7, zlink_recv (sub, buffer, sizeof (buffer), 0));
    TEST_ASSERT_EQUAL_STRING ("payload", buffer);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
    memcpy (zlink_msg_data (&part), "drop", 4);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_publish (pub, "topic", &part, 1, 0));

    char topic[32];
    memset (topic, 0, sizeof (topic));
    size_t topic_len = sizeof (topic);
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe (sub, NULL, &parts, &part_count, topic, &topic_len, 0));
    TEST_ASSERT_EQUAL_UINT64 (1, part_count);
    TEST_ASSERT_EQUAL_UINT64 (5, topic_len);
    TEST_ASSERT_EQUAL_MEMORY ("topic", topic, topic_len);
    TEST_ASSERT_EQUAL_UINT64 (4, zlink_msg_size (&parts[0]));
    TEST_ASSERT_EQUAL_MEMORY ("drop", zlink_msg_data (&parts[0]), 4);
    zlink_multipart_close (parts, part_count);
    free (parts);

    close_zero_linger (sub);
    close_zero_linger (pub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_xpub_socket_with_handler_receives_subscription_events ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    xpub_handler_probe_t probe;
    g_xpub_probe = &probe;

    void *xpub = zlink_socket (ctx, ZLINK_XPUB);
    TEST_ASSERT_NOT_NULL (xpub);
    int verbose = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_pub_option (xpub, ZLINK_PUB_OPT_VERBOSE, &verbose, sizeof (verbose)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscription_event_handler (xpub, &capture_xpub_event, NULL));
    void *sub = zlink_socket (ctx, ZLINK_SUB);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscribe_handler (sub, &capture_spot_message, NULL));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (xpub, endpoint, sizeof endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sub, endpoint));
    msleep (SETTLE_TIME);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "topic"));

    TEST_ASSERT_TRUE (wait_for_calls (&probe.calls, 1, 2000));
    TEST_ASSERT_EQUAL_INT (1, probe.subscribed.load ());
    TEST_ASSERT_TRUE (probe.rid_size > 0);
    TEST_ASSERT_EQUAL_STRING ("topic", probe.topic);

    close_zero_linger (sub);
    close_zero_linger (xpub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    g_xpub_probe = NULL;
}

void test_xpub_direct_recv_returns_source_rid_and_topic ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *xpub = zlink_socket (ctx, ZLINK_XPUB);
    TEST_ASSERT_NOT_NULL (xpub);
    int verbose = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_pub_option (xpub, ZLINK_PUB_OPT_VERBOSE, &verbose, sizeof (verbose)));
    void *sub = zlink_socket (ctx, ZLINK_SUB);
    TEST_ASSERT_NOT_NULL (sub);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (xpub, endpoint, sizeof endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sub, endpoint));
    msleep (SETTLE_TIME);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "topic"));

    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    int subscribed = 0;
    char topic[32];
    memset (topic, 0, sizeof (topic));
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_subscription_event (
        xpub, &source_rid, &subscribed, topic, &topic_len, 0));
    TEST_ASSERT_EQUAL_INT (1, subscribed);
    TEST_ASSERT_TRUE (source_rid.size > 0);
    TEST_ASSERT_EQUAL_UINT (5, topic_len);
    TEST_ASSERT_EQUAL_STRING ("topic", topic);

    close_zero_linger (sub);
    close_zero_linger (xpub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_xpub_direct_recv_reports_emsgsize_and_required_topic_length ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *xpub = zlink_socket (ctx, ZLINK_XPUB);
    TEST_ASSERT_NOT_NULL (xpub);
    int verbose = 1;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_pub_option (xpub, ZLINK_PUB_OPT_VERBOSE, &verbose, sizeof (verbose)));
    void *sub = zlink_socket (ctx, ZLINK_SUB);
    TEST_ASSERT_NOT_NULL (sub);

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (xpub, endpoint, sizeof endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (sub, endpoint));
    msleep (SETTLE_TIME);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub, "topic"));

    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    int subscribed = 0;
    char topic[4];
    memset (topic, 0, sizeof (topic));
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_subscription_event (
            xpub, &source_rid, &subscribed, topic, &topic_len, 0));
    TEST_ASSERT_EQUAL_INT (EMSGSIZE, zlink_errno ());
    TEST_ASSERT_EQUAL_UINT (5, topic_len);
    TEST_ASSERT_EQUAL_INT (1, subscribed);
    TEST_ASSERT_TRUE (source_rid.size > 0);

    close_zero_linger (sub);
    close_zero_linger (xpub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_pair_socket_same_handle_concurrent_send_is_thread_safe ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    concurrent_send_probe_t probe;
    g_concurrent_send_probe = &probe;

    void *server = zlink_socket (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_recv_handler (server, &count_concurrent_raw_message, NULL));

    void *client = zlink_socket (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_recv_handler (client, &discard_raw_message, NULL));

    raw_monitor_probe_t monitor_probe;
    g_raw_monitor_probe = &monitor_probe;
    zlink_socket_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = ZLINK_EVENT_CONNECTION_READY;
    void *monitor = zlink_socket_monitor_open (server, &opts);
    TEST_ASSERT_NOT_NULL (monitor);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_socket_monitor_handler (monitor, &raw_monitor_ready_handler,
                                    NULL));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));
    TEST_ASSERT_TRUE (wait_for_raw_monitor_event (
      &monitor_probe, 1, ZLINK_EVENT_CONNECTION_READY, 3000));

    const char warmup[] = "warmup";
    TEST_ASSERT_EQUAL_INT (
      static_cast<int> (sizeof (warmup) - 1),
      zlink_send (client, warmup, sizeof (warmup) - 1, 0));
    TEST_ASSERT_TRUE (wait_for_concurrent_calls (&probe, 1, 3000));

    const int sender_count = 4;
    const int messages_per_sender = 32;
    std::mutex start_mutex;
    std::condition_variable start_cv;
    int ready_threads = 0;
    bool start = false;
    std::vector<int> worker_errno (sender_count, 0);
    std::vector<int> worker_progress (sender_count, 0);
    std::vector<std::thread> workers;

    for (int i = 0; i < sender_count; ++i) {
        workers.push_back (std::thread ([&, i]() {
            {
                std::unique_lock<std::mutex> lock (start_mutex);
                ++ready_threads;
                start_cv.notify_all ();
                start_cv.wait (lock, [&start]() { return start; });
            }

            for (int seq = 0; seq < messages_per_sender; ++seq) {
                char payload[32];
                const int len = snprintf (payload, sizeof (payload),
                                          "sender-%d-%02d", i, seq);
                const int rc = zlink_send (client, payload,
                                           static_cast<size_t> (len), 0);
                if (rc != len) {
                    worker_errno[i] = errno;
                    return;
                }
                worker_progress[i] += 1;
            }
        }));
    }

    {
        std::unique_lock<std::mutex> lock (start_mutex);
        TEST_ASSERT_TRUE (start_cv.wait_for (
          lock, std::chrono::seconds (5),
          [&ready_threads, sender_count]() { return ready_threads == sender_count; }));
        start = true;
        start_cv.notify_all ();
    }

    for (size_t i = 0; i < workers.size (); ++i)
        workers[i].join ();

    const int expected_calls = 1 + sender_count * messages_per_sender;
    TEST_ASSERT_TRUE (wait_for_concurrent_calls (&probe, expected_calls, 3000));

    for (int i = 0; i < sender_count; ++i) {
        TEST_ASSERT_EQUAL_INT (messages_per_sender, worker_progress[i]);
        TEST_ASSERT_EQUAL_INT (0, worker_errno[i]);
    }
    TEST_ASSERT_EQUAL_INT (0, probe.close_failures.load ());

    close_zero_linger (client);
    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (monitor, ZLINK_OPT_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_monitor_close (&monitor));
    close_zero_linger (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    g_concurrent_send_probe = NULL;
    g_raw_monitor_probe = NULL;
}
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_socket_handler_family_validation_on_create);
    RUN_TEST (test_socket_attach_handler_is_one_way);
    RUN_TEST (test_pair_socket_with_handler_dispatches_multipart);
    RUN_TEST (test_pair_socket_with_handler_dispatches_multipart_over_inproc);
    RUN_TEST (test_dealer_socket_with_handler_dispatches_multipart_over_inproc);
    RUN_TEST (test_router_socket_with_handler_strips_routing_frame);
    RUN_TEST (test_router_socket_with_handler_strips_routing_frame_over_inproc);
    RUN_TEST (test_sub_socket_with_handler_applies_filter_before_dispatch);
    RUN_TEST (test_sub_socket_with_raw_handler_dispatches_topic_and_payload);
    RUN_TEST (
      test_sub_socket_with_handler_applies_filter_before_dispatch_over_inproc);
    RUN_TEST (test_raw_subscribe_recv_returns_topic_and_payload);
    RUN_TEST (test_pubsub_generic_surface_accepts_raw_prefix_filters_and_raw_publish);
    RUN_TEST (test_xpub_socket_with_handler_receives_subscription_events);
    RUN_TEST (test_xpub_direct_recv_returns_source_rid_and_topic);
    RUN_TEST (test_xpub_direct_recv_reports_emsgsize_and_required_topic_length);
    RUN_TEST (test_pair_socket_same_handle_concurrent_send_is_thread_safe);
    return UNITY_END ();
}
