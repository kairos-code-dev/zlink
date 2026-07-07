/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"

#include "core/ctx.hpp"
#include "core/auto_hwm_policy.hpp"
#include "core/multipart_send_txn.hpp"
#include "services/spot/data_plane/spot_mesh_pub_hwm.hpp"
#include "services/spot/common/spot_auto_hwm_internal.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "sockets/common/socket_base.hpp"

#include <chrono>
#include <future>
#include <thread>
#include <vector>

namespace
{
int expected_mesh_pub_sndhwm (size_t managed_connections_ = 0, size_t active_connections_ = 0)
{
    LIBZLINK_UNUSED (managed_connections_);
    LIBZLINK_UNUSED (active_connections_);
    return 0;
}

int expected_spot_hwm (zlink::auto_hwm_role_t role_,
                       int socket_type_,
                       size_t managed_connections_ = 0,
                       size_t active_connections_ = 0)
{
    zlink::auto_hwm_context_plan_t context_plan;
    zlink::auto_hwm_context_plan_make (ZLINK_CTX_AUTO_HWM_ENABLE_DFLT != 0,
                                       ZLINK_CTX_AUTO_HWM_PROFILE_DFLT, &context_plan);
    zlink::auto_hwm_socket_plan_t socket_plan;
    zlink::auto_hwm_socket_plan_for_role (context_plan, role_, socket_type_, managed_connections_,
                                          active_connections_, &socket_plan, 0, -1, -1, false,
                                          false, zlink::auto_hwm_scope_none, 1, true);
    return socket_plan.sndhwm;
}

int planned_hwm (zlink::auto_hwm_role_t role_,
                 int socket_type_,
                 zlink_auto_hwm_profile_t profile_,
                 int message_unit_bytes_,
                 size_t managed_connections_ = 0)
{
    zlink::auto_hwm_context_plan_t context_plan;
    zlink::auto_hwm_context_plan_make (true, profile_, &context_plan);
    zlink::auto_hwm_socket_plan_t socket_plan;
    zlink::auto_hwm_socket_plan_for_role (
      context_plan, role_, socket_type_, managed_connections_, managed_connections_, &socket_plan,
      message_unit_bytes_, -1, -1, false, false, zlink::auto_hwm_scope_none, 1, true);
    return socket_plan.sndhwm;
}

int planned_connection_bucket_hwm (zlink::auto_hwm_role_t role_,
                                   int socket_type_,
                                   zlink_auto_hwm_profile_t profile_,
                                   int message_unit_bytes_,
                                   size_t managed_connections_)
{
    zlink::auto_hwm_context_plan_t context_plan;
    zlink::auto_hwm_context_plan_make (true, profile_, &context_plan);
    zlink::auto_hwm_socket_plan_t socket_plan;
    zlink::auto_hwm_socket_plan_for_role (
      context_plan, role_, socket_type_, managed_connections_, managed_connections_, &socket_plan,
      message_unit_bytes_, -1, -1, false, false, zlink::auto_hwm_scope_none, 1, true, true);
    return socket_plan.sndhwm;
}

zlink::auto_hwm_socket_plan_t planned_connection_bucket_plan (
  zlink::auto_hwm_role_t role_,
  int socket_type_,
  zlink_auto_hwm_profile_t profile_,
  int message_unit_bytes_,
  size_t managed_connections_,
  bool hysteresis_enabled_ = false,
  uint32_t previous_bucket_index_ = zlink::auto_hwm_connection_bucket_none)
{
    zlink::auto_hwm_context_plan_t context_plan;
    zlink::auto_hwm_context_plan_make (true, profile_, &context_plan);
    zlink::auto_hwm_socket_plan_t socket_plan;
    zlink::auto_hwm_socket_plan_for_role (
      context_plan, role_, socket_type_, managed_connections_, managed_connections_, &socket_plan,
      message_unit_bytes_, -1, -1, false, false, zlink::auto_hwm_scope_none, 1, true, true,
      hysteresis_enabled_, previous_bucket_index_);
    return socket_plan;
}

void set_zero_linger (zlink::socket_base_t *socket_)
{
    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      socket_->setsockopt (ZLINK_INTERNAL_OPT_LINGER, &zero, sizeof (zero)));
}

void set_subscribe_all (zlink::socket_base_t *socket_)
{
    TEST_ASSERT_SUCCESS_ERRNO (socket_->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, "", 0));
}

void set_recv_timeout (zlink::socket_base_t *socket_, int timeout_ms_)
{
    TEST_ASSERT_SUCCESS_ERRNO (
      socket_->setsockopt (ZLINK_INTERNAL_OPT_RCVTIMEO, &timeout_ms_, sizeof (timeout_ms_)));
}

void set_send_timeout (zlink::socket_base_t *socket_, int timeout_ms_)
{
    TEST_ASSERT_SUCCESS_ERRNO (
      socket_->setsockopt (ZLINK_INTERNAL_OPT_SNDTIMEO, &timeout_ms_, sizeof (timeout_ms_)));
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
            memcpy (zlink_msg_data (&frames[i]), parts_[i].data (), parts_[i].size ());
    }

    const int rc = zlink::logical_multipart_publish (
      socket_, topic_.c_str (), frames.empty () ? NULL : &frames[0], frames.size (), flags_);
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
        frames_out_->push_back (
          std::string (static_cast<const char *> (frame.data ()), frame.size ()));
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

int send_single_frame (zlink::socket_base_t *socket_, const std::string &payload_, int flags_)
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

void test_mesh_pub_hwm_defaults_follow_transport_and_ready_peer_count ()
{
    TEST_ASSERT_EQUAL_INT (expected_mesh_pub_sndhwm (),
                           zlink::spot_mesh_pub_hwm_t::resolve_default ("tcp://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (expected_mesh_pub_sndhwm (),
                           zlink::spot_mesh_pub_hwm_t::resolve_default ("tcp://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (expected_mesh_pub_sndhwm (),
                           zlink::spot_mesh_pub_hwm_t::resolve_default ("tcp://127.0.0.1:9000", 2));

    TEST_ASSERT_EQUAL_INT (expected_mesh_pub_sndhwm (),
                           zlink::spot_mesh_pub_hwm_t::resolve_default ("ws://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (expected_mesh_pub_sndhwm (),
                           zlink::spot_mesh_pub_hwm_t::resolve_default ("ws://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (expected_mesh_pub_sndhwm (),
                           zlink::spot_mesh_pub_hwm_t::resolve_default ("ws://127.0.0.1:9000", 2));

    TEST_ASSERT_EQUAL_INT (expected_mesh_pub_sndhwm (),
                           zlink::spot_mesh_pub_hwm_t::resolve_default ("tls://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (expected_mesh_pub_sndhwm (),
                           zlink::spot_mesh_pub_hwm_t::resolve_default ("tls://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (expected_mesh_pub_sndhwm (),
                           zlink::spot_mesh_pub_hwm_t::resolve_default ("tls://127.0.0.1:9000", 2));

    TEST_ASSERT_EQUAL_INT (expected_mesh_pub_sndhwm (),
                           zlink::spot_mesh_pub_hwm_t::resolve_default ("wss://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (expected_mesh_pub_sndhwm (),
                           zlink::spot_mesh_pub_hwm_t::resolve_default ("wss://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (expected_mesh_pub_sndhwm (),
                           zlink::spot_mesh_pub_hwm_t::resolve_default ("wss://127.0.0.1:9000", 2));
}

void test_mesh_pub_hwm_refresh_follows_ready_peer_count_changes ()
{
    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_hwm_t::should_refresh ("tls://127.0.0.1:9000", 0, 1));
    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_hwm_t::should_refresh ("wss://127.0.0.1:9000", 0, 2));
    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_hwm_t::should_refresh ("tcp://127.0.0.1:9000", 0, 1));
    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_hwm_t::should_refresh ("tcp://127.0.0.1:9000", 1, 2));
    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_hwm_t::should_refresh ("tls://127.0.0.1:9000", 1, 2));
    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_hwm_t::should_refresh ("tls://127.0.0.1:9000", 2, 100));
    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_hwm_t::should_refresh ("wss://127.0.0.1:9000", 2, 100));
    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_hwm_t::should_refresh ("ws://127.0.0.1:9000", 2, 100));
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_hwm_t::should_refresh ("tls://127.0.0.1:9000", 2, 2));
    TEST_ASSERT_FALSE (
      zlink::spot_mesh_pub_hwm_t::should_refresh ("wss://127.0.0.1:9000", 100, 100));
}

void test_ready_peer_count_is_clamped_to_connected_peers ()
{
    TEST_ASSERT_EQUAL_UINT (2, zlink::clamp_ready_peer_count (100, 2));
    TEST_ASSERT_EQUAL_UINT (1, zlink::clamp_ready_peer_count (1, 2));
    TEST_ASSERT_EQUAL_UINT (0, zlink::clamp_ready_peer_count (7, 0));
}

void test_auto_hwm_v2_profile_caps_and_unit_budgets ()
{
    zlink::auto_hwm_context_plan_t context_plan;
    zlink::auto_hwm_socket_plan_t socket_plan;

    zlink::auto_hwm_context_plan_make (true, ZLINK_AUTO_HWM_PROFILE_BALANCED, &context_plan);
    zlink::auto_hwm_socket_plan_for_role (context_plan, zlink::auto_hwm_role_fanout,
                                          ZLINK_CORE_SOCKET_PUB, 100, 100, &socket_plan, 262144, -1,
                                          -1, false, false, zlink::auto_hwm_scope_none, 1, true);
    TEST_ASSERT_EQUAL_UINT32 (zlink::auto_hwm_policy_fanout, socket_plan.policy_class);
    TEST_ASSERT_EQUAL_UINT64 (256u * 4096u, socket_plan.unit_budget_bytes);
    TEST_ASSERT_EQUAL_UINT32 (1024u, socket_plan.size_cap);
    TEST_ASSERT_EQUAL_INT (4, socket_plan.sndhwm);

    zlink::auto_hwm_context_plan_make (true, ZLINK_AUTO_HWM_PROFILE_COMPACT, &context_plan);
    zlink::auto_hwm_socket_plan_for_role (context_plan, zlink::auto_hwm_role_fanout,
                                          ZLINK_CORE_SOCKET_PUB, 100, 100, &socket_plan, 262144, -1,
                                          -1, false, false, zlink::auto_hwm_scope_none, 1, true);
    TEST_ASSERT_EQUAL_UINT64 (64u * 4096u, socket_plan.unit_budget_bytes);
    TEST_ASSERT_EQUAL_UINT32 (256u, socket_plan.size_cap);
    TEST_ASSERT_EQUAL_INT (1, socket_plan.sndhwm);

    zlink::auto_hwm_context_plan_make (true, ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY, &context_plan);
    zlink::auto_hwm_socket_plan_for_role (context_plan, zlink::auto_hwm_role_fanout,
                                          ZLINK_CORE_SOCKET_PUB, 100, 100, &socket_plan, 262144, -1,
                                          -1, false, false, zlink::auto_hwm_scope_none, 1, true);
    TEST_ASSERT_EQUAL_UINT64 (128u * 4096u, socket_plan.unit_budget_bytes);
    TEST_ASSERT_EQUAL_UINT32 (512u, socket_plan.size_cap);
    TEST_ASSERT_EQUAL_INT (2, socket_plan.sndhwm);

    zlink::auto_hwm_context_plan_make (true, ZLINK_AUTO_HWM_PROFILE_THROUGHPUT, &context_plan);
    zlink::auto_hwm_socket_plan_for_role (context_plan, zlink::auto_hwm_role_fanout,
                                          ZLINK_CORE_SOCKET_PUB, 100, 100, &socket_plan, 262144, -1,
                                          -1, false, false, zlink::auto_hwm_scope_none, 1, true);
    TEST_ASSERT_EQUAL_UINT64 (512u * 4096u, socket_plan.unit_budget_bytes);
    TEST_ASSERT_EQUAL_UINT32 (4096u, socket_plan.size_cap);
    TEST_ASSERT_EQUAL_INT (8, socket_plan.sndhwm);
}

void test_auto_hwm_pc_profile_table_and_message_unit_scaling ()
{
    TEST_ASSERT_EQUAL_INT (64, planned_hwm (zlink::auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER,
                                            ZLINK_AUTO_HWM_PROFILE_COMPACT, 0));
    TEST_ASSERT_EQUAL_INT (256, planned_hwm (zlink::auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER,
                                             ZLINK_AUTO_HWM_PROFILE_BALANCED, 0));
    TEST_ASSERT_EQUAL_INT (256, planned_hwm (zlink::auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER,
                                             ZLINK_AUTO_HWM_PROFILE_BALANCED, 4096));

    TEST_ASSERT_EQUAL_INT (64, planned_hwm (zlink::auto_hwm_role_fanout, ZLINK_CORE_SOCKET_PUB,
                                            ZLINK_AUTO_HWM_PROFILE_COMPACT, 0));
    TEST_ASSERT_EQUAL_INT (128, planned_hwm (zlink::auto_hwm_role_fanout, ZLINK_CORE_SOCKET_PUB,
                                             ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY, 0));
    TEST_ASSERT_EQUAL_INT (256, planned_hwm (zlink::auto_hwm_role_fanout, ZLINK_CORE_SOCKET_PUB,
                                             ZLINK_AUTO_HWM_PROFILE_BALANCED, 0));
    TEST_ASSERT_EQUAL_INT (512, planned_hwm (zlink::auto_hwm_role_fanout, ZLINK_CORE_SOCKET_PUB,
                                             ZLINK_AUTO_HWM_PROFILE_THROUGHPUT, 0));
    TEST_ASSERT_EQUAL_INT (8, planned_hwm (zlink::auto_hwm_role_stream, ZLINK_CORE_SOCKET_STREAM,
                                           ZLINK_AUTO_HWM_PROFILE_COMPACT, 0));
    TEST_ASSERT_EQUAL_INT (16, planned_hwm (zlink::auto_hwm_role_stream, ZLINK_CORE_SOCKET_STREAM,
                                            ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY, 0));
    TEST_ASSERT_EQUAL_INT (64, planned_hwm (zlink::auto_hwm_role_stream, ZLINK_CORE_SOCKET_STREAM,
                                            ZLINK_AUTO_HWM_PROFILE_BALANCED, 0));
    TEST_ASSERT_EQUAL_INT (256, planned_hwm (zlink::auto_hwm_role_stream, ZLINK_CORE_SOCKET_STREAM,
                                             ZLINK_AUTO_HWM_PROFILE_THROUGHPUT, 0));

    TEST_ASSERT_EQUAL_INT (1024, planned_hwm (zlink::auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER,
                                              ZLINK_AUTO_HWM_PROFILE_BALANCED, 64));
    TEST_ASSERT_EQUAL_INT (1024, planned_hwm (zlink::auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER,
                                              ZLINK_AUTO_HWM_PROFILE_BALANCED, 256));
    TEST_ASSERT_EQUAL_INT (1024, planned_hwm (zlink::auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER,
                                              ZLINK_AUTO_HWM_PROFILE_BALANCED, 1024));
    TEST_ASSERT_EQUAL_INT (16, planned_hwm (zlink::auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER,
                                            ZLINK_AUTO_HWM_PROFILE_BALANCED, 65536));
    TEST_ASSERT_EQUAL_INT (8, planned_hwm (zlink::auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER,
                                           ZLINK_AUTO_HWM_PROFILE_BALANCED, 131072));
    TEST_ASSERT_EQUAL_INT (4, planned_hwm (zlink::auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER,
                                           ZLINK_AUTO_HWM_PROFILE_BALANCED, 262144));

    TEST_ASSERT_EQUAL_INT (64, planned_hwm (zlink::auto_hwm_role_stream, ZLINK_CORE_SOCKET_STREAM,
                                            ZLINK_AUTO_HWM_PROFILE_BALANCED, 1024));
    TEST_ASSERT_EQUAL_INT (128, planned_hwm (zlink::auto_hwm_role_stream, ZLINK_CORE_SOCKET_STREAM,
                                             ZLINK_AUTO_HWM_PROFILE_BALANCED, 64));
    TEST_ASSERT_EQUAL_INT (1, planned_hwm (zlink::auto_hwm_role_stream, ZLINK_CORE_SOCKET_STREAM,
                                           ZLINK_AUTO_HWM_PROFILE_BALANCED, 65536));
    TEST_ASSERT_EQUAL_INT (1, planned_hwm (zlink::auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER,
                                           ZLINK_AUTO_HWM_PROFILE_BALANCED, 32 * 1024 * 1024));
}

void test_auto_hwm_v2_policy_class_mapping_and_spot_fanout_limit ()
{
    TEST_ASSERT_EQUAL_UINT32 (zlink::auto_hwm_policy_peer_queue,
                              zlink::auto_hwm_policy_class_for_role (
                                zlink::auto_hwm_role_peer_queue, ZLINK_CORE_SOCKET_DEALER));
    TEST_ASSERT_EQUAL_UINT32 (zlink::auto_hwm_policy_stream,
                              zlink::auto_hwm_policy_class_for_role (zlink::auto_hwm_role_stream,
                                                                     ZLINK_CORE_SOCKET_STREAM));

    zlink::auto_hwm_context_plan_t context_plan;
    zlink::auto_hwm_context_plan_make (true, ZLINK_AUTO_HWM_PROFILE_BALANCED, &context_plan);
    zlink::auto_hwm_socket_plan_t socket_plan;
    zlink::auto_hwm_socket_plan_for_role (
      context_plan, zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB, 10000, 10000,
      &socket_plan, 64, -1, -1, false, false, zlink::auto_hwm_scope_shared, 10000, true);
    TEST_ASSERT_EQUAL_UINT32 (zlink::auto_hwm_policy_spot_data, socket_plan.policy_class);
    TEST_ASSERT_EQUAL_UINT32 (1024u, socket_plan.size_cap);
    TEST_ASSERT_EQUAL_INT (1024, socket_plan.sndhwm);
}

void test_auto_hwm_v2_hwm_is_independent_from_observed_count ()
{
    zlink::auto_hwm_context_plan_t context_plan;
    zlink::auto_hwm_context_plan_make (true, ZLINK_AUTO_HWM_PROFILE_BALANCED, &context_plan);

    zlink::auto_hwm_socket_plan_t socket_plan;
    zlink::auto_hwm_socket_plan_for_role (context_plan, zlink::auto_hwm_role_recv_ingress,
                                          ZLINK_CORE_SOCKET_SUB, 1, 1, &socket_plan, 131072, -1, -1,
                                          false, false, zlink::auto_hwm_scope_per_spot, 1, true);

    TEST_ASSERT_EQUAL_INT (8, socket_plan.rcvhwm);

    zlink::auto_hwm_socket_plan_for_role (
      context_plan, zlink::auto_hwm_role_recv_ingress, ZLINK_CORE_SOCKET_SUB, 10000, 10000,
      &socket_plan, 131072, -1, -1, false, false, zlink::auto_hwm_scope_per_spot, 1, true);

    TEST_ASSERT_EQUAL_INT (8, socket_plan.rcvhwm);

    zlink::auto_hwm_socket_plan_for_role (context_plan, zlink::auto_hwm_role_routed,
                                          ZLINK_CORE_SOCKET_ROUTER, 1, 1, &socket_plan, 64, -1, -1,
                                          false, false, zlink::auto_hwm_scope_shared, 1, true);

    TEST_ASSERT_EQUAL_INT (1024, socket_plan.sndhwm);

    zlink::auto_hwm_socket_plan_for_role (
      context_plan, zlink::auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER, 10000, 10000,
      &socket_plan, 64, -1, -1, false, false, zlink::auto_hwm_scope_shared, 1, true);

    TEST_ASSERT_EQUAL_INT (1024, socket_plan.sndhwm);
}

void test_auto_hwm_connection_bucket_scales_spot_mesh_roles ()
{
    TEST_ASSERT_EQUAL_INT (256, planned_connection_bucket_hwm (
                                  zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                  ZLINK_AUTO_HWM_PROFILE_BALANCED, 4096, 1));
    TEST_ASSERT_EQUAL_INT (256, planned_connection_bucket_hwm (
                                  zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                  ZLINK_AUTO_HWM_PROFILE_BALANCED, 4096, 64));
    TEST_ASSERT_EQUAL_INT (128, planned_connection_bucket_hwm (
                                  zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                  ZLINK_AUTO_HWM_PROFILE_BALANCED, 4096, 65));
    TEST_ASSERT_EQUAL_INT (128, planned_connection_bucket_hwm (
                                  zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                  ZLINK_AUTO_HWM_PROFILE_BALANCED, 4096, 128));
    TEST_ASSERT_EQUAL_INT (64, planned_connection_bucket_hwm (
                                 zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                 ZLINK_AUTO_HWM_PROFILE_BALANCED, 4096, 129));
    TEST_ASSERT_EQUAL_INT (64, planned_connection_bucket_hwm (
                                 zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                 ZLINK_AUTO_HWM_PROFILE_BALANCED, 4096, 512));
    TEST_ASSERT_EQUAL_INT (32, planned_connection_bucket_hwm (
                                 zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                 ZLINK_AUTO_HWM_PROFILE_BALANCED, 4096, 513));
    TEST_ASSERT_EQUAL_INT (32, planned_connection_bucket_hwm (
                                 zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                 ZLINK_AUTO_HWM_PROFILE_BALANCED, 4096, 999));
    TEST_ASSERT_EQUAL_INT (16, planned_connection_bucket_hwm (
                                 zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                 ZLINK_AUTO_HWM_PROFILE_BALANCED, 4096, 2049));

    TEST_ASSERT_EQUAL_INT (8, planned_connection_bucket_hwm (
                                zlink::auto_hwm_role_recv_ingress, ZLINK_CORE_SOCKET_XSUB,
                                ZLINK_AUTO_HWM_PROFILE_COMPACT, 4096, 2049));
    TEST_ASSERT_EQUAL_INT (32, planned_connection_bucket_hwm (
                                 zlink::auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER,
                                 ZLINK_AUTO_HWM_PROFILE_THROUGHPUT, 4096, 2049));
}

void test_auto_hwm_connection_bucket_profiles ()
{
    const size_t peers[] = {1, 64, 65, 128, 129, 512, 513, 999, 2049};
    const int compact[] = {64, 64, 64, 64, 32, 32, 16, 16, 8};
    const int low_latency[] = {128, 128, 64, 64, 32, 32, 16, 16, 8};
    const int balanced[] = {256, 256, 128, 128, 64, 64, 32, 32, 16};
    const int throughput[] = {512, 512, 256, 256, 128, 128, 64, 64, 32};

    for (size_t i = 0; i != sizeof (peers) / sizeof (peers[0]); ++i) {
        TEST_ASSERT_EQUAL_INT (compact[i], planned_connection_bucket_hwm (
                                             zlink::auto_hwm_role_spot_data,
                                             ZLINK_CORE_SOCKET_PUB,
                                             ZLINK_AUTO_HWM_PROFILE_COMPACT, 4096, peers[i]));
        TEST_ASSERT_EQUAL_INT (low_latency[i], planned_connection_bucket_hwm (
                                                 zlink::auto_hwm_role_spot_data,
                                                 ZLINK_CORE_SOCKET_PUB,
                                                 ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY, 4096, peers[i]));
        TEST_ASSERT_EQUAL_INT (balanced[i], planned_connection_bucket_hwm (
                                              zlink::auto_hwm_role_spot_data,
                                              ZLINK_CORE_SOCKET_PUB,
                                              ZLINK_AUTO_HWM_PROFILE_BALANCED, 4096, peers[i]));
        TEST_ASSERT_EQUAL_INT (throughput[i], planned_connection_bucket_hwm (
                                               zlink::auto_hwm_role_spot_data,
                                               ZLINK_CORE_SOCKET_PUB,
                                               ZLINK_AUTO_HWM_PROFILE_THROUGHPUT, 4096, peers[i]));
    }
}

void test_auto_hwm_connection_bucket_preserves_4k_byte_budget ()
{
    TEST_ASSERT_EQUAL_INT (128, planned_connection_bucket_hwm (
                                  zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                  ZLINK_AUTO_HWM_PROFILE_BALANCED, 1024, 999));
    TEST_ASSERT_EQUAL_INT (32, planned_connection_bucket_hwm (
                                 zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                 ZLINK_AUTO_HWM_PROFILE_BALANCED, 4096, 999));
    TEST_ASSERT_EQUAL_INT (2, planned_connection_bucket_hwm (
                                zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                ZLINK_AUTO_HWM_PROFILE_BALANCED, 64 * 1024, 999));
    TEST_ASSERT_EQUAL_INT (1, planned_connection_bucket_hwm (
                                zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                ZLINK_AUTO_HWM_PROFILE_BALANCED, 128 * 1024, 999));
}

void test_auto_hwm_connection_bucket_hysteresis_boundaries ()
{
    zlink::auto_hwm_socket_plan_t socket_plan = planned_connection_bucket_plan (
      zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB, ZLINK_AUTO_HWM_PROFILE_BALANCED,
      4096, 65, true, 0);
    TEST_ASSERT_EQUAL_UINT32 (0u, socket_plan.connection_bucket_index);
    TEST_ASSERT_TRUE (socket_plan.connection_bucket_hysteresis_retained);
    TEST_ASSERT_EQUAL_INT (256, socket_plan.sndhwm);

    socket_plan = planned_connection_bucket_plan (
      zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB, ZLINK_AUTO_HWM_PROFILE_BALANCED,
      4096, 79, true, 0);
    TEST_ASSERT_EQUAL_UINT32 (0u, socket_plan.connection_bucket_index);
    TEST_ASSERT_TRUE (socket_plan.connection_bucket_hysteresis_retained);
    TEST_ASSERT_EQUAL_INT (256, socket_plan.sndhwm);

    socket_plan = planned_connection_bucket_plan (
      zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB, ZLINK_AUTO_HWM_PROFILE_BALANCED,
      4096, 80, true, 0);
    TEST_ASSERT_EQUAL_UINT32 (1u, socket_plan.connection_bucket_index);
    TEST_ASSERT_FALSE (socket_plan.connection_bucket_hysteresis_retained);
    TEST_ASSERT_EQUAL_INT (128, socket_plan.sndhwm);

    socket_plan = planned_connection_bucket_plan (
      zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB, ZLINK_AUTO_HWM_PROFILE_BALANCED,
      4096, 49, true, 1);
    TEST_ASSERT_EQUAL_UINT32 (1u, socket_plan.connection_bucket_index);
    TEST_ASSERT_TRUE (socket_plan.connection_bucket_hysteresis_retained);
    TEST_ASSERT_EQUAL_INT (128, socket_plan.sndhwm);

    socket_plan = planned_connection_bucket_plan (
      zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB, ZLINK_AUTO_HWM_PROFILE_BALANCED,
      4096, 48, true, 1);
    TEST_ASSERT_EQUAL_UINT32 (0u, socket_plan.connection_bucket_index);
    TEST_ASSERT_FALSE (socket_plan.connection_bucket_hysteresis_retained);
    TEST_ASSERT_EQUAL_INT (256, socket_plan.sndhwm);
}

void test_auto_hwm_connection_bucket_is_opt_in_and_preserves_manual_buffers ()
{
    zlink::auto_hwm_context_plan_t context_plan;
    zlink::auto_hwm_context_plan_make (true, ZLINK_AUTO_HWM_PROFILE_BALANCED, &context_plan);

    zlink::auto_hwm_socket_plan_t socket_plan;
    zlink::auto_hwm_socket_plan_for_role (
      context_plan, zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB, 999, 999, &socket_plan,
      4096, -1, -1, false, false, zlink::auto_hwm_scope_none, 1, true, false);
    TEST_ASSERT_EQUAL_INT (256, socket_plan.sndhwm);
    TEST_ASSERT_FALSE (socket_plan.connection_bucket_enabled);

    zlink::auto_hwm_socket_plan_for_role (
      context_plan, zlink::auto_hwm_role_fanout, ZLINK_CORE_SOCKET_PUB, 999, 999, &socket_plan,
      4096, -1, -1, false, false, zlink::auto_hwm_scope_none, 1, true, true);
    TEST_ASSERT_EQUAL_INT (256, socket_plan.sndhwm);
    TEST_ASSERT_FALSE (socket_plan.connection_bucket_enabled);

    zlink::auto_hwm_socket_plan_for_role (
      context_plan, zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB, 999, 999, &socket_plan,
      4096, -1, -1, false, false, zlink::auto_hwm_scope_none, 1, false, true);
    TEST_ASSERT_EQUAL_INT (256, socket_plan.sndhwm);
    TEST_ASSERT_FALSE (socket_plan.connection_bucket_enabled);

    zlink::auto_hwm_socket_plan_for_role (
      context_plan, zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB, 999, 999, &socket_plan,
      4096, -1, -1, false, false, zlink::auto_hwm_scope_none, 1, true, true);
    TEST_ASSERT_EQUAL_INT (32, socket_plan.sndhwm);
    TEST_ASSERT_TRUE (socket_plan.connection_bucket_enabled);

    zlink::auto_hwm_socket_plan_for_role (
      context_plan, zlink::auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB, 999, 999, &socket_plan,
      4096, 1024 * 1024, 2 * 1024 * 1024, true, true, zlink::auto_hwm_scope_none, 1, true, true);
    TEST_ASSERT_EQUAL_INT (32, socket_plan.sndhwm);
    TEST_ASSERT_EQUAL_INT (1024 * 1024, socket_plan.requested_sndbuf);
    TEST_ASSERT_EQUAL_INT (2 * 1024 * 1024, socket_plan.requested_rcvbuf);
}

void test_auto_hwm_connection_bucket_caps_spot_routed_latency_floor ()
{
    zlink::ctx_t ctx;

    const zlink::spot_internal_auto_hwm_policy_t bucket_policy = {
      zlink::auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER, 999, 999, 0, 0, true, true,
      zlink::auto_hwm_scope_shared, 1, 4096, true};
    const zlink::auto_hwm_socket_plan_t bucket_plan =
      zlink::spot_internal_auto_hwm_plan (&ctx, bucket_policy);
    TEST_ASSERT_EQUAL_INT (32, bucket_plan.sndhwm);
    TEST_ASSERT_EQUAL_INT (32, bucket_plan.rcvhwm);

    const zlink::spot_internal_auto_hwm_policy_t legacy_policy = {
      zlink::auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER, 999, 999, 0, 0, true, true,
      zlink::auto_hwm_scope_shared, 1, 4096, false};
    const zlink::auto_hwm_socket_plan_t legacy_plan =
      zlink::spot_internal_auto_hwm_plan (&ctx, legacy_policy);
    TEST_ASSERT_EQUAL_INT (256, legacy_plan.sndhwm);
    TEST_ASSERT_EQUAL_INT (256, legacy_plan.rcvhwm);
}

void expect_socket_hwm_and_bucket (zlink::socket_base_t *socket_,
                                   int expected_sndhwm_,
                                   int expected_rcvhwm_,
                                   uint32_t expected_count_,
                                   uint32_t expected_bucket_index_,
                                   uint32_t expected_bucket_hwm_4k_,
                                   bool expected_hysteresis_retained_,
                                   uint64_t expected_message_bytes_)
{
    TEST_ASSERT_NOT_NULL (socket_);
    int value = -1;
    size_t value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (
      socket_->getsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (expected_sndhwm_, value);
    value = -1;
    value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (
      socket_->getsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (expected_rcvhwm_, value);

    zlink_monitor_status_t status;
    TEST_ASSERT_SUCCESS_ERRNO (socket_->monitor_snapshot (&status));
    TEST_ASSERT_EQUAL_UINT32 (1u, status.auto_hwm_connection_bucket_enabled);
    TEST_ASSERT_EQUAL_UINT32 (expected_count_, status.auto_hwm_connection_bucket_count);
    TEST_ASSERT_EQUAL_UINT32 (expected_bucket_index_, status.auto_hwm_connection_bucket_index);
    TEST_ASSERT_EQUAL_UINT32 (expected_bucket_hwm_4k_, status.auto_hwm_connection_bucket_hwm_4k);
    TEST_ASSERT_EQUAL_UINT32 (expected_hysteresis_retained_ ? 1u : 0u,
                              status.auto_hwm_connection_bucket_hysteresis_retained);
    TEST_ASSERT_EQUAL_UINT64 (expected_message_bytes_, status.auto_hwm_effective_message_bytes);
    TEST_ASSERT_EQUAL_INT (expected_sndhwm_, status.auto_hwm_applied_sndhwm);
    TEST_ASSERT_EQUAL_INT (expected_rcvhwm_, status.auto_hwm_applied_rcvhwm);
}

void test_apply_spot_internal_auto_hwm_records_runtime_bucket_state ()
{
    zlink::ctx_t *ctx = new zlink::ctx_t;
    TEST_ASSERT_NOT_NULL (ctx);
    zlink::socket_base_t *socket = ctx->create_socket (ZLINK_CORE_SOCKET_ROUTER);
    TEST_ASSERT_NOT_NULL (socket);
    set_zero_linger (socket);

    zlink::spot_internal_auto_hwm_policy_t policy = {
      zlink::auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER, 64, 64, 0, 0, true, true,
      zlink::auto_hwm_scope_shared, 1, 4096, true};

    zlink::apply_spot_internal_auto_hwm (ctx, socket, policy);
    expect_socket_hwm_and_bucket (socket, 256, 256, 64, 0, 256, false, 4096);

    policy.managed_connections = 65;
    policy.active_connections = 65;
    zlink::apply_spot_internal_auto_hwm (ctx, socket, policy);
    expect_socket_hwm_and_bucket (socket, 256, 256, 65, 0, 256, true, 4096);

    policy.managed_connections = 80;
    policy.active_connections = 80;
    zlink::apply_spot_internal_auto_hwm (ctx, socket, policy);
    expect_socket_hwm_and_bucket (socket, 128, 128, 80, 1, 128, false, 4096);

    policy.managed_connections = 49;
    policy.active_connections = 49;
    zlink::apply_spot_internal_auto_hwm (ctx, socket, policy);
    expect_socket_hwm_and_bucket (socket, 128, 128, 49, 1, 128, true, 4096);

    policy.managed_connections = 48;
    policy.active_connections = 48;
    zlink::apply_spot_internal_auto_hwm (ctx, socket, policy);
    expect_socket_hwm_and_bucket (socket, 256, 256, 48, 0, 256, false, 4096);

    policy.managed_connections = 999;
    policy.active_connections = 999;
    policy.message_unit_bytes = 64 * 1024;
    zlink::apply_spot_internal_auto_hwm (ctx, socket, policy);
    expect_socket_hwm_and_bucket (socket, 2, 2, 999, 3, 32, false, 64 * 1024);

    TEST_ASSERT_SUCCESS_ERRNO (ctx->close_socket_and_wait (socket, 1000));
    TEST_ASSERT_SUCCESS_ERRNO (ctx->terminate ());
}

void test_auto_hwm_leaves_transport_buffers_unset ()
{
    zlink::auto_hwm_context_plan_t context_plan;
    zlink::auto_hwm_socket_plan_t socket_plan;

    zlink::auto_hwm_context_plan_make (true, ZLINK_AUTO_HWM_PROFILE_COMPACT, &context_plan);
    zlink::auto_hwm_socket_plan_for_role (context_plan, zlink::auto_hwm_role_routed,
                                          ZLINK_CORE_SOCKET_ROUTER, 1, 1, &socket_plan, 64, -1, -1,
                                          false, false, zlink::auto_hwm_scope_shared, 1, true);
    TEST_ASSERT_EQUAL_INT (-1, socket_plan.requested_sndbuf);
    TEST_ASSERT_EQUAL_INT (-1, socket_plan.requested_rcvbuf);

    zlink::auto_hwm_context_plan_make (true, ZLINK_AUTO_HWM_PROFILE_BALANCED, &context_plan);
    zlink::auto_hwm_socket_plan_for_role (context_plan, zlink::auto_hwm_role_routed,
                                          ZLINK_CORE_SOCKET_ROUTER, 1, 1, &socket_plan, 64, -1, -1,
                                          false, false, zlink::auto_hwm_scope_shared, 1, true);
    TEST_ASSERT_EQUAL_INT (-1, socket_plan.requested_sndbuf);
    TEST_ASSERT_EQUAL_INT (-1, socket_plan.requested_rcvbuf);

    zlink::auto_hwm_context_plan_make (true, ZLINK_AUTO_HWM_PROFILE_COMPACT, &context_plan);
    zlink::auto_hwm_socket_plan_for_role (context_plan, zlink::auto_hwm_role_routed,
                                          ZLINK_CORE_SOCKET_ROUTER, 1, 1, &socket_plan, 65536, -1,
                                          -1, false, false, zlink::auto_hwm_scope_shared, 1, true);
    TEST_ASSERT_EQUAL_INT (-1, socket_plan.requested_sndbuf);
    TEST_ASSERT_EQUAL_INT (-1, socket_plan.requested_rcvbuf);

    zlink::auto_hwm_socket_plan_for_role (
      context_plan, zlink::auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER, 1, 1, &socket_plan, 64,
      1024 * 1024, 2 * 1024 * 1024, true, true, zlink::auto_hwm_scope_shared, 1, true);
    TEST_ASSERT_EQUAL_INT (1024 * 1024, socket_plan.requested_sndbuf);
    TEST_ASSERT_EQUAL_INT (2 * 1024 * 1024, socket_plan.requested_rcvbuf);
}

void test_mesh_pub_hwm_hint_updates_private_runtime_owner ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.bound_endpoint = "wss://127.0.0.1:9000";

    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_hwm_t::publish_ready_hint (&runtime, 1));
    TEST_ASSERT_EQUAL_UINT (1,
                            zlink::mesh_pub_ready_peer_count (&runtime.execution.mesh_peer_state));
    TEST_ASSERT_EQUAL_UINT64 (1, zlink::mesh_pub_hwm_version (&runtime.execution.mesh_peer_state));

    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_hwm_t::publish_ready_hint (&runtime, 2));
    TEST_ASSERT_EQUAL_UINT (2,
                            zlink::mesh_pub_ready_peer_count (&runtime.execution.mesh_peer_state));
    TEST_ASSERT_EQUAL_UINT64 (2, zlink::mesh_pub_hwm_version (&runtime.execution.mesh_peer_state));

    zlink::spot_mesh_pub_hwm_t::reset_runtime_state (&runtime);
    TEST_ASSERT_EQUAL_UINT (0,
                            zlink::mesh_pub_ready_peer_count (&runtime.execution.mesh_peer_state));
    TEST_ASSERT_EQUAL_UINT64 (3, zlink::mesh_pub_hwm_version (&runtime.execution.mesh_peer_state));
}

void test_mesh_pub_hwm_runtime_owner_uses_bound_endpoint ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.bound_endpoint = "wss://127.0.0.1:9000";

    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_hwm_t::publish_ready_hint (&runtime, 2));
    TEST_ASSERT_EQUAL_INT (0, zlink::spot_mesh_pub_hwm_t::resolve_runtime_default (&runtime));
    zlink::spot_node_runtime_tuning_t hwm = runtime.runtime_tuning_snapshot ();
    hwm.pubsub_hwm_override = 77;
    runtime.set_runtime_tuning (hwm);
    TEST_ASSERT_EQUAL_INT (77, zlink::spot_mesh_pub_hwm_t::resolve_runtime_default (&runtime));
    TEST_ASSERT_EQUAL_INT (
      expected_mesh_pub_sndhwm (),
      zlink::spot_mesh_pub_hwm_t::resolve_initial_bind_sndhwm (&runtime, runtime.bound_endpoint));

    zlink::spot_mesh_pub_hwm_t::reset_runtime_state (&runtime);
    TEST_ASSERT_EQUAL_UINT (0,
                            zlink::mesh_pub_ready_peer_count (&runtime.execution.mesh_peer_state));
}

void test_mesh_pub_hwm_runtime_owner_tracks_ready_count_changes ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.bound_endpoint = "tls://127.0.0.1:9000";

    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_hwm_t::publish_ready_hint (&runtime, 1));
    TEST_ASSERT_EQUAL_UINT64 (1, zlink::mesh_pub_hwm_version (&runtime.execution.mesh_peer_state));

    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_hwm_t::publish_ready_hint (&runtime, 2));
    TEST_ASSERT_EQUAL_UINT (2,
                            zlink::mesh_pub_ready_peer_count (&runtime.execution.mesh_peer_state));
    TEST_ASSERT_EQUAL_UINT64 (2, zlink::mesh_pub_hwm_version (&runtime.execution.mesh_peer_state));
    TEST_ASSERT_EQUAL_INT (0, zlink::spot_mesh_pub_hwm_t::resolve_runtime_default (&runtime));
}

void test_spot_node_hwm_options_round_trip_public_api ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);

    int router_profile = ZLINK_AUTO_HWM_PROFILE_COMPACT;
    int pubsub_profile = ZLINK_AUTO_HWM_PROFILE_THROUGHPUT;
    int router_hwm = 111;
    int pubsub_hwm = 222;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE, &router_profile, sizeof (router_profile)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE, &pubsub_profile, sizeof (pubsub_profile)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (node, ZLINK_SPOT_NODE_OPT_ROUTER_HWM,
                                                           &router_hwm, sizeof (router_hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_spot_node_option (node, ZLINK_SPOT_NODE_OPT_PUBSUB_HWM,
                                                           &pubsub_hwm, sizeof (pubsub_hwm)));

    int value = 0;
    size_t value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (ZLINK_AUTO_HWM_PROFILE_COMPACT, value);
    value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_spot_node_option (
      node, ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (ZLINK_AUTO_HWM_PROFILE_THROUGHPUT, value);
    value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_spot_node_option (node, ZLINK_SPOT_NODE_OPT_ROUTER_HWM, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (111, value);
    value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_spot_node_option (node, ZLINK_SPOT_NODE_OPT_PUBSUB_HWM, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (222, value);

    int reset = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_spot_node_option (node, ZLINK_SPOT_NODE_OPT_ROUTER_HWM, &reset, sizeof (reset)));
    value_size = sizeof (value);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_spot_node_option (node, ZLINK_SPOT_NODE_OPT_ROUTER_HWM, &value, &value_size));
    TEST_ASSERT_EQUAL_INT (64, value);

    int invalid = -1;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_ARGUMENT,
                           zlink_set_spot_node_option (node, ZLINK_SPOT_NODE_OPT_PUBSUB_HWM,
                                                       &invalid, sizeof (invalid)));
    invalid = 999;
    TEST_ASSERT_EQUAL_INT (ZLINK_CONFIG_INVALID_ARGUMENT,
                           zlink_set_spot_node_option (node, ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE,
                                                       &invalid, sizeof (invalid)));

    const int removed_option = 0x3608;
    value_size = sizeof (value);
    TEST_ASSERT_EQUAL_INT (
      ZLINK_CONFIG_INVALID_ARGUMENT,
      zlink_get_spot_node_option (node, static_cast<zlink_spot_node_option_t> (removed_option),
                                  &value, &value_size));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_spot_node_hwm_options_expose_defaults ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);
    void *node = zlink_spot_node_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (node);

    struct expected_option_t
    {
        zlink_spot_node_option_t option;
        int expected;
    };
    const expected_option_t options[] = {
      {ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE, ZLINK_AUTO_HWM_PROFILE_BALANCED},
      {ZLINK_SPOT_NODE_OPT_ROUTER_HWM, 256},
      {ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE, ZLINK_AUTO_HWM_PROFILE_BALANCED},
      {ZLINK_SPOT_NODE_OPT_PUBSUB_HWM, 256},
    };

    for (size_t i = 0; i < sizeof (options) / sizeof (options[0]); ++i) {
        int value = -1;
        size_t value_size = sizeof (value);
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_get_spot_node_option (node, options[i].option, &value, &value_size));
        TEST_ASSERT_EQUAL_UINT (sizeof (value), value_size);
        TEST_ASSERT_EQUAL_INT (options[i].expected, value);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

}

int main (int argc, char **argv)
{
    UNITY_BEGIN ();
    RUN_TEST (test_mesh_pub_hwm_defaults_follow_transport_and_ready_peer_count);
    RUN_TEST (test_mesh_pub_hwm_refresh_follows_ready_peer_count_changes);
    RUN_TEST (test_ready_peer_count_is_clamped_to_connected_peers);
    RUN_TEST (test_auto_hwm_v2_profile_caps_and_unit_budgets);
    RUN_TEST (test_auto_hwm_pc_profile_table_and_message_unit_scaling);
    RUN_TEST (test_auto_hwm_v2_policy_class_mapping_and_spot_fanout_limit);
    RUN_TEST (test_auto_hwm_v2_hwm_is_independent_from_observed_count);
    RUN_TEST (test_auto_hwm_connection_bucket_scales_spot_mesh_roles);
    RUN_TEST (test_auto_hwm_connection_bucket_profiles);
    RUN_TEST (test_auto_hwm_connection_bucket_preserves_4k_byte_budget);
    RUN_TEST (test_auto_hwm_connection_bucket_hysteresis_boundaries);
    RUN_TEST (test_auto_hwm_connection_bucket_is_opt_in_and_preserves_manual_buffers);
    RUN_TEST (test_auto_hwm_connection_bucket_caps_spot_routed_latency_floor);
    RUN_TEST (test_apply_spot_internal_auto_hwm_records_runtime_bucket_state);
    RUN_TEST (test_auto_hwm_leaves_transport_buffers_unset);
    RUN_TEST (test_mesh_pub_hwm_hint_updates_private_runtime_owner);
    RUN_TEST (test_mesh_pub_hwm_runtime_owner_uses_bound_endpoint);
    RUN_TEST (test_mesh_pub_hwm_runtime_owner_tracks_ready_count_changes);
    RUN_TEST (test_spot_node_hwm_options_round_trip_public_api);
    RUN_TEST (test_spot_node_hwm_options_expose_defaults);
    return UNITY_END ();
}
