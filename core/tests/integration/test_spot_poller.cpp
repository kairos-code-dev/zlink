/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <string.h>

#include <thread>
#include <chrono>
#include <string>
#include <atomic>

extern "C" int zlink_socket_request_progress_internal (void *socket_);

namespace
{
struct spot_reply_probe_t
{
    spot_reply_probe_t () : invoked (false), result (ZLINK_REQUEST_OK) {}

    bool invoked;
    zlink_request_result_t result;
    std::string payload;
};

struct spot_request_probe_t
{
    spot_request_probe_t () : invoked (false), request_seq (0)
    {
        memset (&source_node_rid, 0, sizeof (source_node_rid));
        memset (&source_spot_rid, 0, sizeof (source_spot_rid));
    }

    std::atomic<bool> invoked;
    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t source_spot_rid;
    uint64_t request_seq;
    std::string payload;
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

std::string msg_to_string (const zlink_msg_t *part_)
{
    zlink_msg_t *mutable_part = const_cast<zlink_msg_t *> (part_);
    return std::string (static_cast<const char *> (zlink_msg_data (mutable_part)),
                        zlink_msg_size (part_));
}

zlink_routing_id_t get_routing_id_value (void *handle_)
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (handle_, &rid));
    return rid;
}

void ignore_reply (zlink_request_result_t,
                   zlink_msg_t *parts_,
                   size_t part_count_,
                   void *)
{
    if (parts_ && part_count_ > 0)
        zlink_multipart_close (parts_, part_count_);
}

void capture_spot_reply (zlink_request_result_t result_,
                         zlink_msg_t *parts_,
                         size_t part_count_,
                         void *userdata_)
{
    spot_reply_probe_t *probe = static_cast<spot_reply_probe_t *> (userdata_);
    if (!probe)
        return;

    probe->invoked = true;
    probe->result = result_;
    if (parts_ && part_count_ > 0)
        probe->payload = msg_to_string (&parts_[0]);
    if (parts_ && part_count_ > 0)
        zlink_multipart_close (parts_, part_count_);
}

void capture_spot_request_for_reply (const zlink_routing_id_t *source_rid_,
                                     const zlink_routing_id_t *spot_rid_,
                                     uint64_t request_seq_,
                                     zlink_msg_t *parts_,
                                     size_t part_count_,
                                     void *userdata_)
{
    spot_request_probe_t *probe = static_cast<spot_request_probe_t *> (userdata_);
    if (!probe) {
        zlink_multipart_close (parts_, part_count_);
        return;
    }

    if (source_rid_)
        probe->source_node_rid = *source_rid_;
    if (spot_rid_)
        probe->source_spot_rid = *spot_rid_;
    probe->request_seq = request_seq_;
    if (parts_ && part_count_ > 0)
        probe->payload = msg_to_string (&parts_[0]);
    if (parts_ && part_count_ > 0)
        zlink_multipart_close (parts_, part_count_);
    probe->invoked.store (true, std::memory_order_release);
}

bool wait_for_spot_request (spot_request_probe_t *probe_, int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (probe_ && probe_->invoked.load (std::memory_order_acquire))
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    return probe_ && probe_->invoked.load (std::memory_order_acquire);
}

bool wait_for_spot_reply_via_poller (void *poller_,
                                     spot_reply_probe_t *probe_,
                                     int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (probe_ && probe_->invoked)
            return true;
        zlink_poller_event_t ev;
        const int rc = zlink_poller_wait (poller_, &ev, 1, 50, NULL);
        TEST_ASSERT_TRUE (rc >= 0);
    }
    return probe_ && probe_->invoked;
}

void test_spot_poller_wait_reports_original_spot_for_subscribe ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *sub_spot = zlink_spot_new (node);
    void *pub_spot = zlink_spot_new (node);
    void *poller = zlink_poller_new ();
    int user_tag = 11;
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (sub_spot);
    TEST_ASSERT_NOT_NULL (pub_spot);
    TEST_ASSERT_NOT_NULL (poller);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_subscription (sub_spot, "poll.spot.topic"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, sub_spot, &user_tag, ZLINK_POLLIN));

    zlink_msg_t part;
    init_string_part (&part, "poll-spot-subscribe");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_publish (pub_spot, "poll.spot.topic", &part, 1, 0));

    zlink_poller_event_t ev;
    TEST_ASSERT_EQUAL_INT (1, zlink_poller_wait (poller, &ev, 1, 1000, NULL));
    TEST_ASSERT_EQUAL_INT (ZLINK_POLLER_SOURCE_SOCKET, ev.source_kind);
    TEST_ASSERT_EQUAL_PTR (sub_spot, ev.socket);
    TEST_ASSERT_EQUAL_PTR (&user_tag, ev.user_data);
    TEST_ASSERT_EQUAL_INT (ZLINK_POLLIN, ev.events);

    zlink_routing_id_t source_rid;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[64];
    size_t topic_len = sizeof (topic);
    memset (&source_rid, 0, sizeof (source_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_subscribe (
      sub_spot, &source_rid, &parts, &part_count, topic, &topic_len,
      ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_STRING ("poll.spot.topic",
                              std::string (topic, topic_len).c_str ());
    TEST_ASSERT_EQUAL_STRING ("poll-spot-subscribe",
                              part_count > 0 ? msg_to_string (&parts[0]).c_str ()
                                             : "");
    zlink_multipart_close (parts, part_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, sub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sub_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_poller_wait_reports_original_spot_for_routed_recv ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *recv_spot = zlink_spot_new (node);
    void *router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *poller = zlink_poller_new ();
    int user_tag = 29;
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (recv_spot);
    TEST_ASSERT_NOT_NULL (router);
    TEST_ASSERT_NOT_NULL (poller);

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t recv_spot_rid = get_routing_id_value (recv_spot);
    set_routing_id_text (router, "poller-router");

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_add (poller, recv_spot, &user_tag,
                                                 ZLINK_POLLIN));

    zlink_msg_t part;
    init_string_part (&part, "poll-spot-routed");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_router_request_spot (router, &node_rid, &recv_spot_rid, &part, 1,
                                 &ignore_reply, NULL, 0, 1000));

    zlink_poller_event_t ev;
    TEST_ASSERT_EQUAL_INT (1, zlink_poller_wait (poller, &ev, 1, 1000, NULL));
    TEST_ASSERT_EQUAL_INT (ZLINK_POLLER_SOURCE_SOCKET, ev.source_kind);
    TEST_ASSERT_EQUAL_PTR (recv_spot, ev.socket);
    TEST_ASSERT_EQUAL_PTR (&user_tag, ev.user_data);
    TEST_ASSERT_EQUAL_INT (ZLINK_POLLIN, ev.events);

    const zlink_routing_id_t *source_node_rid = NULL;
    const zlink_routing_id_t *source_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (recv_spot, &source_node_rid, &source_spot_rid, &request_seq,
                       &parts, &part_count, ZLINK_DONTWAIT));
    TEST_ASSERT_NOT_NULL (source_node_rid);
    TEST_ASSERT_TRUE (source_spot_rid == NULL || source_spot_rid->size == 0);
    TEST_ASSERT_TRUE (request_seq != 0);
    TEST_ASSERT_EQUAL_STRING (
      "poll-spot-routed",
      part_count > 0 ? msg_to_string (&parts[0]).c_str () : "");
    zlink_multipart_close (parts, part_count);

    zlink_msg_t reply_part;
    init_string_part (&reply_part, "poll-spot-reply");
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_reply_router (recv_spot, source_node_rid, request_seq,
                               &reply_part, 1));
    std::this_thread::sleep_for (std::chrono::milliseconds (10));
    TEST_ASSERT_TRUE (zlink_socket_request_progress_internal (router) >= 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, recv_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&recv_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_poller_progresses_spot_request_reply_completion ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *client_spot = zlink_spot_new (node);
    void *server_spot = zlink_spot_new (node);
    void *poller = zlink_poller_new ();
    int user_tag = 41;
    spot_reply_probe_t reply_probe;
    spot_request_probe_t request_probe;
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (client_spot);
    TEST_ASSERT_NOT_NULL (server_spot);
    TEST_ASSERT_NOT_NULL (poller);

    set_routing_id_text (node, "poller-spot-node");
    set_routing_id_text (client_spot, "poller-spot-client");
    set_routing_id_text (server_spot, "poller-spot-server");

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t server_spot_rid =
      get_routing_id_value (server_spot);
    const zlink_routing_id_t *unused_source_node_rid = NULL;
    const zlink_routing_id_t *unused_source_spot_rid = NULL;
    uint64_t unused_request_seq = 0;
    zlink_msg_t *unused_parts = NULL;
    size_t unused_part_count = 0;
    TEST_ASSERT_EQUAL (ZLINK_RECV_NO_DATA,
                       zlink_spot_recv (client_spot, &unused_source_node_rid,
                                        &unused_source_spot_rid,
                                        &unused_request_seq, &unused_parts,
                                        &unused_part_count, ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL (EAGAIN, zlink_errno ());
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_add (poller, client_spot,
                                                 &user_tag, ZLINK_POLLIN));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_handler (
      server_spot, &capture_spot_request_for_reply, &request_probe));

    zlink_msg_t request_part;
    init_string_part (&request_part, "poll-spot-request");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_request_spot_part (
      client_spot, &node_rid, &server_spot_rid, &request_part,
      &capture_spot_reply, &reply_probe, ZLINK_SEND_FLAGS_NONE,
      ZLINK_PART_FINAL, 1000));

    TEST_ASSERT_TRUE (wait_for_spot_request (&request_probe, 1000));
    TEST_ASSERT_EQUAL_STRING ("poll-spot-request", request_probe.payload.c_str ());

    zlink_msg_t reply_part;
    init_string_part (&reply_part, "poll-spot-reply");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_reply_spot_part (
      server_spot, &request_probe.source_node_rid, &request_probe.source_spot_rid,
      request_probe.request_seq, &reply_part, ZLINK_PART_FINAL));

    TEST_ASSERT_TRUE (
      wait_for_spot_reply_via_poller (poller, &reply_probe, 1000));
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, reply_probe.result);
    TEST_ASSERT_EQUAL_STRING ("poll-spot-reply", reply_probe.payload.c_str ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, client_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&server_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&client_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

// Regression: when zlink_poller_wait observes a hidden completion
// drain (a reply landing on a registered spot socket) but the matching
// registration was added with user_data == NULL, the previous
// implementation looped back into wait() until the user-supplied timeout
// expired, capping throughput at 1 / poll_timeout per slot. Ensure that
// wait returns promptly so callers can act on the just-fired
// reply callback (e.g. flip waiting_reply=false and submit the next
// request) within the same iteration.
void test_spot_poller_wait_returns_promptly_after_reply ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *client_spot = zlink_spot_new (node);
    void *server_spot = zlink_spot_new (node);
    void *poller = zlink_poller_new ();
    int user_tag = 73;
    spot_reply_probe_t reply_probe;
    spot_request_probe_t request_probe;
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (client_spot);
    TEST_ASSERT_NOT_NULL (server_spot);
    TEST_ASSERT_NOT_NULL (poller);

    set_routing_id_text (node, "wakeup-spot-node");
    set_routing_id_text (client_spot, "wakeup-spot-client");
    set_routing_id_text (server_spot, "wakeup-spot-server");

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t server_spot_rid = get_routing_id_value (server_spot);

    // Prime the spot's request-reply state so that the poller wires up the
    // hidden completion signal socket on add.
    const zlink_routing_id_t *unused_source_node_rid = NULL;
    const zlink_routing_id_t *unused_source_spot_rid = NULL;
    uint64_t unused_request_seq = 0;
    zlink_msg_t *unused_parts = NULL;
    size_t unused_part_count = 0;
    TEST_ASSERT_EQUAL (ZLINK_RECV_NO_DATA,
                       zlink_spot_recv (client_spot, &unused_source_node_rid,
                                        &unused_source_spot_rid,
                                        &unused_request_seq, &unused_parts,
                                        &unused_part_count, ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL (EAGAIN, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, client_spot, &user_tag, ZLINK_POLLIN));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_handler (
      server_spot, &capture_spot_request_for_reply, &request_probe));

    zlink_msg_t request_part;
    init_string_part (&request_part, "wakeup-request");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_request_spot_part (
      client_spot, &node_rid, &server_spot_rid, &request_part,
      &capture_spot_reply, &reply_probe, ZLINK_SEND_FLAGS_NONE,
      ZLINK_PART_FINAL, 5000));

    TEST_ASSERT_TRUE (wait_for_spot_request (&request_probe, 1000));

    zlink_msg_t reply_part;
    init_string_part (&reply_part, "wakeup-reply");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_reply_spot_part (
      server_spot, &request_probe.source_node_rid,
      &request_probe.source_spot_rid, request_probe.request_seq, &reply_part,
      ZLINK_PART_FINAL));

    // Give the dispatcher a moment to enqueue the completion. The signal
    // byte should now be on the inproc PAIR pipe, making the registered
    // completion fd readable. wait must return promptly even though
    // the registration's user_data is NULL.
    std::this_thread::sleep_for (std::chrono::milliseconds (20));

    zlink_poller_event_t events[4];
    const auto wait_start = std::chrono::steady_clock::now ();
    const int rc = zlink_poller_wait (
      poller, events, static_cast<int> (sizeof (events) / sizeof (events[0])),
      5000, NULL);
    const auto wait_elapsed =
      std::chrono::steady_clock::now () - wait_start;
    const long long elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds> (wait_elapsed)
        .count ();

    TEST_ASSERT_TRUE (rc >= 0);
    // Allow generous slack for slow CI; the regression caused this call to
    // hit the full 5 s timeout. Anything under 500 ms proves the wakeup
    // reached the caller.
    TEST_ASSERT_TRUE (elapsed_ms < 500);
    TEST_ASSERT_TRUE (reply_probe.invoked);
    TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, reply_probe.result);
    TEST_ASSERT_EQUAL_STRING ("wakeup-reply", reply_probe.payload.c_str ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, client_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&server_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&client_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

// Regression: spot->spot routed send must wake a poller that waits with a
// large timeout. Reproduces the SPOT_SENDSEND throughput cap observed when
// the perf client's poller wait timeout was raised to -1 / 100 ms — the
// client hangs because the receiving spot's routed_recv signal does not
// fire on the poller's hidden completion / fd registration.
void test_spot_poller_wait_returns_promptly_after_spot_to_spot_send ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *sender_spot = zlink_spot_new (node);
    void *receiver_spot = zlink_spot_new (node);
    void *poller = zlink_poller_new ();
    int user_tag = 91;
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (sender_spot);
    TEST_ASSERT_NOT_NULL (receiver_spot);
    TEST_ASSERT_NOT_NULL (poller);

    set_routing_id_text (node, "sendsend-spot-node");
    set_routing_id_text (sender_spot, "sendsend-spot-sender");
    set_routing_id_text (receiver_spot, "sendsend-spot-receiver");

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t receiver_spot_rid =
      get_routing_id_value (receiver_spot);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, receiver_spot, &user_tag, ZLINK_POLLIN));

    zlink_msg_t part;
    init_string_part (&part, "sendsend-payload");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_send_spot_part (
      sender_spot, &node_rid, &receiver_spot_rid, &part,
      ZLINK_SEND_FLAGS_NONE, ZLINK_PART_FINAL));

    // Allow the dispatcher to enqueue the routed message. The receiver's
    // routed_recv_queue should now be readable.
    std::this_thread::sleep_for (std::chrono::milliseconds (20));

    zlink_poller_event_t events[4];
    const auto wait_start = std::chrono::steady_clock::now ();
    const int rc = zlink_poller_wait (
      poller, events, static_cast<int> (sizeof (events) / sizeof (events[0])),
      5000, NULL);
    const auto wait_elapsed =
      std::chrono::steady_clock::now () - wait_start;
    const long long elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds> (wait_elapsed)
        .count ();

    TEST_ASSERT_TRUE (rc >= 1);
    TEST_ASSERT_TRUE (elapsed_ms < 500);
    TEST_ASSERT_EQUAL_PTR (receiver_spot, events[0].socket);

    const zlink_routing_id_t *src_node_rid = NULL;
    const zlink_routing_id_t *src_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (receiver_spot, &src_node_rid, &src_spot_rid,
                       &request_seq, &parts, &part_count, ZLINK_DONTWAIT));
    TEST_ASSERT_EQUAL_STRING (
      "sendsend-payload",
      part_count > 0 ? msg_to_string (&parts[0]).c_str () : "");
    zlink_multipart_close (parts, part_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, receiver_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&receiver_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sender_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

// Regression: a spot socket must be addable to a poller with
// (ZLINK_POLLIN | ZLINK_POLLOUT) so that bi-directional workloads
// (e.g. SPOT_SENDSEND) can wait on either send-readiness (backpressure
// recovery) or recv-readiness (incoming routed message) in one call.
// Previously zlink_service_poller_add_internal validated POLLIN xor
// POLLOUT only and rejected the combined mask with EINVAL, forcing
// callers to use a short polling fallback (1ms timeout) instead of an
// indefinite signal-driven wait.
void test_spot_poller_accepts_pollin_or_pollout_combined ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *sender_spot = zlink_spot_new (node);
    void *receiver_spot = zlink_spot_new (node);
    void *poller = zlink_poller_new ();
    int user_tag = 92;
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (sender_spot);
    TEST_ASSERT_NOT_NULL (receiver_spot);
    TEST_ASSERT_NOT_NULL (poller);

    set_routing_id_text (node, "ssbi-spot-node");
    set_routing_id_text (sender_spot, "ssbi-spot-sender");
    set_routing_id_text (receiver_spot, "ssbi-spot-receiver");

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t receiver_spot_rid =
      get_routing_id_value (receiver_spot);

    // The combined POLLIN|POLLOUT registration must succeed. Previously
    // failed with EINVAL.
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_add (
      poller, receiver_spot, &user_tag,
      static_cast<short> (ZLINK_POLLIN | ZLINK_POLLOUT)));

    // POLLOUT half: the spot is initially writable, so a wait with a
    // generous timeout must return immediately with a POLLOUT event.
    {
        zlink_poller_event_t events[4];
        const auto wait_start = std::chrono::steady_clock::now ();
        const int rc = zlink_poller_wait (
          poller, events,
          static_cast<int> (sizeof (events) / sizeof (events[0])), 5000, NULL);
        const long long elapsed_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (
            std::chrono::steady_clock::now () - wait_start)
            .count ();
        TEST_ASSERT_TRUE (rc >= 1);
        TEST_ASSERT_TRUE (elapsed_ms < 500);
        bool saw_pollout = false;
        for (int i = 0; i < rc; ++i) {
            if (events[i].socket == receiver_spot
                && (events[i].events & ZLINK_POLLOUT) != 0) {
                saw_pollout = true;
                break;
            }
        }
        TEST_ASSERT_TRUE (saw_pollout);
    }

    // POLLIN half: a routed send from the peer spot must wake wait
    // promptly and surface a POLLIN event for the receiver_spot.
    zlink_msg_t part;
    init_string_part (&part, "ssbi-payload");
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_send_spot_part (
      sender_spot, &node_rid, &receiver_spot_rid, &part,
      ZLINK_SEND_FLAGS_NONE, ZLINK_PART_FINAL));
    std::this_thread::sleep_for (std::chrono::milliseconds (20));

    {
        zlink_poller_event_t events[4];
        const auto wait_start = std::chrono::steady_clock::now ();
        const int rc = zlink_poller_wait (
          poller, events,
          static_cast<int> (sizeof (events) / sizeof (events[0])), 5000, NULL);
        const long long elapsed_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (
            std::chrono::steady_clock::now () - wait_start)
            .count ();
        TEST_ASSERT_TRUE (rc >= 1);
        TEST_ASSERT_TRUE (elapsed_ms < 500);
        bool saw_pollin = false;
        for (int i = 0; i < rc; ++i) {
            if (events[i].socket == receiver_spot
                && (events[i].events & ZLINK_POLLIN) != 0) {
                saw_pollin = true;
                break;
            }
        }
        TEST_ASSERT_TRUE (saw_pollin);
    }

    const zlink_routing_id_t *src_node_rid = NULL;
    const zlink_routing_id_t *src_spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_recv (receiver_spot, &src_node_rid, &src_spot_rid,
                       &request_seq, &parts, &part_count, ZLINK_DONTWAIT));
    zlink_multipart_close (parts, part_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, receiver_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&receiver_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&sender_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

// Regression: sustained spot request/reply round trips must let
// zlink_poller_wait return after every reply, not just the first. The
// single-event variant previously looped back into wait() after firing
// the hidden completion drain (no public event to surface), capping
// callback-driven throughput at 1 / poll_timeout per slot. The fix
// uses the same hidden completion path and returns 0 once the drain fires
// any user callback so the caller can act on the just-received reply
// and submit follow-up work.
void test_spot_poller_wait_returns_for_each_reply_in_sustained_request_loop ()
{
    void *ctx = zlink_ctx_new ();
    void *node = zlink_spot_node_new (ctx, NULL);
    void *client_spot = zlink_spot_new (node);
    void *server_spot = zlink_spot_new (node);
    void *poller = zlink_poller_new ();
    int user_tag = 73;
    TEST_ASSERT_NOT_NULL (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_NOT_NULL (client_spot);
    TEST_ASSERT_NOT_NULL (server_spot);
    TEST_ASSERT_NOT_NULL (poller);

    set_routing_id_text (node, "sustained-reqrep-node");
    set_routing_id_text (client_spot, "sustained-reqrep-client");
    set_routing_id_text (server_spot, "sustained-reqrep-server");

    const zlink_routing_id_t node_rid = get_routing_id_value (node);
    const zlink_routing_id_t server_spot_rid =
      get_routing_id_value (server_spot);

    // Prime the client spot's request-reply state so poller_add wires up
    // the completion signal fd registration.
    {
        const zlink_routing_id_t *prime_node_rid = NULL;
        const zlink_routing_id_t *prime_spot_rid = NULL;
        uint64_t prime_seq = 0;
        zlink_msg_t *prime_parts = NULL;
        size_t prime_part_count = 0;
        TEST_ASSERT_EQUAL (
          ZLINK_RECV_NO_DATA,
          zlink_spot_recv (client_spot, &prime_node_rid, &prime_spot_rid,
                           &prime_seq, &prime_parts, &prime_part_count,
                           ZLINK_RECV_FLAGS_DONTWAIT));
    }

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add (poller, client_spot, &user_tag, ZLINK_POLLIN));
    spot_request_probe_t request_probe;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_handler (
      server_spot, &capture_spot_request_for_reply, &request_probe));

    const int kRoundTrips = 16;
    for (int i = 0; i < kRoundTrips; ++i) {
        spot_reply_probe_t reply_probe;
        request_probe.invoked.store (false, std::memory_order_release);

        zlink_msg_t request_part;
        init_string_part (&request_part, "sustained-request");
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_request_spot_part (
          client_spot, &node_rid, &server_spot_rid, &request_part,
          &capture_spot_reply, &reply_probe, ZLINK_SEND_FLAGS_NONE,
          ZLINK_PART_FINAL, 5000));

        TEST_ASSERT_TRUE (wait_for_spot_request (&request_probe, 1000));

        zlink_msg_t reply_part;
        init_string_part (&reply_part, "sustained-reply");
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_reply_spot_part (
          server_spot, &request_probe.source_node_rid,
          &request_probe.source_spot_rid, request_probe.request_seq,
          &reply_part, ZLINK_PART_FINAL));

        zlink_poller_event_t event;
        const auto wait_start = std::chrono::steady_clock::now ();
        const int rc =
          zlink_poller_wait (poller, &event, 1, 5000, NULL);
        const auto wait_elapsed =
          std::chrono::steady_clock::now () - wait_start;
        const long long elapsed_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (wait_elapsed)
            .count ();

        TEST_ASSERT_TRUE (rc >= 0);
        // Wakeup should arrive well within 500 ms; under the bug the
        // wait stalls on iterations 2+ until the 5000 ms outer timeout.
        TEST_ASSERT_TRUE (elapsed_ms < 500);
        TEST_ASSERT_TRUE (reply_probe.invoked);
        TEST_ASSERT_EQUAL (ZLINK_REQUEST_OK, reply_probe.result);
        TEST_ASSERT_EQUAL_STRING ("sustained-reply",
                                  reply_probe.payload.c_str ());
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_remove (poller, client_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&server_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&client_spot));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
}

SETUP_TEARDOWN_TESTCONTEXT

int main (void)
{
    setup_test_environment (60);

    UNITY_BEGIN ();
    RUN_TEST (test_spot_poller_wait_reports_original_spot_for_subscribe);
    RUN_TEST (test_spot_poller_wait_reports_original_spot_for_routed_recv);
    RUN_TEST (test_spot_poller_progresses_spot_request_reply_completion);
    RUN_TEST (test_spot_poller_wait_returns_promptly_after_reply);
    RUN_TEST (test_spot_poller_wait_returns_promptly_after_spot_to_spot_send);
    RUN_TEST (test_spot_poller_accepts_pollin_or_pollout_combined);
    RUN_TEST (
      test_spot_poller_wait_returns_for_each_reply_in_sustained_request_loop);
    return UNITY_END ();
}
