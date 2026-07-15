/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"
#include "services/spot/pubsub/spot_subject_access.hpp"
#include "services/spot/runtime/spot_handle.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <string.h>
#include <thread>
#include <vector>

namespace
{
struct spot_dispatch_probe_t
{
    spot_dispatch_probe_t () : event (0), subject_kind (0), subject (NULL), called (false) {}

    std::mutex mutex;
    std::condition_variable cv;
    int event;
    int subject_kind;
    void *subject;
    bool called;
};

struct spot_dispatch_recv_probe_t
{
    spot_dispatch_recv_probe_t () :
        timer (NULL),
        callback_count (0),
        inflight (0),
        max_inflight (0),
        first_callback_entered (false),
        release_first_callback (false),
        block_first_callback (false),
        subscribe_new_filter_after_first_payload (false),
        subscription_changed_in_callback (false),
        failed (false),
        last_errno (0),
        routed_request_seq (0),
        channel_reply_callback_count (0),
        channel_reply_subject (NULL),
        channel_reply_result (ZLINK_REQUEST_INTERNAL_ERROR)
    {
    }

    std::mutex mutex;
    std::condition_variable cv;
    void *timer;
    size_t callback_count;
    int inflight;
    int max_inflight;
    bool first_callback_entered;
    bool release_first_callback;
    bool block_first_callback;
    bool subscribe_new_filter_after_first_payload;
    bool subscription_changed_in_callback;
    bool failed;
    int last_errno;
    std::vector<int> events;
    std::vector<std::string> subscribe_topics;
    std::vector<std::string> subscribe_payloads;
    std::vector<std::string> timer_fire_counts;
    std::vector<std::string> routed_payloads;
    std::vector<std::string> channel_reply_payloads;
    std::vector<std::string> channel_reply_channel_names;
    std::vector<void *> channel_reply_subjects;
    std::string routed_source_rid;
    std::string routed_spot_rid;
    std::string channel_name;
    uint64_t routed_request_seq;
    size_t channel_reply_callback_count;
    void *channel_reply_subject;
    zlink_request_result_t channel_reply_result;
};

void init_string_part (zlink_msg_t *part_, const char *text_)
{
    const size_t size = strlen (text_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (part_, size));
    memcpy (zlink_msg_data (part_), text_, size);
}

void set_routing_id_text (void *handle_, const char *text_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (handle_, text_, strlen (text_)));
}

zlink_routing_id_t get_routing_id_value (void *handle_)
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (handle_, &rid));
    return rid;
}

std::string msg_to_string (const zlink_msg_t *part_)
{
    zlink_msg_t *mutable_part = const_cast<zlink_msg_t *> (part_);
    return std::string (static_cast<const char *> (zlink_msg_data (mutable_part)),
                        zlink_msg_size (part_));
}

int drain_completion_via_poller (void *subject_)
{
    void *poller = zlink_poller_new ();
    if (!poller)
        return -1;
    int rc = -1;
    if (zlink_poller_add (poller, subject_, NULL, ZLINK_POLLCOMPLETION) == ZLINK_CONFIG_OK) {
        zlink_poller_event_t event;
        rc = zlink_poller_wait (poller, &event, 1, 0, NULL);
        (void) zlink_poller_remove (poller, subject_);
    }
    (void) zlink_poller_destroy (&poller);
    return rc;
}

bool wait_for_channel_reply_callbacks (spot_dispatch_recv_probe_t *probe_,
                                       void *spot_,
                                       size_t expected_count_,
                                       int timeout_ms_,
                                       size_t expected_subject_count_ = static_cast<size_t> (-1))
{
    if (expected_subject_count_ == static_cast<size_t> (-1))
        expected_subject_count_ = expected_count_;
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        (void) drain_completion_via_poller (spot_);
        {
            std::unique_lock<std::mutex> lock (probe_->mutex);
            if (probe_->cv.wait_for (lock, std::chrono::milliseconds (10),
                                     [probe_, expected_count_, expected_subject_count_] () {
                                         return probe_->failed
                                                || probe_->channel_reply_callback_count
                                                       >= expected_count_
                                                     && probe_->channel_reply_subjects.size ()
                                                          >= expected_subject_count_;
                                     }))
                return true;
        }
    }
    (void) drain_completion_via_poller (spot_);
    std::lock_guard<std::mutex> lock (probe_->mutex);
    return probe_->failed
           || (probe_->channel_reply_callback_count >= expected_count_
               && probe_->channel_reply_subjects.size () >= expected_subject_count_);
}

bool wait_for_channel_reply_callbacks_on_poller (
  spot_dispatch_recv_probe_t *probe_,
  void *poller_,
  size_t expected_count_,
  int timeout_ms_,
  size_t expected_subject_count_ = static_cast<size_t> (-1))
{
    if (expected_subject_count_ == static_cast<size_t> (-1))
        expected_subject_count_ = expected_count_;
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink_poller_event_t event;
        (void) zlink_poller_wait (poller_, &event, 1, 10, NULL);
        std::lock_guard<std::mutex> lock (probe_->mutex);
        if (probe_->failed
            || (probe_->channel_reply_callback_count >= expected_count_
                && probe_->channel_reply_subjects.size () >= expected_subject_count_))
            return true;
    }
    return false;
}

void on_spot_dispatch_event (void *, const zlink_spot_dispatch_info_t *info_, void *userdata_)
{
    spot_dispatch_probe_t *probe = static_cast<spot_dispatch_probe_t *> (userdata_);
    std::lock_guard<std::mutex> lock (probe->mutex);
    probe->event = info_ ? static_cast<int> (info_->event) : 0;
    probe->subject_kind = info_ ? static_cast<int> (info_->subject_kind) : 0;
    probe->subject = info_ ? info_->subject : NULL;
    probe->called = true;
    probe->cv.notify_all ();
}

void on_channel_reply (zlink_request_result_t result_,
                       zlink_msg_t *parts_,
                       size_t part_count_,
                       void *userdata_)
{
    spot_dispatch_recv_probe_t *probe = static_cast<spot_dispatch_recv_probe_t *> (userdata_);
    if (!probe)
        return;

    std::lock_guard<std::mutex> lock (probe->mutex);
    probe->channel_reply_result = result_;
    ++probe->channel_reply_callback_count;
    probe->channel_reply_payloads.push_back (part_count_ > 0 ? msg_to_string (&parts_[0])
                                                             : std::string ());
    probe->cv.notify_all ();
}

void on_spot_dispatch_recv_event (void *spot_,
                                  const zlink_spot_dispatch_info_t *info_,
                                  void *userdata_)
{
    spot_dispatch_recv_probe_t *probe = static_cast<spot_dispatch_recv_probe_t *> (userdata_);
    if (!probe || !info_)
        return;

    {
        std::unique_lock<std::mutex> lock (probe->mutex);
        ++probe->callback_count;
        ++probe->inflight;
        if (probe->inflight > probe->max_inflight)
            probe->max_inflight = probe->inflight;
        probe->events.push_back (static_cast<int> (info_->event));
        if (probe->block_first_callback && !probe->first_callback_entered) {
            probe->first_callback_entered = true;
            probe->cv.notify_all ();
            while (!probe->release_first_callback)
                probe->cv.wait (lock);
        }
    }

    if (info_->event == ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE) {
        for (;;) {
            zlink_routing_id_t source_rid;
            zlink_msg_t *parts = NULL;
            size_t part_count = 0;
            char topic[64];
            size_t topic_len = sizeof (topic);
            memset (&source_rid, 0, sizeof (source_rid));

            const zlink_recv_result_t rc = zlink_subscribe (spot_, &source_rid, &parts, &part_count,
                                                            topic, &topic_len, ZLINK_DONTWAIT);
            if (rc == ZLINK_RECV_NO_DATA && zlink_errno () == EAGAIN)
                break;
            if (rc != ZLINK_RECV_OK) {
                std::lock_guard<std::mutex> lock (probe->mutex);
                probe->failed = true;
                probe->last_errno = zlink_errno ();
                break;
            }

            std::lock_guard<std::mutex> lock (probe->mutex);
            probe->subscribe_topics.push_back (std::string (topic, topic_len));
            probe->subscribe_payloads.push_back (part_count > 0 ? msg_to_string (&parts[0])
                                                                : std::string ());
            zlink_multipart_close (parts, part_count);
            if (probe->subscribe_new_filter_after_first_payload
                && !probe->subscription_changed_in_callback) {
                probe->subscription_changed_in_callback = true;
                if (zlink_set_subscription (spot_, "dispatch.topic.changed") != ZLINK_CONFIG_OK) {
                    probe->failed = true;
                    probe->last_errno = zlink_errno ();
                }
            }
        }
    } else if (info_->event == ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE) {
        for (;;) {
            const zlink_routing_id_t *source_rid = NULL;
            const zlink_routing_id_t *spot_rid = NULL;
            uint64_t request_seq = 0;
            zlink_msg_t *parts = NULL;
            size_t part_count = 0;

            const zlink_recv_result_t rc = zlink_spot_recv (
              spot_, &source_rid, &spot_rid, &request_seq, &parts, &part_count, ZLINK_DONTWAIT);
            if (rc == ZLINK_RECV_NO_DATA && zlink_errno () == EAGAIN)
                break;
            if (rc != ZLINK_RECV_OK) {
                std::lock_guard<std::mutex> lock (probe->mutex);
                probe->failed = true;
                probe->last_errno = zlink_errno ();
                break;
            }

            std::lock_guard<std::mutex> lock (probe->mutex);
            probe->routed_request_seq = request_seq;
            probe->routed_source_rid.assign (reinterpret_cast<const char *> (source_rid->data),
                                             source_rid->size);
            probe->routed_spot_rid.assign (
              spot_rid ? reinterpret_cast<const char *> (spot_rid->data) : "",
              spot_rid ? spot_rid->size : 0);
            probe->routed_payloads.push_back (part_count > 0 ? msg_to_string (&parts[0])
                                                             : std::string ());
            zlink_multipart_close (parts, part_count);
        }
    } else if (info_->event == ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE && probe->timer != NULL
               && info_->subject_kind == ZLINK_SPOT_DISPATCH_SUBJECT_TIMER) {
        for (;;) {
            uint64_t fire_count = 0;
            const zlink_recv_result_t rc = zlink_timer_recv (info_->subject, &fire_count);
            if (rc == ZLINK_RECV_NO_DATA && zlink_errno () == EAGAIN)
                break;
            if (rc != ZLINK_RECV_OK) {
                std::lock_guard<std::mutex> lock (probe->mutex);
                probe->failed = true;
                probe->last_errno = zlink_errno ();
                break;
            }

            std::lock_guard<std::mutex> lock (probe->mutex);
            probe->timer_fire_counts.push_back (std::to_string (fire_count));
        }
    } else if (info_->event == ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE
               && info_->subject_kind == ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER) {
        size_t channel_name_len = 0;
        char channel_name_buf[64];
        memset (channel_name_buf, 0, sizeof (channel_name_buf));
        if (zlink_socket_get_channel_name (info_->subject, channel_name_buf,
                                           sizeof (channel_name_buf), &channel_name_len)
            != ZLINK_CONFIG_OK) {
            std::lock_guard<std::mutex> lock (probe->mutex);
            probe->failed = true;
            probe->last_errno = zlink_errno ();
        } else {
            std::lock_guard<std::mutex> lock (probe->mutex);
            probe->channel_reply_subject = info_->subject;
            probe->channel_name.assign (channel_name_buf, channel_name_len);
            probe->channel_reply_subjects.push_back (info_->subject);
            probe->channel_reply_channel_names.push_back (
              std::string (channel_name_buf, channel_name_len));
        }
    }

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        --probe->inflight;
    }
    probe->cv.notify_all ();
}

bool recv_router_request_until (void *router_,
                                zlink_routing_id_t *source_rid_out_,
                                uint64_t *request_seq_out_,
                                std::string *payload_out_,
                                int timeout_ms_)
{
    const zlink_routing_id_t *source_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        const zlink_recv_result_t rc =
          zlink_router_recv (router_, &source_rid, &source_spot_rid, &request_seq, &parts,
                             &part_count, ZLINK_DONTWAIT);
        if (rc == ZLINK_RECV_OK) {
            TEST_ASSERT_NOT_NULL (source_rid);
            TEST_ASSERT_TRUE (request_seq != 0);
            if (source_rid_out_)
                *source_rid_out_ = *source_rid;
            if (request_seq_out_)
                *request_seq_out_ = request_seq;
            if (payload_out_)
                *payload_out_ = part_count > 0 ? msg_to_string (&parts[0]) : std::string ();
            zlink_multipart_close (parts, part_count);
            return true;
        }
        TEST_ASSERT_TRUE (rc == ZLINK_RECV_NO_DATA && zlink_errno () == EAGAIN);
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    return false;
}

void send_malformed_router_reply (void *router_,
                                  const zlink_routing_id_t *peer_rid_,
                                  uint64_t request_seq_)
{
    zlink_msg_t parts[5];
    const unsigned char protocol_id = 0x01;
    const unsigned char version = 0x01;
    const unsigned char type = 0x03;
    const unsigned char invalid_errno = 0x7f;
    unsigned char seq_buf[8];
    for (size_t i = 0; i < 5; ++i)
        zlink_msg_init (&parts[i]);
    for (size_t i = 0; i < 8; ++i)
        seq_buf[7 - i] = static_cast<unsigned char> ((request_seq_ >> (i * 8)) & 0xffu);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[0], 1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[1], 1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[2], 1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[3], sizeof (seq_buf)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&parts[4], 1));
    memcpy (zlink_msg_data (&parts[0]), &protocol_id, 1);
    memcpy (zlink_msg_data (&parts[1]), &version, 1);
    memcpy (zlink_msg_data (&parts[2]), &type, 1);
    memcpy (zlink_msg_data (&parts[3]), seq_buf, sizeof (seq_buf));
    memcpy (zlink_msg_data (&parts[4]), &invalid_errno, 1);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_send_rid (router_, peer_rid_, parts, 5, static_cast<zlink_send_flags_t> (0)));
}

void test_spot_timer_dispatch_event_and_recv ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *spot = zlink_spot_new (node);
    void *timer = zlink_spot_timer_new (spot);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (spot);
    TEST_ASSERT_NOT_NULL (timer);

    spot_dispatch_probe_t probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (spot, &on_spot_dispatch_event, &probe));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_start (timer, 20 * 1000 * 1000ULL, 1));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool fired = probe.cv.wait_for (lock, std::chrono::milliseconds (500),
                                              [&probe] () { return probe.called; });
        TEST_ASSERT_TRUE (fired);
        TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE, probe.event);
        TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_DISPATCH_SUBJECT_TIMER, probe.subject_kind);
        TEST_ASSERT_EQUAL_PTR (timer, probe.subject);
    }

    uint64_t fire_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_recv (timer, &fire_count));
    TEST_ASSERT_EQUAL_UINT64 (1, fire_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_destroy (&timer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_dispatch_subscribe_recv_inside_callback ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *sub_spot = zlink_spot_new (node);
    void *pub_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (sub_spot);
    TEST_ASSERT_NOT_NULL (pub_spot);

    set_routing_id_text (pub_spot, "pub-spot");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub_spot, "dispatch.topic"));

    spot_dispatch_recv_probe_t probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (sub_spot, &on_spot_dispatch_recv_event, &probe));

    zlink_msg_t part;
    init_string_part (&part, "topic-payload");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_publish (pub_spot, "dispatch.topic", &part, 1, 0));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool received =
          probe.cv.wait_for (lock, std::chrono::milliseconds (500), [&probe] () {
              return probe.failed || probe.subscribe_payloads.size () >= 1;
          });
        TEST_ASSERT_TRUE (received);
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_UINT (1, probe.subscribe_payloads.size ());
        TEST_ASSERT_EQUAL_STRING ("dispatch.topic", probe.subscribe_topics[0].c_str ());
        TEST_ASSERT_EQUAL_STRING ("topic-payload", probe.subscribe_payloads[0].c_str ());
        TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE, probe.events[0]);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_dispatch_deferred_subscribe_recv_clears_readiness_signal ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *sub_spot = zlink_spot_new (node);
    void *pub_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (sub_spot);
    TEST_ASSERT_NOT_NULL (pub_spot);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub_spot, "dispatch.topic.deferred"));
    spot_dispatch_probe_t probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (sub_spot, &on_spot_dispatch_event, &probe));

    zlink_msg_t part;
    init_string_part (&part, "deferred-payload");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_publish (pub_spot, "dispatch.topic.deferred", &part, 1, 0));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        TEST_ASSERT_TRUE (probe.cv.wait_for (lock, std::chrono::milliseconds (500), [&probe] () {
            return probe.called;
        }));
    }

    const zlink_routing_id_t *source_rid = NULL;
    zlink_msg_t received_part;
    zlink_msg_init (&received_part);
    zlink_part_flag_t has_more = ZLINK_PART_MORE;
    char topic[64];
    size_t topic_len = 0;
    TEST_ASSERT_EQUAL_INT (
      ZLINK_RECV_OK,
      zlink_spot_subscribe_part (sub_spot, &source_rid, topic, sizeof (topic), &topic_len,
                                 &received_part, &has_more,
                                 static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT)));
    TEST_ASSERT_EQUAL_INT (ZLINK_PART_FINAL, has_more);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_close (&received_part));

    spot_handle_t *handle = as_spot_handle (sub_spot);
    TEST_ASSERT_NOT_NULL (handle);
    TEST_ASSERT_FALSE (handle->logical_state->subscribe_signal_armed);
    TEST_ASSERT_EQUAL_INT (-1, handle->logical_state->subscribe_signaler.recv_failable ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_dispatch_subscribe_event_is_not_fanned_out_to_unrelated_spot ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *sub_spot = zlink_spot_new (node);
    void *idle_spot = zlink_spot_new (node);
    void *pub_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (sub_spot);
    TEST_ASSERT_NOT_NULL (idle_spot);
    TEST_ASSERT_NOT_NULL (pub_spot);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub_spot, "dispatch.topic.one"));

    spot_dispatch_recv_probe_t sub_probe;
    spot_dispatch_recv_probe_t idle_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (sub_spot, &on_spot_dispatch_recv_event, &sub_probe));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (idle_spot, &on_spot_dispatch_recv_event, &idle_probe));

    zlink_msg_t part;
    init_string_part (&part, "isolated-payload");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_publish (pub_spot, "dispatch.topic.one", &part, 1, 0));

    {
        std::unique_lock<std::mutex> lock (sub_probe.mutex);
        const bool received =
          sub_probe.cv.wait_for (lock, std::chrono::milliseconds (500), [&sub_probe] () {
              return sub_probe.failed || sub_probe.subscribe_payloads.size () >= 1;
          });
        TEST_ASSERT_TRUE (received);
        TEST_ASSERT_FALSE (sub_probe.failed);
        TEST_ASSERT_EQUAL_UINT (1, sub_probe.subscribe_payloads.size ());
        TEST_ASSERT_EQUAL_STRING ("isolated-payload", sub_probe.subscribe_payloads[0].c_str ());
    }

    std::this_thread::sleep_for (std::chrono::milliseconds (100));
    {
        std::lock_guard<std::mutex> lock (idle_probe.mutex);
        TEST_ASSERT_FALSE (idle_probe.failed);
        TEST_ASSERT_EQUAL_UINT (0, idle_probe.callback_count);
        TEST_ASSERT_TRUE (idle_probe.events.empty ());
        TEST_ASSERT_TRUE (idle_probe.subscribe_payloads.empty ());
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&idle_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_dispatch_subscribe_drain_until_eagain ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *sub_spot = zlink_spot_new (node);
    void *pub_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (sub_spot);
    TEST_ASSERT_NOT_NULL (pub_spot);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub_spot, "dispatch.topic.drain"));

    spot_dispatch_recv_probe_t probe;
    probe.block_first_callback = true;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (sub_spot, &on_spot_dispatch_recv_event, &probe));

    zlink_msg_t part1;
    zlink_msg_t part2;
    zlink_msg_t part3;
    init_string_part (&part1, "payload-1");
    init_string_part (&part2, "payload-2");
    init_string_part (&part3, "payload-3");

    TEST_ASSERT_SUCCESS_ERRNO (zlink_publish (pub_spot, "dispatch.topic.drain", &part1, 1, 0));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool entered = probe.cv.wait_for (lock, std::chrono::milliseconds (500), [&probe] () {
            return probe.first_callback_entered;
        });
        TEST_ASSERT_TRUE (entered);
        TEST_ASSERT_EQUAL_INT (1, probe.max_inflight);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_publish (pub_spot, "dispatch.topic.drain", &part2, 1, 0));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_publish (pub_spot, "dispatch.topic.drain", &part3, 1, 0));

    {
        std::lock_guard<std::mutex> lock (probe.mutex);
        probe.release_first_callback = true;
    }
    probe.cv.notify_all ();

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool drained =
          probe.cv.wait_for (lock, std::chrono::milliseconds (1000), [&probe] () {
              return probe.failed || (probe.subscribe_payloads.size () >= 3 && probe.inflight == 0);
          });
        TEST_ASSERT_TRUE (drained);
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_INT (1, probe.max_inflight);
        TEST_ASSERT_EQUAL_UINT (3, probe.subscribe_payloads.size ());
        TEST_ASSERT_EQUAL_STRING ("payload-1", probe.subscribe_payloads[0].c_str ());
        TEST_ASSERT_EQUAL_STRING ("payload-2", probe.subscribe_payloads[1].c_str ());
        TEST_ASSERT_EQUAL_STRING ("payload-3", probe.subscribe_payloads[2].c_str ());
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_dispatch_subscription_change_applies_to_later_fanout ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *sub_spot = zlink_spot_new (node);
    void *pub_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (sub_spot);
    TEST_ASSERT_NOT_NULL (pub_spot);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (sub_spot, "dispatch.topic.initial"));

    spot_dispatch_recv_probe_t probe;
    probe.subscribe_new_filter_after_first_payload = true;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (sub_spot, &on_spot_dispatch_recv_event, &probe));

    zlink_msg_t first;
    init_string_part (&first, "initial-payload");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_publish (pub_spot, "dispatch.topic.initial", &first, 1, 0));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool changed =
          probe.cv.wait_for (lock, std::chrono::milliseconds (1000), [&probe] () {
              return probe.failed || probe.subscription_changed_in_callback;
          });
        TEST_ASSERT_TRUE (changed);
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_UINT (1, probe.subscribe_payloads.size ());
        TEST_ASSERT_EQUAL_STRING ("initial-payload", probe.subscribe_payloads[0].c_str ());
    }

    zlink_msg_t second;
    init_string_part (&second, "changed-payload");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_publish (pub_spot, "dispatch.topic.changed", &second, 1, 0));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool received =
          probe.cv.wait_for (lock, std::chrono::milliseconds (1000), [&probe] () {
              return probe.failed || probe.subscribe_payloads.size () >= 2;
          });
        TEST_ASSERT_TRUE (received);
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_UINT (2, probe.subscribe_payloads.size ());
        TEST_ASSERT_EQUAL_STRING ("changed-payload", probe.subscribe_payloads[1].c_str ());
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_dispatch_routed_recv_inside_callback ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *receiver = zlink_spot_new (node);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (receiver);
    TEST_ASSERT_NOT_NULL (router);

    set_routing_id_text (node, "node-dispatch");
    set_routing_id_text (receiver, "spot-receiver");
    set_routing_id_text (router, "router-dispatch");

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t receiver_rid = get_routing_id_value (receiver);
    const zlink_routing_id_t router_rid = get_routing_id_value (router);

    spot_dispatch_recv_probe_t probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (receiver, &on_spot_dispatch_recv_event, &probe));

    zlink_msg_t part;
    init_string_part (&part, "routed-payload");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_router_send_spot (router, &node_rid, &receiver_rid, &part, 1, 0));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool received =
          probe.cv.wait_for (lock, std::chrono::milliseconds (500), [&probe] () {
              return probe.failed || probe.routed_payloads.size () >= 1;
          });
        TEST_ASSERT_TRUE (received);
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_UINT (1, probe.routed_payloads.size ());
        TEST_ASSERT_EQUAL_STRING ("routed-payload", probe.routed_payloads[0].c_str ());
        TEST_ASSERT_EQUAL_STRING ("router-dispatch", probe.routed_source_rid.c_str ());
        TEST_ASSERT_EQUAL_STRING ("", probe.routed_spot_rid.c_str ());
        TEST_ASSERT_EQUAL_UINT64 (0, probe.routed_request_seq);
        TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE, probe.events[0]);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&receiver));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_dispatch_routed_drain_until_eagain ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *receiver = zlink_spot_new (node);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (receiver);
    TEST_ASSERT_NOT_NULL (router);

    set_routing_id_text (node, "node-routed-drain");
    set_routing_id_text (receiver, "spot-routed-drain");
    set_routing_id_text (router, "router-routed-drain");

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t receiver_rid = get_routing_id_value (receiver);

    spot_dispatch_recv_probe_t probe;
    probe.block_first_callback = true;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (receiver, &on_spot_dispatch_recv_event, &probe));

    zlink_msg_t part1;
    zlink_msg_t part2;
    zlink_msg_t part3;
    init_string_part (&part1, "routed-1");
    init_string_part (&part2, "routed-2");
    init_string_part (&part3, "routed-3");

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_router_send_spot (router, &node_rid, &receiver_rid, &part1, 1, 0));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool entered = probe.cv.wait_for (lock, std::chrono::milliseconds (500), [&probe] () {
            return probe.first_callback_entered;
        });
        TEST_ASSERT_TRUE (entered);
        TEST_ASSERT_EQUAL_INT (1, probe.max_inflight);
    }

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_router_send_spot (router, &node_rid, &receiver_rid, &part2, 1, 0));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_router_send_spot (router, &node_rid, &receiver_rid, &part3, 1, 0));

    {
        std::lock_guard<std::mutex> lock (probe.mutex);
        probe.release_first_callback = true;
    }
    probe.cv.notify_all ();

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool drained =
          probe.cv.wait_for (lock, std::chrono::milliseconds (1000), [&probe] () {
              return probe.failed || (probe.routed_payloads.size () >= 3 && probe.inflight == 0);
          });
        TEST_ASSERT_TRUE (drained);
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_INT (1, probe.max_inflight);
        TEST_ASSERT_EQUAL_UINT (3, probe.routed_payloads.size ());
        TEST_ASSERT_EQUAL_STRING ("routed-1", probe.routed_payloads[0].c_str ());
        TEST_ASSERT_EQUAL_STRING ("routed-2", probe.routed_payloads[1].c_str ());
        TEST_ASSERT_EQUAL_STRING ("routed-3", probe.routed_payloads[2].c_str ());
        TEST_ASSERT_EQUAL_STRING ("router-routed-drain", probe.routed_source_rid.c_str ());
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&receiver));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_dispatch_callbacks_are_serialized ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *spot = zlink_spot_new (node);
    void *pub_spot = zlink_spot_new (node);
    void *timer = zlink_spot_timer_new (spot);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (spot);
    TEST_ASSERT_NOT_NULL (pub_spot);
    TEST_ASSERT_NOT_NULL (timer);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_subscription (spot, "serialize.topic"));

    spot_dispatch_recv_probe_t probe;
    probe.timer = timer;
    probe.block_first_callback = true;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (spot, &on_spot_dispatch_recv_event, &probe));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_start (timer, 20 * 1000 * 1000ULL, 1));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool entered = probe.cv.wait_for (lock, std::chrono::milliseconds (500), [&probe] () {
            return probe.first_callback_entered;
        });
        TEST_ASSERT_TRUE (entered);
        TEST_ASSERT_EQUAL_INT (1, probe.max_inflight);
        TEST_ASSERT_EQUAL_INT (1, static_cast<int> (probe.callback_count));
    }

    zlink_msg_t part;
    init_string_part (&part, "serialized-payload");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_publish (pub_spot, "serialize.topic", &part, 1, 0));

    {
        std::lock_guard<std::mutex> lock (probe.mutex);
        TEST_ASSERT_EQUAL_INT (1, probe.max_inflight);
        probe.release_first_callback = true;
    }
    probe.cv.notify_all ();

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool done = probe.cv.wait_for (lock, std::chrono::milliseconds (500), [&probe] () {
            return probe.failed
                   || (probe.timer_fire_counts.size () >= 1 && probe.subscribe_payloads.size () >= 1
                       && probe.callback_count >= 2 && probe.inflight == 0);
        });
        TEST_ASSERT_TRUE (done);
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_INT (1, probe.max_inflight);
        TEST_ASSERT_EQUAL_UINT (1, probe.timer_fire_counts.size ());
        TEST_ASSERT_EQUAL_UINT (1, probe.subscribe_payloads.size ());
        TEST_ASSERT_EQUAL_STRING ("serialized-payload", probe.subscribe_payloads[0].c_str ());
        TEST_ASSERT_TRUE (probe.events.size () >= 2);
        TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE, probe.events[0]);
        TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE, probe.events[1]);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_destroy (&timer));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

}

SETUP_TEARDOWN_TESTCONTEXT

int main (void)
{
    setup_test_environment (60);

    UNITY_BEGIN ();
    RUN_TEST (test_spot_timer_dispatch_event_and_recv);
    RUN_TEST (test_spot_dispatch_subscribe_recv_inside_callback);
    RUN_TEST (test_spot_dispatch_deferred_subscribe_recv_clears_readiness_signal);
    RUN_TEST (test_spot_dispatch_subscribe_event_is_not_fanned_out_to_unrelated_spot);
    RUN_TEST (test_spot_dispatch_subscribe_drain_until_eagain);
    RUN_TEST (test_spot_dispatch_subscription_change_applies_to_later_fanout);
    RUN_TEST (test_spot_dispatch_routed_recv_inside_callback);
    RUN_TEST (test_spot_dispatch_routed_drain_until_eagain);
    RUN_TEST (test_spot_dispatch_callbacks_are_serialized);
    return UNITY_END ();
}
