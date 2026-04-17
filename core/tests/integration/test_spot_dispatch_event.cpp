/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <string.h>
#include <vector>

namespace
{
struct spot_dispatch_probe_t
{
    spot_dispatch_probe_t () : event (0), called (false) {}

    std::mutex mutex;
    std::condition_variable cv;
    int event;
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
        failed (false),
        last_errno (0),
        routed_request_seq (0)
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
    bool failed;
    int last_errno;
    std::vector<int> events;
    std::vector<std::string> subscribe_topics;
    std::vector<std::string> subscribe_payloads;
    std::vector<std::string> timer_fire_counts;
    std::vector<std::string> routed_payloads;
    std::string routed_source_rid;
    std::string routed_spot_rid;
    uint64_t routed_request_seq;
};

void init_string_part (zlink_msg_t *part_, const char *text_)
{
    const size_t size = strlen (text_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (part_, size));
    memcpy (zlink_msg_data (part_), text_, size);
}

void set_routing_id_text (void *handle_, const char *text_)
{
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (handle_, text_, strlen (text_)));
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

void on_spot_dispatch_event (void *,
                             zlink_spot_dispatch_event_t event_,
                             void *userdata_)
{
    spot_dispatch_probe_t *probe =
      static_cast<spot_dispatch_probe_t *> (userdata_);
    std::lock_guard<std::mutex> lock (probe->mutex);
    probe->event = static_cast<int> (event_);
    probe->called = true;
    probe->cv.notify_all ();
}

void on_spot_dispatch_recv_event (void *spot_,
                                  zlink_spot_dispatch_event_t event_,
                                  void *userdata_)
{
    spot_dispatch_recv_probe_t *probe =
      static_cast<spot_dispatch_recv_probe_t *> (userdata_);
    if (!probe)
        return;

    {
        std::unique_lock<std::mutex> lock (probe->mutex);
        ++probe->callback_count;
        ++probe->inflight;
        if (probe->inflight > probe->max_inflight)
            probe->max_inflight = probe->inflight;
        probe->events.push_back (static_cast<int> (event_));
        if (probe->block_first_callback && !probe->first_callback_entered) {
            probe->first_callback_entered = true;
            probe->cv.notify_all ();
            while (!probe->release_first_callback)
                probe->cv.wait (lock);
        }
    }

    if (event_ == ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE) {
        for (;;) {
            zlink_routing_id_t source_rid;
            zlink_msg_t *parts = NULL;
            size_t part_count = 0;
            char topic[64];
            size_t topic_len = sizeof (topic);
            memset (&source_rid, 0, sizeof (source_rid));

            const zlink_recv_result_t rc =
              zlink_subscribe (spot_, &source_rid, &parts, &part_count, topic,
                               &topic_len, ZLINK_DONTWAIT);
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
            probe->subscribe_payloads.push_back (
              part_count > 0 ? msg_to_string (&parts[0]) : std::string ());
            zlink_multipart_close (parts, part_count);
        }
    } else if (event_ == ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE) {
        for (;;) {
            const zlink_routing_id_t *source_rid = NULL;
            const zlink_routing_id_t *spot_rid = NULL;
            uint64_t request_seq = 0;
            zlink_msg_t *parts = NULL;
            size_t part_count = 0;

            const zlink_recv_result_t rc =
              zlink_spot_recv (spot_, &source_rid, &spot_rid, &request_seq,
                               &parts, &part_count, ZLINK_DONTWAIT);
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
            probe->routed_source_rid.assign (
              reinterpret_cast<const char *> (source_rid->data),
              source_rid->size);
            probe->routed_spot_rid.assign (
              spot_rid ? reinterpret_cast<const char *> (spot_rid->data) : "",
              spot_rid ? spot_rid->size : 0);
            probe->routed_payloads.push_back (
              part_count > 0 ? msg_to_string (&parts[0]) : std::string ());
            zlink_multipart_close (parts, part_count);
        }
    } else if (event_ == ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE
               && probe->timer != NULL) {
        for (;;) {
            uint64_t fire_count = 0;
            const zlink_recv_result_t rc = zlink_timer_recv (probe->timer,
                                                             &fire_count);
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
    }

    {
        std::lock_guard<std::mutex> lock (probe->mutex);
        --probe->inflight;
    }
    probe->cv.notify_all ();
}

void test_spot_timer_dispatch_event_and_recv ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx);
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
        const bool fired = probe.cv.wait_for (
          lock, std::chrono::milliseconds (500),
          [&probe]() { return probe.called; });
        TEST_ASSERT_TRUE (fired);
        TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE,
                               probe.event);
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
    void *node = zlink_spot_node_new (ctx);
    void *sub_spot = zlink_spot_new (node);
    void *pub_spot = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (sub_spot);
    TEST_ASSERT_NOT_NULL (pub_spot);

    set_routing_id_text (pub_spot, "pub-spot");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub_spot, "dispatch.topic"));

    spot_dispatch_recv_probe_t probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_dispatch_event_handler (
      sub_spot, &on_spot_dispatch_recv_event, &probe));

    zlink_msg_t part;
    init_string_part (&part, "topic-payload");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_publish (pub_spot, "dispatch.topic", &part, 1, 0));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool received = probe.cv.wait_for (
          lock, std::chrono::milliseconds (500), [&probe]() {
              return probe.failed || probe.subscribe_payloads.size () >= 1;
          });
        TEST_ASSERT_TRUE (received);
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_UINT (1, probe.subscribe_payloads.size ());
        TEST_ASSERT_EQUAL_STRING ("dispatch.topic",
                                  probe.subscribe_topics[0].c_str ());
        TEST_ASSERT_EQUAL_STRING ("topic-payload",
                                  probe.subscribe_payloads[0].c_str ());
        TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE,
                               probe.events[0]);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_dispatch_routed_recv_inside_callback ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx);
    void *sender = zlink_spot_new (node);
    void *receiver = zlink_spot_new (node);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (sender);
    TEST_ASSERT_NOT_NULL (receiver);

    set_routing_id_text (node, "node-dispatch");
    set_routing_id_text (sender, "spot-sender");
    set_routing_id_text (receiver, "spot-receiver");

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t receiver_rid = get_routing_id_value (receiver);

    spot_dispatch_recv_probe_t probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_dispatch_event_handler (
      receiver, &on_spot_dispatch_recv_event, &probe));

    zlink_msg_t part;
    init_string_part (&part, "routed-payload");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_send_spot (
      sender, &node_rid, &receiver_rid, &part, 1, 0));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool received = probe.cv.wait_for (
          lock, std::chrono::milliseconds (500), [&probe]() {
              return probe.failed || probe.routed_payloads.size () >= 1;
          });
        TEST_ASSERT_TRUE (received);
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_UINT (1, probe.routed_payloads.size ());
        TEST_ASSERT_EQUAL_STRING ("routed-payload",
                                  probe.routed_payloads[0].c_str ());
        TEST_ASSERT_EQUAL_STRING ("node-dispatch",
                                  probe.routed_source_rid.c_str ());
        TEST_ASSERT_EQUAL_STRING ("spot-sender",
                                  probe.routed_spot_rid.c_str ());
        TEST_ASSERT_EQUAL_UINT64 (0, probe.routed_request_seq);
        TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE,
                               probe.events[0]);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&receiver));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sender));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_dispatch_callbacks_are_serialized ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx);
    void *spot = zlink_spot_new (node);
    void *pub_spot = zlink_spot_new (node);
    void *timer = zlink_spot_timer_new (spot);
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (spot);
    TEST_ASSERT_NOT_NULL (pub_spot);
    TEST_ASSERT_NOT_NULL (timer);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (spot, "serialize.topic"));

    spot_dispatch_recv_probe_t probe;
    probe.timer = timer;
    probe.block_first_callback = true;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (spot, &on_spot_dispatch_recv_event,
                                         &probe));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_timer_start (timer, 20 * 1000 * 1000ULL, 1));

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool entered = probe.cv.wait_for (
          lock, std::chrono::milliseconds (500),
          [&probe]() { return probe.first_callback_entered; });
        TEST_ASSERT_TRUE (entered);
        TEST_ASSERT_EQUAL_INT (1, probe.max_inflight);
        TEST_ASSERT_EQUAL_INT (1, static_cast<int> (probe.callback_count));
    }

    zlink_msg_t part;
    init_string_part (&part, "serialized-payload");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_publish (pub_spot, "serialize.topic", &part, 1, 0));

    {
        std::lock_guard<std::mutex> lock (probe.mutex);
        TEST_ASSERT_EQUAL_INT (1, probe.max_inflight);
        probe.release_first_callback = true;
    }
    probe.cv.notify_all ();

    {
        std::unique_lock<std::mutex> lock (probe.mutex);
        const bool done = probe.cv.wait_for (
          lock, std::chrono::milliseconds (500), [&probe]() {
              return probe.failed
                     || (probe.timer_fire_counts.size () >= 1
                         && probe.subscribe_payloads.size () >= 1
                         && probe.callback_count >= 2 && probe.inflight == 0);
          });
        TEST_ASSERT_TRUE (done);
        TEST_ASSERT_FALSE (probe.failed);
        TEST_ASSERT_EQUAL_INT (1, probe.max_inflight);
        TEST_ASSERT_EQUAL_UINT (1, probe.timer_fire_counts.size ());
        TEST_ASSERT_EQUAL_UINT (1, probe.subscribe_payloads.size ());
        TEST_ASSERT_EQUAL_STRING ("serialized-payload",
                                  probe.subscribe_payloads[0].c_str ());
        TEST_ASSERT_TRUE (probe.events.size () >= 2);
        TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE,
                               probe.events[0]);
        TEST_ASSERT_EQUAL_INT (ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE,
                               probe.events[1]);
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
    return 0;
}
