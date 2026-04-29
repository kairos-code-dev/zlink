/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"

#include "core/ctx.hpp"
#include "core/auto_hwm_policy.hpp"
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
int expected_mesh_pub_sndhwm (size_t managed_connections_ = 0,
                              size_t active_connections_ = 0)
{
    zlink::auto_hwm_context_plan_t context_plan;
    zlink::auto_hwm_context_plan_from_budget_mb (
      ZLINK_CTX_AUTO_HWM_ENABLE_DFLT != 0,
      ZLINK_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB_DFLT, &context_plan);
    zlink::auto_hwm_socket_plan_t socket_plan;
    zlink::auto_hwm_socket_plan_for_role (
      context_plan, zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
      managed_connections_, active_connections_, &socket_plan, 0, -1, -1,
      false, false, zlink::auto_hwm_scope_none, 1, true,
      ZLINK_CTX_AUTO_HWM_SPOT_BOOTSTRAP_DFLT);
    return socket_plan.sndhwm;
}

int expected_spot_hwm (zlink::auto_hwm_role_t role_,
                       int socket_type_,
                       size_t managed_connections_ = 0,
                       size_t active_connections_ = 0)
{
    zlink::auto_hwm_context_plan_t context_plan;
    zlink::auto_hwm_context_plan_from_budget_mb (
      ZLINK_CTX_AUTO_HWM_ENABLE_DFLT != 0,
      ZLINK_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB_DFLT, &context_plan);
    zlink::auto_hwm_socket_plan_t socket_plan;
    zlink::auto_hwm_socket_plan_for_role (context_plan, role_, socket_type_,
                                          managed_connections_,
                                          active_connections_, &socket_plan, 0,
                                          -1, -1, false, false,
                                          zlink::auto_hwm_scope_none, 1, true,
                                          ZLINK_CTX_AUTO_HWM_SPOT_BOOTSTRAP_DFLT);
    return socket_plan.sndhwm;
}

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
      expected_mesh_pub_sndhwm (),
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tcp://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (
      expected_mesh_pub_sndhwm (),
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tcp://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (
      expected_mesh_pub_sndhwm (),
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tcp://127.0.0.1:9000", 2));

    TEST_ASSERT_EQUAL_INT (
      expected_mesh_pub_sndhwm (),
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "ws://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (
      expected_mesh_pub_sndhwm (),
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "ws://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (
      expected_mesh_pub_sndhwm (),
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "ws://127.0.0.1:9000", 2));

    TEST_ASSERT_EQUAL_INT (
      expected_mesh_pub_sndhwm (),
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tls://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (
      expected_mesh_pub_sndhwm (),
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tls://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (
      expected_mesh_pub_sndhwm (),
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tls://127.0.0.1:9000", 2));

    TEST_ASSERT_EQUAL_INT (
      expected_mesh_pub_sndhwm (),
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "wss://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (
      expected_mesh_pub_sndhwm (),
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "wss://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (
      expected_mesh_pub_sndhwm (),
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

void test_auto_hwm_v2_profile_caps_and_unit_budgets ()
{
    zlink::auto_hwm_context_plan_t context_plan;
    zlink::auto_hwm_socket_plan_t socket_plan;

    zlink::auto_hwm_context_plan_from_budget_mb (
      true, 1024, &context_plan, ZLINK_AUTO_HWM_PROFILE_BALANCED);
    zlink::auto_hwm_socket_plan_for_role (
      context_plan, zlink::auto_hwm_role_fanout, ZLINK_CORE_SOCKET_PUB, 100,
      100, &socket_plan, 262144, -1, -1, false, false,
      zlink::auto_hwm_scope_none, 1, true, 1);
    TEST_ASSERT_EQUAL_UINT32 (zlink::auto_hwm_policy_fanout,
                              socket_plan.policy_class);
    TEST_ASSERT_EQUAL_UINT64 (2u * 1024u * 1024u,
                              socket_plan.unit_budget_bytes);
    TEST_ASSERT_EQUAL_UINT32 (8u, socket_plan.size_cap);
    TEST_ASSERT_EQUAL_INT (8, socket_plan.sndhwm);

    zlink::auto_hwm_context_plan_from_budget_mb (
      true, 1024, &context_plan, ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY);
    zlink::auto_hwm_socket_plan_for_role (
      context_plan, zlink::auto_hwm_role_fanout, ZLINK_CORE_SOCKET_PUB, 100,
      100, &socket_plan, 262144, -1, -1, false, false,
      zlink::auto_hwm_scope_none, 1, true, 1);
    TEST_ASSERT_EQUAL_UINT64 (512u * 1024u, socket_plan.unit_budget_bytes);
    TEST_ASSERT_EQUAL_UINT32 (4u, socket_plan.size_cap);
    TEST_ASSERT_EQUAL_INT (2, socket_plan.sndhwm);

    zlink::auto_hwm_context_plan_from_budget_mb (
      true, 1024, &context_plan, ZLINK_AUTO_HWM_PROFILE_THROUGHPUT);
    zlink::auto_hwm_socket_plan_for_role (
      context_plan, zlink::auto_hwm_role_fanout, ZLINK_CORE_SOCKET_PUB, 100,
      100, &socket_plan, 262144, -1, -1, false, false,
      zlink::auto_hwm_scope_none, 1, true, 1);
    TEST_ASSERT_EQUAL_UINT64 (4u * 1024u * 1024u,
                              socket_plan.unit_budget_bytes);
    TEST_ASSERT_EQUAL_UINT32 (16u, socket_plan.size_cap);
    TEST_ASSERT_EQUAL_INT (16, socket_plan.sndhwm);
}

void test_auto_hwm_v2_policy_class_mapping_and_spot_fanout_limit ()
{
    TEST_ASSERT_EQUAL_UINT32 (
      zlink::auto_hwm_policy_peer_queue,
      zlink::auto_hwm_policy_class_for_role (
        zlink::auto_hwm_role_peer_queue, ZLINK_CORE_SOCKET_DEALER));
    TEST_ASSERT_EQUAL_UINT32 (
      zlink::auto_hwm_policy_stream,
      zlink::auto_hwm_policy_class_for_role (
        zlink::auto_hwm_role_stream, ZLINK_CORE_SOCKET_STREAM));

    zlink::auto_hwm_context_plan_t context_plan;
    zlink::auto_hwm_context_plan_from_budget_mb (
      true, 1024, &context_plan, ZLINK_AUTO_HWM_PROFILE_BALANCED);
    zlink::auto_hwm_socket_plan_t socket_plan;
    zlink::auto_hwm_socket_plan_for_role (
      context_plan, zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
      10000, 10000, &socket_plan, 64, -1, -1, false, false,
      zlink::auto_hwm_scope_shared, 10000, true, 500);
    TEST_ASSERT_EQUAL_UINT32 (zlink::auto_hwm_policy_spot_data,
                              socket_plan.policy_class);
    TEST_ASSERT_EQUAL_UINT32 (500u, socket_plan.effective_publish_fanout);
    TEST_ASSERT_EQUAL_UINT32 (64u, socket_plan.size_cap);
}

void test_auto_hwm_v2_planning_uses_observed_count_before_bootstrap ()
{
    zlink::auto_hwm_context_plan_t context_plan;
    zlink::auto_hwm_context_plan_from_budget_mb (
      true, 128, &context_plan, ZLINK_AUTO_HWM_PROFILE_BALANCED);

    zlink::auto_hwm_socket_plan_t socket_plan;
    zlink::auto_hwm_socket_plan_for_role (
      context_plan, zlink::auto_hwm_role_recv_ingress, ZLINK_CORE_SOCKET_SUB,
      1, 1, &socket_plan, 131072, -1, -1, false, false,
      zlink::auto_hwm_scope_per_spot, 1, true,
      ZLINK_CTX_AUTO_HWM_SPOT_BOOTSTRAP_DFLT);

    TEST_ASSERT_EQUAL_UINT32 (1u, socket_plan.observed_count);
    TEST_ASSERT_EQUAL_UINT32 (1u, socket_plan.planning_count);
    TEST_ASSERT_EQUAL_INT (2, socket_plan.rcvhwm);

    zlink::auto_hwm_socket_plan_for_role (
      context_plan, zlink::auto_hwm_role_recv_ingress, ZLINK_CORE_SOCKET_SUB,
      0, 0, &socket_plan, 131072, -1, -1, false, false,
      zlink::auto_hwm_scope_per_spot, 1, true,
      ZLINK_CTX_AUTO_HWM_SPOT_BOOTSTRAP_DFLT);

    TEST_ASSERT_EQUAL_UINT32 (0u, socket_plan.observed_count);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_CTX_AUTO_HWM_SPOT_BOOTSTRAP_DFLT,
                              socket_plan.planning_count);
    TEST_ASSERT_EQUAL_INT (1, socket_plan.rcvhwm);
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
      expected_mesh_pub_sndhwm (0, 0),
      zlink::spot_mesh_pub_budget_t::resolve_runtime_default (&runtime));
    TEST_ASSERT_EQUAL_INT (
      expected_mesh_pub_sndhwm (),
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
      expected_mesh_pub_sndhwm (0, 0),
      zlink::spot_mesh_pub_budget_t::resolve_runtime_default (&runtime));
}

void test_spot_node_hwm_options_round_trip_public_api ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);

    int topic_send = 111;
    int topic_recv = 222;
    int routed_send = 333;
    int routed_recv = 444;
    int sub_queue_limit = 55;
    int routed_queue_limit = 66;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_PUB_HWM, &topic_send,
      sizeof (topic_send)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_SUB_HWM, &topic_recv,
      sizeof (topic_recv)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM, &routed_send,
      sizeof (routed_send)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM, &routed_recv,
      sizeof (routed_recv)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_SUB_QUEUE_HARD_LIMIT, &sub_queue_limit,
      sizeof (sub_queue_limit)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_ROUTED_QUEUE_HARD_LIMIT, &routed_queue_limit,
      sizeof (routed_queue_limit)));

    int value = 0;
    size_t value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_PUB_HWM, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (111, value);
    value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_SUB_HWM, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (222, value);
    value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (333, value);
    value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (444, value);
    value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_SUB_QUEUE_HARD_LIMIT, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (55, value);
    value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_ROUTED_QUEUE_HARD_LIMIT, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (66, value);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_hwm_options_expose_defaults ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);

    const zlink_spot_node_option_t options[] = {
      ZLINK_SPOT_NODE_OPT_PUB_HWM,
      ZLINK_SPOT_NODE_OPT_SUB_HWM,
      ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM,
      ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM,
      ZLINK_SPOT_NODE_OPT_SUB_QUEUE_HARD_LIMIT,
      ZLINK_SPOT_NODE_OPT_ROUTED_QUEUE_HARD_LIMIT,
    };

    for (size_t i = 0; i < sizeof (options) / sizeof (options[0]); ++i) {
        int value = -1;
        size_t value_size = sizeof (value);
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_get_spot_node_option (node, options[i], &value, &value_size));
        TEST_ASSERT_EQUAL_UINT (sizeof (value), value_size);
        int expected = 0;
        if (options[i] == ZLINK_SPOT_NODE_OPT_SUB_QUEUE_HARD_LIMIT)
            expected = ZLINK_SPOT_NODE_SUB_QUEUE_HARD_LIMIT_DFLT;
        else if (options[i] == ZLINK_SPOT_NODE_OPT_ROUTED_QUEUE_HARD_LIMIT)
            expected = ZLINK_SPOT_NODE_ROUTED_QUEUE_HARD_LIMIT_DFLT;
        else if (options[i] == ZLINK_SPOT_NODE_OPT_PUB_HWM)
            expected = expected_mesh_pub_sndhwm ();
        else
            expected = expected_spot_hwm (
              options[i] == ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM
                  || options[i] == ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM
                ? zlink::auto_hwm_role_routed
                : zlink::auto_hwm_role_recv_ingress,
              options[i] == ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM
                  || options[i] == ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM
                ? ZLINK_CORE_SOCKET_ROUTER
                : ZLINK_CORE_SOCKET_SUB);
        TEST_ASSERT_EQUAL_INT (expected, value);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
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
    RUN_TEST (test_auto_hwm_v2_profile_caps_and_unit_budgets);
    RUN_TEST (test_auto_hwm_v2_policy_class_mapping_and_spot_fanout_limit);
    RUN_TEST (test_auto_hwm_v2_planning_uses_observed_count_before_bootstrap);
    RUN_TEST (test_mesh_pub_budget_hint_updates_private_runtime_owner);
    RUN_TEST (test_mesh_pub_budget_runtime_owner_uses_bound_endpoint);
    RUN_TEST (
      test_mesh_pub_budget_runtime_owner_tracks_ready_count_changes);
    RUN_TEST (test_spot_node_hwm_options_round_trip_public_api);
    RUN_TEST (test_spot_node_hwm_options_expose_defaults);
    return UNITY_END ();
}
