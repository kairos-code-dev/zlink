/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"

#include "../../src/runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.hpp"
#include "../../src/runtime/services/actor/service_spot_actor_internal.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <string.h>
#include <vector>

namespace
{
zlink_routing_id_t make_rid (const char *value_)
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    rid.size = static_cast<uint8_t> (strlen (value_));
    memcpy (rid.data, value_, rid.size);
    return rid;
}

bool same_rid (const zlink_routing_id_t &lhs_, const zlink_routing_id_t &rhs_)
{
    return lhs_.size == rhs_.size && memcmp (lhs_.data, rhs_.data, lhs_.size) == 0;
}

void init_part (zlink_msg_t *part_, const char *text_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (part_, strlen (text_)));
    memcpy (zlink_msg_data (part_), text_, strlen (text_));
}

std::string part_to_string (zlink_msg_t *part_)
{
    return std::string (static_cast<const char *> (zlink_msg_data (part_)),
                        static_cast<const char *> (zlink_msg_data (part_)) + zlink_msg_size (part_));
}

struct callback_probe_t
{
    callback_probe_t () : calls (0), result (ZLINK_REQUEST_INTERNAL_ERROR), part_count (0) {}

    std::mutex mutex;
    std::condition_variable cv;
    int calls;
    zlink_request_result_t result;
    size_t part_count;
    std::string payload;
};

void on_reply (zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, void *ud_)
{
    callback_probe_t *probe = static_cast<callback_probe_t *> (ud_);
    std::lock_guard<std::mutex> lock (probe->mutex);
    ++probe->calls;
    probe->result = result_;
    probe->part_count = part_count_;
    probe->payload = part_count_ > 0 ? part_to_string (&parts_[0]) : std::string ();
    probe->cv.notify_all ();
}

bool wait_callback_count (callback_probe_t *probe_, int count_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (lock, std::chrono::milliseconds (500),
                                [probe_, count_] () { return probe_->calls >= count_; });
}

int callback_count (callback_probe_t *probe_)
{
    std::lock_guard<std::mutex> lock (probe_->mutex);
    return probe_->calls;
}

struct actor_recv_probe_t
{
    actor_recv_probe_t () : calls (0) { memset (&info, 0, sizeof (info)); }

    std::mutex mutex;
    std::condition_variable cv;
    int calls;
    zlink_actor_ref_t actor;
    zlink_actor_recv_info_t info;
    std::string payload;
    std::vector<std::string> payloads;
};

struct actor_dispatch_context_t
{
    void *node;
    actor_recv_probe_t *probe;
};

void on_actor_dispatch_with_node (void *spot_,
                                  const zlink_spot_dispatch_info_t *info_,
                                  void *ud_)
{
    actor_dispatch_context_t *ctx = static_cast<actor_dispatch_context_t *> (ud_);
    if (!ctx || !ctx->probe || !info_
        || info_->event != ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE)
        return;

    while (true) {
        zlink_msg_t part;
        zlink_msg_init (&part);
        zlink_part_flag_t more = ZLINK_PART_FINAL;
        zlink_actor_recv_info_t recv_info;
        memset (&recv_info, 0, sizeof (recv_info));
        const zlink_recv_result_t rc =
          zlink_spot_node_actor_recv_part (ctx->node, &ctx->probe->actor, &recv_info, &part, &more,
                                           ZLINK_RECV_FLAGS_DONTWAIT);
        if (rc != ZLINK_RECV_OK) {
            zlink_msg_close (&part);
            break;
        }
        {
            std::lock_guard<std::mutex> lock (ctx->probe->mutex);
            ++ctx->probe->calls;
            ctx->probe->info = recv_info;
            ctx->probe->payload = part_to_string (&part);
            ctx->probe->payloads.push_back (ctx->probe->payload);
            ctx->probe->cv.notify_all ();
        }
        zlink_msg_close (&part);
        if (more == ZLINK_PART_FINAL)
            break;
    }
    LIBZLINK_UNUSED (spot_);
}

bool wait_actor_recv_count (actor_recv_probe_t *probe_, int count_)
{
    std::unique_lock<std::mutex> lock (probe_->mutex);
    return probe_->cv.wait_for (lock, std::chrono::milliseconds (500),
                                [probe_, count_] () { return probe_->calls >= count_; });
}

void make_node_with_actor (void **ctx_out_,
                           void **node_out_,
                           void **entry_spot_out_,
                           zlink_actor_ref_t *actor_out_)
{
    static int next_node_id = 1;
    *ctx_out_ = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (*ctx_out_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_set (*ctx_out_, ZLINK_MAX_SOCKETS, 128));
    *node_out_ = zlink_spot_node_new (*ctx_out_, NULL);
    TEST_ASSERT_NOT_NULL (*node_out_);
    char node_id[64];
    snprintf (node_id, sizeof (node_id), "no-bind-node-%d", next_node_id++);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_routing_id (*node_out_, node_id, strlen (node_id)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_entry_spot (*node_out_, entry_spot_out_));
    TEST_ASSERT_NOT_NULL (*entry_spot_out_);
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_OK,
                           zlink_spot_node_actor_new (*node_out_, "actor-a", actor_out_));
}

void destroy_node (void **ctx_, void **node_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (node_));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (*ctx_));
    *ctx_ = NULL;
}

void inject_no_bind_reply (void *node_,
                           const zlink_routing_id_t &source_node_rid_,
                           const zlink_routing_id_t &caller_endpoint_rid_,
                           const zlink_actor_ref_t &actor_,
                           uint64_t request_id_,
                           zlink_request_result_t result_,
                           const char *payload_)
{
    zlink_msg_t control;
    TEST_ASSERT_TRUE (zlink::spot_actor_gateway::init_control_msg (
      zlink::spot_actor_gateway::packet_actor_to_server_no_bind_reply, caller_endpoint_rid_,
      actor_.actor_id, actor_.generation, ZLINK_PART_FINAL, &control, request_id_,
      static_cast<int32_t> (result_)));
    zlink_msg_t payload;
    zlink_msg_init (&payload);
    zlink_msg_t *parts = &control;
    size_t part_count = 1;
    zlink_msg_t two_parts[2];
    if (payload_) {
        init_part (&payload, payload_);
        zlink_msg_init (&two_parts[0]);
        zlink_msg_init (&two_parts[1]);
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_move (&two_parts[0], &control));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_move (&two_parts[1], &payload));
        parts = two_parts;
        part_count = 2;
    }
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::spot_actor_internal::process_gateway_delivery (node_, &source_node_rid_, parts,
                                                            part_count));
    for (size_t i = 0; i < part_count; ++i)
        zlink_msg_close (&parts[i]);
}

void test_request_reply_correlation_ignores_wrong_id_and_matches_mixed_replies ()
{
    void *ctx = NULL;
    void *node = NULL;
    void *entry = NULL;
    zlink_actor_ref_t actor;
    make_node_with_actor (&ctx, &node, &entry, &actor);

    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (node, &node_rid));

    callback_probe_t first;
    callback_probe_t second;
    zlink_msg_t first_msg;
    zlink_msg_t second_msg;
    init_part (&first_msg, "request-one");
    init_part (&second_msg, "request-two");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                             zlink_spot_node_request_to_actor (
                             node, &actor, &first_msg, 1, &on_reply, &first, ZLINK_DONTWAIT, 5000));
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                             zlink_spot_node_request_to_actor (
                             node, &actor, &second_msg, 1, &on_reply, &second, ZLINK_DONTWAIT,
                             5000));

    inject_no_bind_reply (node, node_rid, node_rid, actor, 999, ZLINK_REQUEST_OK, "wrong");
    TEST_ASSERT_EQUAL_INT (0, callback_count (&first));
    TEST_ASSERT_EQUAL_INT (0, callback_count (&second));

    inject_no_bind_reply (node, node_rid, node_rid, actor, 2, ZLINK_REQUEST_OK, "reply-two");
    TEST_ASSERT_TRUE (wait_callback_count (&second, 1));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, second.result);
    TEST_ASSERT_EQUAL_STRING ("reply-two", second.payload.c_str ());
    TEST_ASSERT_EQUAL_INT (0, callback_count (&first));

    inject_no_bind_reply (node, node_rid, node_rid, actor, 1, ZLINK_REQUEST_OK, "reply-one");
    TEST_ASSERT_TRUE (wait_callback_count (&first, 1));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, first.result);
    TEST_ASSERT_EQUAL_STRING ("reply-one", first.payload.c_str ());

    destroy_node (&ctx, &node);
}

void test_no_bind_delivery_ack_and_mailbox_source_do_not_bind_session ()
{
    void *ctx = NULL;
    void *node = NULL;
    void *entry = NULL;
    zlink_actor_ref_t actor;
    make_node_with_actor (&ctx, &node, &entry, &actor);

    actor_recv_probe_t recv_probe;
    recv_probe.actor = actor;
    actor_dispatch_context_t dispatch_ctx;
    dispatch_ctx.node = node;
    dispatch_ctx.probe = &recv_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (entry, &on_actor_dispatch_with_node, &dispatch_ctx));

    callback_probe_t ack;
    zlink_msg_t msg;
    init_part (&msg, "no-bind");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_spot_node_send_to_actor (
                             node, &actor, &msg, 1, &on_reply, &ack, ZLINK_DONTWAIT, 1000));
    TEST_ASSERT_TRUE (wait_callback_count (&ack, 1));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, ack.result);
    TEST_ASSERT_EQUAL_UINT (0, ack.part_count);

    TEST_ASSERT_TRUE (wait_actor_recv_count (&recv_probe, 1));
    TEST_ASSERT_EQUAL_STRING ("no-bind", recv_probe.payload.c_str ());
    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (node, &node_rid));
    TEST_ASSERT_TRUE (same_rid (node_rid, recv_probe.info.source_node_rid));
    TEST_ASSERT_TRUE (same_rid (node_rid, recv_probe.info.source_session_rid));

    callback_probe_t multipart_ack;
    zlink_msg_t multipart[2];
    init_part (&multipart[0], "header");
    init_part (&multipart[1], "payload");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_spot_node_send_to_actor (
                             node, &actor, multipart, 2, &on_reply, &multipart_ack, ZLINK_DONTWAIT,
                             1000));
    TEST_ASSERT_TRUE (wait_callback_count (&multipart_ack, 1));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_OK, multipart_ack.result);
    TEST_ASSERT_EQUAL_UINT (0, multipart_ack.part_count);

    TEST_ASSERT_TRUE (wait_actor_recv_count (&recv_probe, 3));
    TEST_ASSERT_EQUAL_STRING ("header", recv_probe.payloads[1].c_str ());
    TEST_ASSERT_EQUAL_STRING ("payload", recv_probe.payloads[2].c_str ());
    TEST_ASSERT_TRUE (same_rid (node_rid, recv_probe.info.source_node_rid));
    TEST_ASSERT_TRUE (same_rid (node_rid, recv_probe.info.source_session_rid));

    destroy_node (&ctx, &node);
}

void test_actor_missing_and_stale_return_distinct_native_results ()
{
    void *ctx = NULL;
    void *node = NULL;
    void *entry = NULL;
    zlink_actor_ref_t actor;
    make_node_with_actor (&ctx, &node, &entry, &actor);

    callback_probe_t missing;
    zlink_actor_ref_t missing_actor = actor;
    strncpy (missing_actor.actor_id, "missing-actor", ZLINK_ACTOR_ID_MAX - 1);
    zlink_msg_t missing_msg;
    init_part (&missing_msg, "missing");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_spot_node_send_to_actor (
                             node, &missing_actor, &missing_msg, 1, &on_reply, &missing,
                             ZLINK_DONTWAIT, 1000));
    TEST_ASSERT_TRUE (wait_callback_count (&missing, 1));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_NOT_FOUND, missing.result);

    callback_probe_t stale;
    zlink_actor_ref_t stale_actor = actor;
    stale_actor.generation += 1;
    zlink_msg_t stale_msg;
    init_part (&stale_msg, "stale");
    TEST_ASSERT_EQUAL_INT (ZLINK_SUBMIT_OK,
                           zlink_spot_node_send_to_actor (
                             node, &stale_actor, &stale_msg, 1, &on_reply, &stale, ZLINK_DONTWAIT,
                             1000));
    TEST_ASSERT_TRUE (wait_callback_count (&stale, 1));
    TEST_ASSERT_EQUAL_INT (ZLINK_REQUEST_CONFLICT, stale.result);

    destroy_node (&ctx, &node);
}

void test_route_not_connected_fails_at_submit ()
{
    void *ctx = NULL;
    void *node = NULL;
    void *entry = NULL;
    zlink_actor_ref_t actor;
    make_node_with_actor (&ctx, &node, &entry, &actor);

    zlink_actor_ref_t remote = actor;
    remote.node_rid = make_rid ("unknown-node");
    callback_probe_t callback;
    zlink_msg_t msg;
    init_part (&msg, "route-missing");
    const zlink_submit_result_t rc = zlink_spot_node_send_to_actor (
      node, &remote, &msg, 1, &on_reply, &callback, ZLINK_DONTWAIT, 100);
    TEST_ASSERT_NOT_EQUAL (ZLINK_SUBMIT_OK, rc);
    TEST_ASSERT_FALSE (wait_callback_count (&callback, 1));
    zlink_msg_close (&msg);

    destroy_node (&ctx, &node);
}

void test_existing_session_gateway_still_delivers_and_sets_session_source ()
{
    void *ctx = NULL;
    void *node = NULL;
    void *entry = NULL;
    zlink_actor_ref_t actor;
    make_node_with_actor (&ctx, &node, &entry, &actor);

    actor_recv_probe_t recv_probe;
    recv_probe.actor = actor;
    actor_dispatch_context_t dispatch_ctx;
    dispatch_ctx.node = node;
    dispatch_ctx.probe = &recv_probe;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_dispatch_event_handler (entry, &on_actor_dispatch_with_node, &dispatch_ctx));

    zlink_routing_id_t source_node = make_rid ("session-node");
    zlink_routing_id_t session = make_rid ("session-rid");
    zlink_msg_t control;
    TEST_ASSERT_TRUE (zlink::spot_actor_gateway::init_control_msg (
      zlink::spot_actor_gateway::packet_session_to_actor, session, actor.actor_id,
      actor.generation, ZLINK_PART_FINAL, &control));
    zlink_msg_t payload;
    init_part (&payload, "session-path");
    zlink_msg_t parts[2];
    zlink_msg_init (&parts[0]);
    zlink_msg_init (&parts[1]);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_move (&parts[0], &control));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_move (&parts[1], &payload));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::spot_actor_internal::process_gateway_delivery (node, &source_node, parts, 2));
    TEST_ASSERT_TRUE (wait_actor_recv_count (&recv_probe, 1));
    TEST_ASSERT_EQUAL_STRING ("session-path", recv_probe.payload.c_str ());
    TEST_ASSERT_TRUE (same_rid (source_node, recv_probe.info.source_node_rid));
    TEST_ASSERT_TRUE (same_rid (session, recv_probe.info.source_session_rid));
    zlink_msg_close (&parts[0]);
    zlink_msg_close (&parts[1]);

    destroy_node (&ctx, &node);
}
}

int main ()
{
    UNITY_BEGIN ();
    RUN_TEST (test_request_reply_correlation_ignores_wrong_id_and_matches_mixed_replies);
    RUN_TEST (test_no_bind_delivery_ack_and_mailbox_source_do_not_bind_session);
    RUN_TEST (test_actor_missing_and_stale_return_distinct_native_results);
    RUN_TEST (test_route_not_connected_fails_at_submit);
    RUN_TEST (test_existing_session_gateway_still_delivers_and_sets_session_source);
    return UNITY_END ();
}
