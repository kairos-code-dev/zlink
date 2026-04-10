/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"

#include "core/ctx.hpp"
#include "core/multipart_send_txn.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_mesh_pub_budget.hpp"
#include "services/spot/spot_runtime.hpp"
#include "sockets/socket_base.hpp"

#include <chrono>
#include <future>
#include <thread>
#include <vector>

namespace
{
void set_zero_linger (zlink::socket_base_t *socket_)
{
    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      socket_->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &zero, sizeof (zero)));
}

void set_subscribe_all (zlink::socket_base_t *socket_)
{
    TEST_ASSERT_SUCCESS_ERRNO (
      socket_->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, "", 0));
}

void set_recv_timeout (zlink::socket_base_t *socket_, int timeout_ms_)
{
    TEST_ASSERT_SUCCESS_ERRNO (socket_->setsockopt (
      ZLINK_INTERNAL_OPT_RCVTIMEO, &timeout_ms_, sizeof (timeout_ms_)));
}

void set_send_timeout (zlink::socket_base_t *socket_, int timeout_ms_)
{
    TEST_ASSERT_SUCCESS_ERRNO (socket_->setsockopt (
      ZLINK_INTERNAL_OPT_SNDTIMEO, &timeout_ms_, sizeof (timeout_ms_)));
}

void set_hwm (zlink::socket_base_t *socket_, int value_)
{
    TEST_ASSERT_SUCCESS_ERRNO (
      socket_->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &value_, sizeof (value_)));
    TEST_ASSERT_SUCCESS_ERRNO (
      socket_->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &value_, sizeof (value_)));
}

int publish_message_with_flags (zlink::socket_base_t *socket_,
                                const std::string &topic_,
                                const std::vector<std::string> &parts_,
                                int flags_)
{
    std::vector<zlink_msg_t> frames (parts_.size ());
    if (!frames.empty ())
        memset (&frames[0], 0, frames.size () * sizeof (zlink_msg_t));
    for (size_t i = 0; i < parts_.size (); ++i) {
        if (zlink_msg_init_size (&frames[i], parts_[i].size ()) != 0) {
            for (size_t j = 0; j < i; ++j)
                (void) zlink_msg_close (&frames[j]);
            return -1;
        }
        if (!parts_[i].empty ())
            memcpy (zlink_msg_data (&frames[i]), parts_[i].data (),
                    parts_[i].size ());
    }

    const int rc = zlink::logical_multipart_publish (
      socket_, topic_.c_str (), frames.empty () ? NULL : &frames[0],
      frames.size (), flags_, true);
    const int saved_errno = rc == 0 ? 0 : errno;
    for (size_t i = 0; i < frames.size (); ++i)
        (void) zlink_msg_close (&frames[i]);
    if (saved_errno != 0) {
        errno = saved_errno;
        return -1;
    }
    return 0;
}

int publish_message (zlink::socket_base_t *socket_,
                     const std::string &topic_,
                     const std::vector<std::string> &parts_)
{
    return publish_message_with_flags (socket_, topic_, parts_, 0);
}

bool recv_message (zlink::socket_base_t *socket_,
                   std::vector<std::string> *frames_out_,
                   bool dontwait_ = false)
{
    frames_out_->clear ();
    zlink::msg_t frame;
    if (frame.init () != 0)
        return false;
    if (socket_->recv (&frame, dontwait_ ? ZLINK_DONTWAIT : 0) != 0) {
        frame.close ();
        return false;
    }

    bool more = true;
    while (more) {
        frames_out_->push_back (std::string (
          static_cast<const char *> (frame.data ()), frame.size ()));
        more = (frame.flags () & zlink::msg_t::more) != 0;
        frame.close ();
        if (!more)
            break;
        if (frame.init () != 0)
            return false;
        if (socket_->recv (&frame, 0) != 0) {
            frame.close ();
            return false;
        }
    }
    return true;
}

int drain_messages (zlink::socket_base_t *socket_)
{
    int drained = 0;
    std::vector<std::string> frames;
    while (recv_message (socket_, &frames, true))
        ++drained;
    return drained;
}

int send_single_frame (zlink::socket_base_t *socket_,
                       const std::string &payload_,
                       int flags_)
{
    zlink::msg_t msg;
    if (msg.init_size (payload_.size ()) != 0)
        return -1;
    if (!payload_.empty ())
        memcpy (msg.data (), payload_.data (), payload_.size ());
    const int rc = socket_->send (&msg, flags_);
    const int saved_errno = rc == 0 ? 0 : errno;
    msg.close ();
    if (saved_errno != 0) {
        errno = saved_errno;
        return -1;
    }
    return 0;
}

void fill_socket_until_hwm (zlink::socket_base_t *socket_)
{
    int sent = 0;
    while (send_single_frame (socket_, "fill", ZLINK_DONTWAIT) == 0)
        ++sent;

    TEST_ASSERT_GREATER_THAN_INT (0, sent);
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);
}

void close_socket (zlink::ctx_t *ctx_, zlink::socket_base_t *&socket_)
{
    (void) ctx_;
    if (!socket_)
        return;
    socket_->stop ();
    socket_->close ();
    socket_ = NULL;
}

void test_mesh_pub_budget_defaults_follow_transport_and_ready_peer_count ()
{
    TEST_ASSERT_EQUAL_INT (
      1000,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tcp://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (
      1000,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tcp://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (
      1000,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tcp://127.0.0.1:9000", 2));

    TEST_ASSERT_EQUAL_INT (
      1000,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "ws://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (
      1000,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "ws://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (
      1000,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "ws://127.0.0.1:9000", 2));

    TEST_ASSERT_EQUAL_INT (
      1000,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tls://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (
      1000,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tls://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (
      1000,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tls://127.0.0.1:9000", 2));

    TEST_ASSERT_EQUAL_INT (
      1000,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "wss://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (
      1000,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "wss://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (
      1000,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "wss://127.0.0.1:9000", 2));
}

void test_mesh_pub_budget_refresh_follows_ready_peer_count_changes ()
{
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "tls://127.0.0.1:9000", 0, 1));
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "wss://127.0.0.1:9000", 0, 2));
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "tcp://127.0.0.1:9000", 0, 1));
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "tcp://127.0.0.1:9000", 1, 2));
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "tls://127.0.0.1:9000", 1, 2));
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "tls://127.0.0.1:9000", 2, 100));
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "wss://127.0.0.1:9000", 2, 100));
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "ws://127.0.0.1:9000", 2, 100));
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "tls://127.0.0.1:9000", 2, 2));
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "wss://127.0.0.1:9000", 100, 100));
}

void test_ready_peer_count_is_clamped_to_connected_peers ()
{
    TEST_ASSERT_EQUAL_UINT (
      2, zlink::clamp_ready_peer_count (100, 2));
    TEST_ASSERT_EQUAL_UINT (
      1, zlink::clamp_ready_peer_count (1, 2));
    TEST_ASSERT_EQUAL_UINT (
      0, zlink::clamp_ready_peer_count (7, 0));
}

void test_mesh_pub_budget_hint_updates_private_runtime_owner ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.bound_endpoint = "wss://127.0.0.1:9000";

    TEST_ASSERT_TRUE (
      zlink::spot_mesh_pub_budget_t::publish_ready_hint (&runtime, 1));
    TEST_ASSERT_EQUAL_UINT (
      1, zlink::mesh_pub_ready_peer_count (&runtime.execution.mesh_peer_state));
    TEST_ASSERT_EQUAL_UINT64 (
      0, zlink::mesh_pub_budget_version (&runtime.execution.mesh_peer_state));

    TEST_ASSERT_TRUE (
      zlink::spot_mesh_pub_budget_t::publish_ready_hint (&runtime, 2));
    TEST_ASSERT_EQUAL_UINT (
      2, zlink::mesh_pub_ready_peer_count (&runtime.execution.mesh_peer_state));
    TEST_ASSERT_EQUAL_UINT64 (
      0, zlink::mesh_pub_budget_version (&runtime.execution.mesh_peer_state));

    zlink::spot_mesh_pub_budget_t::reset_runtime_state (&runtime);
    TEST_ASSERT_EQUAL_UINT (
      0, zlink::mesh_pub_ready_peer_count (&runtime.execution.mesh_peer_state));
    TEST_ASSERT_EQUAL_UINT64 (
      1, zlink::mesh_pub_budget_version (&runtime.execution.mesh_peer_state));
}

void test_mesh_pub_budget_runtime_owner_uses_bound_endpoint ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.bound_endpoint = "wss://127.0.0.1:9000";

    TEST_ASSERT_TRUE (
      zlink::spot_mesh_pub_budget_t::publish_ready_hint (&runtime, 2));
    TEST_ASSERT_EQUAL_INT (
      1000, zlink::spot_mesh_pub_budget_t::resolve_runtime_default (&runtime));
    TEST_ASSERT_EQUAL_INT (
      1000,
      zlink::spot_mesh_pub_budget_t::resolve_initial_bind_sndhwm (
        &runtime, runtime.bound_endpoint));

    zlink::spot_mesh_pub_budget_t::reset_runtime_state (&runtime);
    TEST_ASSERT_EQUAL_UINT (
      0, zlink::mesh_pub_ready_peer_count (&runtime.execution.mesh_peer_state));
}

void test_mesh_pub_budget_runtime_owner_tracks_ready_count_changes ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.bound_endpoint = "tls://127.0.0.1:9000";

    TEST_ASSERT_TRUE (
      zlink::spot_mesh_pub_budget_t::publish_ready_hint (&runtime, 1));
    TEST_ASSERT_EQUAL_UINT64 (
      0, zlink::mesh_pub_budget_version (&runtime.execution.mesh_peer_state));

    TEST_ASSERT_TRUE (
      zlink::spot_mesh_pub_budget_t::publish_ready_hint (&runtime, 2));
    TEST_ASSERT_EQUAL_UINT (
      2, zlink::mesh_pub_ready_peer_count (&runtime.execution.mesh_peer_state));
    TEST_ASSERT_EQUAL_UINT64 (
      0, zlink::mesh_pub_budget_version (&runtime.execution.mesh_peer_state));
    TEST_ASSERT_EQUAL_INT (
      1000,
      zlink::spot_mesh_pub_budget_t::resolve_runtime_default (&runtime));
}

void test_spot_node_batch_options_round_trip_public_api ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    int enabled = 1;
    int delay_ms = 25;
    int max_messages = 7;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_PEER_BATCH_ENABLE, &enabled,
      sizeof (enabled)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_PEER_BATCH_DELAY_MS, &delay_ms,
      sizeof (delay_ms)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_PEER_BATCH_MAX_MESSAGES, &max_messages,
      sizeof (max_messages)));

    int value = 0;
    size_t value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_PEER_BATCH_ENABLE, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (1, value);
    value = 0;
    value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_PEER_BATCH_DELAY_MS, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (25, value);
    value = 0;
    value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_PEER_BATCH_MAX_MESSAGES, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (7, value);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_batch_options_expose_documented_defaults ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    struct expected_option_t
    {
        zlink_spot_node_option_t option;
        int expected_value;
    };
    const expected_option_t expected[] = {
      { ZLINK_SPOT_NODE_OPT_PEER_BATCH_ENABLE, 0 },
      { ZLINK_SPOT_NODE_OPT_PEER_BATCH_DELAY_MS, 20 },
      { ZLINK_SPOT_NODE_OPT_PEER_BATCH_MAX_MESSAGES, 32 },
      { ZLINK_SPOT_NODE_OPT_PEER_BATCH_MAX_BYTES, 64 * 1024 },
      { ZLINK_SPOT_NODE_OPT_PEER_BATCH_BYPASS_BYTES, 64 * 1024 },
      { ZLINK_SPOT_NODE_OPT_PEER_UNBATCH_MAX_MESSAGES_PER_TURN, 32 },
      { ZLINK_SPOT_NODE_OPT_PEER_UNBATCH_MAX_BYTES_PER_TURN, 64 * 1024 },
    };

    for (size_t i = 0; i < sizeof (expected) / sizeof (expected[0]); ++i) {
        int value = -1;
        size_t value_size = sizeof (value);
        TEST_ASSERT_SUCCESS_ERRNO (zlink_get_spot_node_option (
          node, expected[i].option, &value, &value_size));
        TEST_ASSERT_EQUAL_UINT (sizeof (value), value_size);
        TEST_ASSERT_EQUAL_INT (expected[i].expected_value, value);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_hwm_options_round_trip_public_api ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    int topic_send = 111;
    int topic_recv = 222;
    int routed_send = 333;
    int routed_recv = 444;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_TOPIC_SEND_HWM, &topic_send,
      sizeof (topic_send)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_TOPIC_RECV_HWM, &topic_recv,
      sizeof (topic_recv)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM, &routed_send,
      sizeof (routed_send)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM, &routed_recv,
      sizeof (routed_recv)));

    int value = 0;
    size_t value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_TOPIC_SEND_HWM, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (111, value);
    value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_TOPIC_RECV_HWM, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (222, value);
    value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (333, value);
    value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (444, value);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_hwm_options_expose_defaults ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);

    const zlink_spot_node_option_t options[] = {
      ZLINK_SPOT_NODE_OPT_TOPIC_SEND_HWM,
      ZLINK_SPOT_NODE_OPT_TOPIC_RECV_HWM,
      ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM,
      ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM,
    };

    for (size_t i = 0; i < sizeof (options) / sizeof (options[0]); ++i) {
        int value = -1;
        size_t value_size = sizeof (value);
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_get_spot_node_option (node, options[i], &value, &value_size));
        TEST_ASSERT_EQUAL_UINT (sizeof (value), value_size);
        TEST_ASSERT_EQUAL_INT (1000, value);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_peer_batch_delay_flush_emits_internal_batch_frame ()
{
    zlink::ctx_t *ctx = new zlink::ctx_t;
    TEST_ASSERT_NOT_NULL (ctx);
    zlink::socket_base_t *source = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *ingress = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *mesh_pub = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *mesh_sink = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    TEST_ASSERT_NOT_NULL (source);
    TEST_ASSERT_NOT_NULL (ingress);
    TEST_ASSERT_NOT_NULL (mesh_pub);
    TEST_ASSERT_NOT_NULL (mesh_sink);
    set_zero_linger (source);
    set_zero_linger (ingress);
    set_zero_linger (mesh_pub);
    set_zero_linger (mesh_sink);
    set_recv_timeout (mesh_sink, 500);
    TEST_ASSERT_SUCCESS_ERRNO (ingress->bind ("inproc://spot-batch-delay-in"));
    TEST_ASSERT_SUCCESS_ERRNO (source->connect ("inproc://spot-batch-delay-in"));
    TEST_ASSERT_SUCCESS_ERRNO (mesh_pub->bind ("inproc://spot-batch-delay-out"));
    TEST_ASSERT_SUCCESS_ERRNO (
      mesh_sink->connect ("inproc://spot-batch-delay-out"));
    msleep (10);

    zlink::spot_runtime_t runtime (NULL);
    runtime.peer_batch_config.enabled = true;
    runtime.peer_batch_config.delay_ms = 10;
    runtime.peer_batch_config.max_messages = 64;
    runtime.peer_batch_config.max_bytes = 65536;
    runtime.peer_batch_config.bypass_bytes = 65536;
    zlink::spot_data_plane_runtime_state_t state;

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_message (source, "A", std::vector<std::string> (1, "m1")));
    TEST_ASSERT_SUCCESS_ERRNO (zlink::spot_data_plane_forwarder_t::recv_and_forward_ingress (
      ingress, mesh_pub, NULL, &runtime, &state, NULL));
    std::vector<std::string> frames;
    TEST_ASSERT_FALSE (recv_message (mesh_sink, &frames, true));
    TEST_ASSERT_EQUAL_UINT (1, state.pending_peer_batches.size ());

    const uint64_t deadline = zlink::spot_data_plane_forwarder_t::next_flush_deadline_ms (
      &state, runtime.peer_batch_config);
    TEST_ASSERT_TRUE (deadline != 0);
    TEST_ASSERT_SUCCESS_ERRNO (zlink::spot_data_plane_forwarder_t::flush_due_buckets (
      &runtime, &state, mesh_pub, deadline));
    TEST_ASSERT_TRUE (recv_message (mesh_sink, &frames));
    TEST_ASSERT_EQUAL_UINT (4, frames.size ());
    TEST_ASSERT_EQUAL_STRING ("A", frames[0].c_str ());
    TEST_ASSERT_EQUAL_UINT (12, frames[1].size ());
    TEST_ASSERT_EQUAL_UINT (16, frames[2].size ());
    TEST_ASSERT_TRUE (state.pending_peer_batches.empty ());

    close_socket (ctx, mesh_sink);
    close_socket (ctx, mesh_pub);
    close_socket (ctx, ingress);
    close_socket (ctx, source);
    TEST_ASSERT_SUCCESS_ERRNO (ctx->terminate ());
}

void test_peer_batch_max_messages_flushes_on_enqueue_boundary ()
{
    zlink::ctx_t *ctx = new zlink::ctx_t;
    zlink::socket_base_t *source = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *ingress = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *mesh_pub = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *mesh_sink = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    set_zero_linger (source);
    set_zero_linger (ingress);
    set_zero_linger (mesh_pub);
    set_zero_linger (mesh_sink);
    set_recv_timeout (mesh_sink, 50);
    TEST_ASSERT_SUCCESS_ERRNO (ingress->bind ("inproc://spot-batch-count-in"));
    TEST_ASSERT_SUCCESS_ERRNO (source->connect ("inproc://spot-batch-count-in"));
    TEST_ASSERT_SUCCESS_ERRNO (mesh_pub->bind ("inproc://spot-batch-count-out"));
    TEST_ASSERT_SUCCESS_ERRNO (
      mesh_sink->connect ("inproc://spot-batch-count-out"));
    msleep (10);

    zlink::spot_runtime_t runtime (NULL);
    runtime.peer_batch_config.enabled = true;
    runtime.peer_batch_config.delay_ms = 1000;
    runtime.peer_batch_config.max_messages = 2;
    runtime.peer_batch_config.max_bytes = 65536;
    runtime.peer_batch_config.bypass_bytes = 65536;
    zlink::spot_data_plane_runtime_state_t state;
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_message (source, "A", std::vector<std::string> (1, "m1")));
    TEST_ASSERT_SUCCESS_ERRNO (zlink::spot_data_plane_forwarder_t::recv_and_forward_ingress (
      ingress, mesh_pub, NULL, &runtime, &state, NULL));
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_message (source, "A", std::vector<std::string> (1, "m2")));
    TEST_ASSERT_SUCCESS_ERRNO (zlink::spot_data_plane_forwarder_t::recv_and_forward_ingress (
      ingress, mesh_pub, NULL, &runtime, &state, NULL));

    std::vector<std::string> frames;
    TEST_ASSERT_TRUE (recv_message (mesh_sink, &frames));
    TEST_ASSERT_EQUAL_UINT (4, frames.size ());
    TEST_ASSERT_TRUE (state.pending_peer_batches.empty ());

    close_socket (ctx, mesh_sink);
    close_socket (ctx, mesh_pub);
    close_socket (ctx, ingress);
    close_socket (ctx, source);
    TEST_ASSERT_SUCCESS_ERRNO (ctx->terminate ());
}

void test_peer_batch_oversized_bypass_preserves_plain_message_wire_shape ()
{
    zlink::ctx_t *ctx = new zlink::ctx_t;
    zlink::socket_base_t *source = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *ingress = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *mesh_pub = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *mesh_sink = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    set_zero_linger (source);
    set_zero_linger (ingress);
    set_zero_linger (mesh_pub);
    set_zero_linger (mesh_sink);
    set_recv_timeout (mesh_sink, 50);
    TEST_ASSERT_SUCCESS_ERRNO (ingress->bind ("inproc://spot-batch-bypass-in"));
    TEST_ASSERT_SUCCESS_ERRNO (source->connect ("inproc://spot-batch-bypass-in"));
    TEST_ASSERT_SUCCESS_ERRNO (mesh_pub->bind ("inproc://spot-batch-bypass-out"));
    TEST_ASSERT_SUCCESS_ERRNO (
      mesh_sink->connect ("inproc://spot-batch-bypass-out"));
    msleep (10);

    zlink::spot_runtime_t runtime (NULL);
    runtime.peer_batch_config.enabled = true;
    runtime.peer_batch_config.delay_ms = 1000;
    runtime.peer_batch_config.max_messages = 16;
    runtime.peer_batch_config.max_bytes = 65536;
    runtime.peer_batch_config.bypass_bytes = 8;
    zlink::spot_data_plane_runtime_state_t state;

    TEST_ASSERT_SUCCESS_ERRNO (publish_message (
      source, "A", std::vector<std::string> (1, std::string (16, 'x'))));
    TEST_ASSERT_SUCCESS_ERRNO (zlink::spot_data_plane_forwarder_t::recv_and_forward_ingress (
      ingress, mesh_pub, NULL, &runtime, &state, NULL));

    std::vector<std::string> frames;
    TEST_ASSERT_TRUE (recv_message (mesh_sink, &frames));
    TEST_ASSERT_EQUAL_UINT (2, frames.size ());
    TEST_ASSERT_EQUAL_STRING ("A", frames[0].c_str ());
    TEST_ASSERT_EQUAL_UINT (16, frames[1].size ());
    TEST_ASSERT_TRUE (state.pending_peer_batches.empty ());

    close_socket (ctx, mesh_sink);
    close_socket (ctx, mesh_pub);
    close_socket (ctx, ingress);
    close_socket (ctx, source);
    TEST_ASSERT_SUCCESS_ERRNO (ctx->terminate ());
}

void test_peer_batch_max_bytes_flushes_before_next_enqueue ()
{
    zlink::ctx_t *ctx = new zlink::ctx_t;
    zlink::socket_base_t *source = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *ingress = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *mesh_pub = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *mesh_sink = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    set_zero_linger (source);
    set_zero_linger (ingress);
    set_zero_linger (mesh_pub);
    set_zero_linger (mesh_sink);
    set_recv_timeout (mesh_sink, 50);
    TEST_ASSERT_SUCCESS_ERRNO (ingress->bind ("inproc://spot-batch-bytes-in"));
    TEST_ASSERT_SUCCESS_ERRNO (source->connect ("inproc://spot-batch-bytes-in"));
    TEST_ASSERT_SUCCESS_ERRNO (mesh_pub->bind ("inproc://spot-batch-bytes-out"));
    TEST_ASSERT_SUCCESS_ERRNO (
      mesh_sink->connect ("inproc://spot-batch-bytes-out"));
    msleep (10);

    zlink::spot_runtime_t runtime (NULL);
    runtime.peer_batch_config.enabled = true;
    runtime.peer_batch_config.delay_ms = 1000;
    runtime.peer_batch_config.max_messages = 16;
    runtime.peer_batch_config.max_bytes = 12;
    runtime.peer_batch_config.bypass_bytes = 1024;
    zlink::spot_data_plane_runtime_state_t state;

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_message (source, "A", std::vector<std::string> (1, "a")));
    TEST_ASSERT_SUCCESS_ERRNO (zlink::spot_data_plane_forwarder_t::recv_and_forward_ingress (
      ingress, mesh_pub, NULL, &runtime, &state, NULL));
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_message (source, "A", std::vector<std::string> (1, "b")));
    TEST_ASSERT_SUCCESS_ERRNO (zlink::spot_data_plane_forwarder_t::recv_and_forward_ingress (
      ingress, mesh_pub, NULL, &runtime, &state, NULL));

    std::vector<std::string> first;
    TEST_ASSERT_TRUE (recv_message (mesh_sink, &first));
    TEST_ASSERT_EQUAL_UINT (4, first.size ());
    TEST_ASSERT_EQUAL_UINT (1, state.pending_peer_batches.size ());
    TEST_ASSERT_EQUAL_UINT (
      1, state.pending_peer_batches.find ("A")->second.pending_message_count);

    close_socket (ctx, mesh_sink);
    close_socket (ctx, mesh_pub);
    close_socket (ctx, ingress);
    close_socket (ctx, source);
    TEST_ASSERT_SUCCESS_ERRNO (ctx->terminate ());
}

void test_peer_batch_same_topic_small_then_large_bypass_keeps_order ()
{
    zlink::ctx_t *ctx = new zlink::ctx_t;
    zlink::socket_base_t *source = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *ingress = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *mesh_pub = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *mesh_sink = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    set_zero_linger (source);
    set_zero_linger (ingress);
    set_zero_linger (mesh_pub);
    set_zero_linger (mesh_sink);
    set_recv_timeout (mesh_sink, 50);
    TEST_ASSERT_SUCCESS_ERRNO (ingress->bind ("inproc://spot-batch-order-in"));
    TEST_ASSERT_SUCCESS_ERRNO (source->connect ("inproc://spot-batch-order-in"));
    TEST_ASSERT_SUCCESS_ERRNO (mesh_pub->bind ("inproc://spot-batch-order-out"));
    TEST_ASSERT_SUCCESS_ERRNO (
      mesh_sink->connect ("inproc://spot-batch-order-out"));
    msleep (10);

    zlink::spot_runtime_t runtime (NULL);
    runtime.peer_batch_config.enabled = true;
    runtime.peer_batch_config.delay_ms = 1000;
    runtime.peer_batch_config.max_messages = 16;
    runtime.peer_batch_config.max_bytes = 65536;
    runtime.peer_batch_config.bypass_bytes = 12;
    zlink::spot_data_plane_runtime_state_t state;

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_message (source, "A", std::vector<std::string> (1, "m1")));
    TEST_ASSERT_SUCCESS_ERRNO (zlink::spot_data_plane_forwarder_t::recv_and_forward_ingress (
      ingress, mesh_pub, NULL, &runtime, &state, NULL));
    TEST_ASSERT_SUCCESS_ERRNO (publish_message (
      source, "A", std::vector<std::string> (1, std::string (32, 'L'))));
    TEST_ASSERT_SUCCESS_ERRNO (zlink::spot_data_plane_forwarder_t::recv_and_forward_ingress (
      ingress, mesh_pub, NULL, &runtime, &state, NULL));

    std::vector<std::string> first;
    std::vector<std::string> second;
    TEST_ASSERT_TRUE (recv_message (mesh_sink, &first));
    TEST_ASSERT_TRUE (recv_message (mesh_sink, &second));
    TEST_ASSERT_EQUAL_UINT (4, first.size ());
    TEST_ASSERT_EQUAL_UINT (2, second.size ());
    TEST_ASSERT_EQUAL_STRING ("A", first[0].c_str ());
    TEST_ASSERT_EQUAL_STRING ("A", second[0].c_str ());
    TEST_ASSERT_EQUAL_STRING_LEN ("m1", first[3].data () + 10, 2);
    TEST_ASSERT_EQUAL_UINT (32, second[1].size ());

    close_socket (ctx, mesh_sink);
    close_socket (ctx, mesh_pub);
    close_socket (ctx, ingress);
    close_socket (ctx, source);
    TEST_ASSERT_SUCCESS_ERRNO (ctx->terminate ());
}

void test_peer_batch_flush_all_buckets_drains_pending_shutdown_work ()
{
    zlink::ctx_t *ctx = new zlink::ctx_t;
    zlink::socket_base_t *source = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *ingress = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *mesh_pub = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *mesh_sink = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    set_zero_linger (source);
    set_zero_linger (ingress);
    set_zero_linger (mesh_pub);
    set_zero_linger (mesh_sink);
    set_recv_timeout (mesh_sink, 50);
    TEST_ASSERT_SUCCESS_ERRNO (ingress->bind ("inproc://spot-batch-drain-in"));
    TEST_ASSERT_SUCCESS_ERRNO (source->connect ("inproc://spot-batch-drain-in"));
    TEST_ASSERT_SUCCESS_ERRNO (mesh_pub->bind ("inproc://spot-batch-drain-out"));
    TEST_ASSERT_SUCCESS_ERRNO (
      mesh_sink->connect ("inproc://spot-batch-drain-out"));
    msleep (10);

    zlink::spot_runtime_t runtime (NULL);
    runtime.peer_batch_config.enabled = true;
    runtime.peer_batch_config.delay_ms = 1000;
    runtime.peer_batch_config.max_messages = 16;
    runtime.peer_batch_config.max_bytes = 65536;
    runtime.peer_batch_config.bypass_bytes = 65536;
    zlink::spot_data_plane_runtime_state_t state;

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_message (source, "A", std::vector<std::string> (1, "shutdown")));
    TEST_ASSERT_SUCCESS_ERRNO (zlink::spot_data_plane_forwarder_t::recv_and_forward_ingress (
      ingress, mesh_pub, NULL, &runtime, &state, NULL));
    TEST_ASSERT_EQUAL_UINT (1, state.pending_peer_batches.size ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink::spot_data_plane_forwarder_t::flush_all_buckets (
      &runtime, &state, mesh_pub));
    std::vector<std::string> frames;
    TEST_ASSERT_TRUE (recv_message (mesh_sink, &frames));
    TEST_ASSERT_TRUE (state.pending_peer_batches.empty ());

    close_socket (ctx, mesh_sink);
    close_socket (ctx, mesh_pub);
    close_socket (ctx, ingress);
    close_socket (ctx, source);
    TEST_ASSERT_SUCCESS_ERRNO (ctx->terminate ());
}

void test_peer_batch_blocked_mesh_pub_stops_ingress_drain_and_propagates_pressure ()
{
    zlink::ctx_t *ctx = new zlink::ctx_t;
    zlink::socket_base_t *source = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *ingress = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *mesh_pub = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *mesh_sink = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    TEST_ASSERT_NOT_NULL (source);
    TEST_ASSERT_NOT_NULL (ingress);
    TEST_ASSERT_NOT_NULL (mesh_pub);
    TEST_ASSERT_NOT_NULL (mesh_sink);
    set_zero_linger (source);
    set_zero_linger (ingress);
    set_zero_linger (mesh_pub);
    set_zero_linger (mesh_sink);
    set_hwm (source, 1);
    set_hwm (ingress, 1);
    set_hwm (mesh_pub, 1);
    set_hwm (mesh_sink, 1);
    set_send_timeout (source, 200);
    set_send_timeout (mesh_pub, 200);
    set_recv_timeout (mesh_sink, 50);
    TEST_ASSERT_SUCCESS_ERRNO (
      ingress->bind ("inproc://spot-batch-blocked-ingress"));
    TEST_ASSERT_SUCCESS_ERRNO (
      source->connect ("inproc://spot-batch-blocked-ingress"));
    TEST_ASSERT_SUCCESS_ERRNO (
      mesh_pub->bind ("inproc://spot-batch-blocked-mesh"));
    TEST_ASSERT_SUCCESS_ERRNO (
      mesh_sink->connect ("inproc://spot-batch-blocked-mesh"));
    std::this_thread::sleep_for (std::chrono::milliseconds (10));

    fill_socket_until_hwm (mesh_pub);

    zlink::spot_runtime_t runtime (NULL);
    runtime.peer_batch_config.enabled = true;
    runtime.peer_batch_config.delay_ms = 0;
    runtime.peer_batch_config.max_messages = 1;
    runtime.peer_batch_config.max_bytes = 65536;
    runtime.peer_batch_config.bypass_bytes = 65536;
    runtime.set_peer_batch_config (runtime.peer_batch_config);
    zlink::spot_data_plane_runtime_state_t state;

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_message (source, "A", std::vector<std::string> (1, "m1")));

    std::future<int> drain_future = std::async (std::launch::async, [&]() {
        const int rc =
          zlink::spot_data_plane_forwarder_t::recv_and_forward_ingress (
            ingress, mesh_pub, NULL, &runtime, &state, NULL);
        return rc == 0 ? 0 : errno;
    });

    std::this_thread::sleep_for (std::chrono::milliseconds (100));
    const std::future_status blocked_status =
      drain_future.wait_for (std::chrono::milliseconds (0));
    TEST_ASSERT_EQUAL_INT (static_cast<int> (std::future_status::timeout),
                           static_cast<int> (blocked_status));

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_message (source, "A", std::vector<std::string> (1, "m2")));
    int pressure_sent = 0;
    for (; pressure_sent < 8192; ++pressure_sent) {
        const std::string payload = "m" + std::to_string (pressure_sent + 3);
        if (publish_message_with_flags (
              source, "A", std::vector<std::string> (1, payload),
              ZLINK_DONTWAIT)
            != 0) {
            TEST_ASSERT_EQUAL_INT (EAGAIN, errno);
            break;
        }
    }
    TEST_ASSERT_LESS_THAN_INT (8192, pressure_sent);

    const std::future_status drain_ready =
      drain_future.wait_for (std::chrono::milliseconds (1500));
    if (drain_ready != std::future_status::ready) {
        close_socket (ctx, mesh_sink);
        close_socket (ctx, mesh_pub);
        TEST_ASSERT_EQUAL_INT (
          static_cast<int> (std::future_status::ready),
          static_cast<int> (
            drain_future.wait_for (std::chrono::milliseconds (1500))));
    }
    const int drain_result = drain_future.get ();
    TEST_ASSERT_TRUE (drain_result == 0 || drain_result == EAGAIN
                      || drain_result == ETERM);

    close_socket (ctx, mesh_sink);
    close_socket (ctx, mesh_pub);
    close_socket (ctx, ingress);
    close_socket (ctx, source);
    TEST_ASSERT_SUCCESS_ERRNO (ctx->terminate ());
}

void test_peer_batch_next_turn_resumes_after_blocked_mesh_pub_is_released ()
{
    zlink::ctx_t *ctx = new zlink::ctx_t;
    zlink::socket_base_t *source = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *ingress = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *mesh_pub = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    zlink::socket_base_t *mesh_sink = ctx->create_socket (ZLINK_CORE_SOCKET_PAIR);
    TEST_ASSERT_NOT_NULL (source);
    TEST_ASSERT_NOT_NULL (ingress);
    TEST_ASSERT_NOT_NULL (mesh_pub);
    TEST_ASSERT_NOT_NULL (mesh_sink);
    set_zero_linger (source);
    set_zero_linger (ingress);
    set_zero_linger (mesh_pub);
    set_zero_linger (mesh_sink);
    set_hwm (source, 4);
    set_hwm (ingress, 4);
    set_hwm (mesh_pub, 1);
    set_hwm (mesh_sink, 1);
    set_send_timeout (mesh_pub, 100);
    set_recv_timeout (mesh_sink, 100);
    TEST_ASSERT_SUCCESS_ERRNO (
      ingress->bind ("inproc://spot-batch-recovery-ingress"));
    TEST_ASSERT_SUCCESS_ERRNO (
      source->connect ("inproc://spot-batch-recovery-ingress"));
    TEST_ASSERT_SUCCESS_ERRNO (
      mesh_pub->bind ("inproc://spot-batch-recovery-mesh"));
    TEST_ASSERT_SUCCESS_ERRNO (
      mesh_sink->connect ("inproc://spot-batch-recovery-mesh"));
    std::this_thread::sleep_for (std::chrono::milliseconds (10));

    fill_socket_until_hwm (mesh_pub);

    zlink::spot_runtime_t runtime (NULL);
    runtime.peer_batch_config.enabled = true;
    runtime.peer_batch_config.delay_ms = 0;
    runtime.peer_batch_config.max_messages = 1;
    runtime.peer_batch_config.max_bytes = 65536;
    runtime.peer_batch_config.bypass_bytes = 65536;
    runtime.set_peer_batch_config (runtime.peer_batch_config);
    zlink::spot_data_plane_runtime_state_t state;

    TEST_ASSERT_SUCCESS_ERRNO (
      publish_message (source, "A", std::vector<std::string> (1, "m1")));
    TEST_ASSERT_SUCCESS_ERRNO (
      publish_message (source, "A", std::vector<std::string> (1, "m2")));

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink::spot_data_plane_forwarder_t::recv_and_forward_ingress (
            ingress, mesh_pub, NULL, &runtime, &state, NULL));
    TEST_ASSERT_EQUAL_INT (EAGAIN, errno);

    TEST_ASSERT_GREATER_THAN_INT (0, drain_messages (mesh_sink));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink::spot_data_plane_forwarder_t::recv_and_forward_ingress (
        ingress, mesh_pub, NULL, &runtime, &state, NULL));

    std::vector<std::string> frames;
    TEST_ASSERT_TRUE (recv_message (mesh_sink, &frames));
    TEST_ASSERT_EQUAL_UINT (4, frames.size ());
    TEST_ASSERT_EQUAL_STRING ("A", frames[0].c_str ());
    TEST_ASSERT_TRUE (frames[3].find ("m2") != std::string::npos);

    close_socket (ctx, mesh_sink);
    close_socket (ctx, mesh_pub);
    close_socket (ctx, ingress);
    close_socket (ctx, source);
    TEST_ASSERT_SUCCESS_ERRNO (ctx->terminate ());
}

}

int main (int argc, char **argv)
{
    UNITY_BEGIN ();
    RUN_TEST (
      test_mesh_pub_budget_defaults_follow_transport_and_ready_peer_count);
    RUN_TEST (
      test_mesh_pub_budget_refresh_follows_ready_peer_count_changes);
    RUN_TEST (test_ready_peer_count_is_clamped_to_connected_peers);
    RUN_TEST (test_mesh_pub_budget_hint_updates_private_runtime_owner);
    RUN_TEST (test_mesh_pub_budget_runtime_owner_uses_bound_endpoint);
    RUN_TEST (
      test_mesh_pub_budget_runtime_owner_tracks_ready_count_changes);
    RUN_TEST (test_spot_node_batch_options_round_trip_public_api);
    RUN_TEST (test_spot_node_batch_options_expose_documented_defaults);
    RUN_TEST (test_spot_node_hwm_options_round_trip_public_api);
    RUN_TEST (test_spot_node_hwm_options_expose_defaults);
    RUN_TEST (test_peer_batch_delay_flush_emits_internal_batch_frame);
    RUN_TEST (test_peer_batch_max_messages_flushes_on_enqueue_boundary);
    RUN_TEST (test_peer_batch_max_bytes_flushes_before_next_enqueue);
    RUN_TEST (
      test_peer_batch_oversized_bypass_preserves_plain_message_wire_shape);
    RUN_TEST (test_peer_batch_same_topic_small_then_large_bypass_keeps_order);
    RUN_TEST (test_peer_batch_flush_all_buckets_drains_pending_shutdown_work);
    RUN_TEST (
      test_peer_batch_blocked_mesh_pub_stops_ingress_drain_and_propagates_pressure);
    RUN_TEST (
      test_peer_batch_next_turn_resumes_after_blocked_mesh_pub_is_released);
    return UNITY_END ();
}
