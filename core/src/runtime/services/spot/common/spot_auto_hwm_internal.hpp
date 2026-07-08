/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_AUTO_HWM_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_AUTO_HWM_INTERNAL_HPP_INCLUDED__

#include "core/auto_hwm_policy.hpp"
#include "core/internal_defs.hpp"
#include "core/ctx.hpp"
#include "services/spot/runtime/spot_runtime_hwm.hpp"
#include "sockets/common/socket_base.hpp"

#include <algorithm>

namespace zlink
{
namespace spot_auto_hwm_internal
{
inline uint32_t spot_routed_small_message_cap (zlink_auto_hwm_profile_t profile_)
{
    return auto_hwm_profile_routed_small_message_cap (profile_);
}

inline void apply_spot_routed_latency_floor (auto_hwm_socket_plan_t *socket_plan_,
                                             zlink_auto_hwm_profile_t profile_)
{
    if (!socket_plan_ || socket_plan_->policy_class != auto_hwm_policy_routed
        || socket_plan_->effective_message_bytes > 16ull * 1024ull)
        return;
    if (socket_plan_->connection_bucket_enabled)
        return;

    const uint32_t target_cap = spot_routed_small_message_cap (profile_);
    if (socket_plan_->size_cap < target_cap)
        socket_plan_->size_cap = target_cap;
    if (socket_plan_->sndhwm > 0 && socket_plan_->sndhwm < static_cast<int> (target_cap))
        socket_plan_->sndhwm = static_cast<int> (target_cap);
    if (socket_plan_->rcvhwm > 0 && socket_plan_->rcvhwm < static_cast<int> (target_cap))
        socket_plan_->rcvhwm = static_cast<int> (target_cap);
}

inline int current_socket_sndhwm (socket_base_t *socket_)
{
    if (!socket_)
        return 0;

    int value = 0;
    size_t value_size = sizeof (value);
    if (socket_->getsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &value, &value_size) == 0
        && value_size == sizeof (value)) {
        return value;
    }
    return 0;
}
}

struct spot_internal_auto_hwm_policy_t
{
    auto_hwm_role_t role;
    int socket_type;
    size_t managed_connections;
    size_t active_connections;
    int sndhwm_floor;
    int rcvhwm_floor;
    bool apply_sndhwm;
    bool apply_rcvhwm;
    auto_hwm_scope_t scope;
    size_t scope_count;
    int message_unit_bytes;
    bool connection_bucket_enabled;
};

inline auto_hwm_socket_plan_t spot_internal_auto_hwm_plan (
  ctx_t *ctx_,
  const spot_internal_auto_hwm_policy_t &policy_,
  bool connection_bucket_hysteresis_enabled_ = false,
  uint32_t previous_connection_bucket_index_ = auto_hwm_connection_bucket_none)
{
    const zlink_auto_hwm_profile_t profile =
      ctx_ ? ctx_->auto_hwm_profile () : ZLINK_CTX_AUTO_HWM_PROFILE_DFLT;
    auto_hwm_context_plan_t context_plan;
    auto_hwm_context_plan_make (ctx_ && ctx_->get (ZLINK_CTX_OPT_AUTO_HWM_ENABLE) != 0, profile,
                                &context_plan, ctx_ ? ctx_->auto_hwm_msg_unit_bytes () : 0);

    auto_hwm_socket_plan_t socket_plan;
    const auto_hwm_scope_t scope =
      policy_.scope == auto_hwm_scope_none ? auto_hwm_scope_shared : policy_.scope;
    size_t scope_count = policy_.scope_count;
    if (scope_count == 0)
        scope_count = std::max<size_t> (
          std::max<size_t> (policy_.managed_connections, policy_.active_connections), 1u);
    auto_hwm_socket_plan_for_role (
      context_plan, policy_.role, policy_.socket_type, policy_.managed_connections,
      policy_.active_connections, &socket_plan,
      policy_.message_unit_bytes > 0 ? policy_.message_unit_bytes : context_plan.message_unit_bytes,
      -1, -1, false, false, scope, scope_count, true, policy_.connection_bucket_enabled,
      connection_bucket_hysteresis_enabled_, previous_connection_bucket_index_);
    spot_auto_hwm_internal::apply_spot_routed_latency_floor (&socket_plan, profile);
    return socket_plan;
}

inline int spot_internal_auto_hwm_default_hwm (ctx_t *ctx_,
                                               auto_hwm_role_t role_,
                                               int socket_type_,
                                               bool recv_side_,
                                               int floor_ = 0,
                                               size_t managed_connections_ = 0,
                                               size_t active_connections_ = 0)
{
    const spot_internal_auto_hwm_policy_t policy = {role_,
                                                    socket_type_,
                                                    managed_connections_,
                                                    active_connections_,
                                                    floor_,
                                                    floor_,
                                                    false,
                                                    false,
                                                    auto_hwm_scope_none,
                                                    1,
                                                    0,
                                                    false};
    const auto_hwm_socket_plan_t socket_plan = spot_internal_auto_hwm_plan (ctx_, policy);
    const int value = recv_side_ ? socket_plan.rcvhwm : socket_plan.sndhwm;
    return std::max (floor_, value);
}

inline void apply_spot_internal_auto_hwm (ctx_t *ctx_,
                                          socket_base_t *socket_,
                                          const spot_internal_auto_hwm_policy_t &policy_)
{
    if (!ctx_ || !socket_)
        return;

    const zlink_auto_hwm_profile_t profile = ctx_->auto_hwm_profile ();
    const int configured_message_unit = policy_.message_unit_bytes > 0
                                          ? policy_.message_unit_bytes
                                          : ctx_->auto_hwm_msg_unit_bytes ();
    const uint64_t effective_message_bytes =
      configured_message_unit > 0 ? static_cast<uint64_t> (configured_message_unit) : 4096ull;

    bool hysteresis_enabled = false;
    uint32_t previous_bucket_index = auto_hwm_connection_bucket_none;
    if (policy_.connection_bucket_enabled) {
        zlink_auto_hwm_profile_t previous_profile = ZLINK_AUTO_HWM_PROFILE_BALANCED;
        uint64_t previous_message_bytes = 0;
        hysteresis_enabled =
          socket_->auto_hwm_connection_bucket_state (&previous_bucket_index, &previous_profile,
                                                     &previous_message_bytes)
          && previous_profile == profile && previous_message_bytes == effective_message_bytes;
    }

    const auto_hwm_socket_plan_t socket_plan =
      spot_internal_auto_hwm_plan (ctx_, policy_, hysteresis_enabled, previous_bucket_index);
    auto_hwm_context_plan_t context_plan;
    auto_hwm_context_plan_make (ctx_->auto_hwm_enabled (), profile, &context_plan,
                                configured_message_unit);
    const auto_hwm_scope_t scope =
      policy_.scope == auto_hwm_scope_none ? auto_hwm_scope_shared : policy_.scope;
    size_t scope_count = policy_.scope_count;
    if (scope_count == 0)
        scope_count = std::max<size_t> (
          std::max<size_t> (policy_.managed_connections, policy_.active_connections), 1u);
    socket_->set_auto_hwm_role (policy_.role);
    socket_->set_auto_hwm_scope (scope, scope_count);
    const int sndhwm = socket_plan.sndhwm;
    const int rcvhwm = socket_plan.rcvhwm;

    if (policy_.apply_sndhwm) {
        (void) socket_->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &sndhwm, sizeof (sndhwm));
    }
    if (policy_.apply_rcvhwm) {
        (void) socket_->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &rcvhwm, sizeof (rcvhwm));
    }
    socket_->record_auto_hwm_socket_plan (context_plan, socket_plan,
                                          ZLINK_AUTO_HWM_RECALC_REASON_NONE);
    if (socket_plan.connection_bucket_enabled
        && socket_plan.connection_bucket_index != auto_hwm_connection_bucket_none) {
        socket_->set_auto_hwm_connection_bucket_state (socket_plan.connection_bucket_index, profile,
                                                       socket_plan.effective_message_bytes);
    } else {
        socket_->clear_auto_hwm_connection_bucket_state ();
    }
    socket_->clear_auto_hwm_manual_overrides (policy_.apply_sndhwm, policy_.apply_rcvhwm, false,
                                              false);
}

inline void apply_spot_runtime_hwm_policy (ctx_t *ctx_,
                                           const spot_node_runtime_tuning_t &hwm_,
                                           size_t local_sub_count_,
                                           size_t connected_peer_count_,
                                           size_t active_peer_count_,
                                           int message_unit_bytes_,
                                           socket_base_t *mesh_pub_,
                                           socket_base_t *local_fanout_xpub_,
                                           socket_base_t *mesh_xsub_,
                                           socket_base_t *routed_router_,
                                           int *current_mesh_pub_hwm_out_)
{
    const bool pubsub_hwm_override = spot_node_pubsub_hwm_overridden (hwm_);
    const bool router_hwm_override = spot_node_router_hwm_overridden (hwm_);

    if (mesh_pub_) {
        if (pubsub_hwm_override) {
            const int desired = spot_node_pubsub_admission_hwm (hwm_);
            if (desired > 0
                && mesh_pub_->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &desired, sizeof (desired))
                     == 0
                && current_mesh_pub_hwm_out_) {
                *current_mesh_pub_hwm_out_ = desired;
            }
        } else {
            apply_spot_internal_auto_hwm (
              ctx_, mesh_pub_,
              spot_internal_auto_hwm_policy_t{auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                              connected_peer_count_, active_peer_count_, 0, 0, true,
                                              false, auto_hwm_scope_shared, 1, message_unit_bytes_,
                                              true});
            if (current_mesh_pub_hwm_out_)
                *current_mesh_pub_hwm_out_ =
                  spot_auto_hwm_internal::current_socket_sndhwm (mesh_pub_);
        }
    }

    if (local_fanout_xpub_) {
        apply_spot_internal_auto_hwm (
          ctx_, local_fanout_xpub_,
          spot_internal_auto_hwm_policy_t{auto_hwm_role_spot_data, ZLINK_CORE_SOCKET_PUB,
                                          local_sub_count_, local_sub_count_, 0, 0, true, false,
                                          auto_hwm_scope_shared, 1, message_unit_bytes_, false});
        const int zero = 0;
        (void) local_fanout_xpub_->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &zero, sizeof (zero));
    }

    if (mesh_xsub_) {
        if (pubsub_hwm_override) {
            const int desired = spot_node_pubsub_admission_hwm (hwm_);
            if (desired > 0)
                (void) mesh_xsub_->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &desired,
                                               sizeof (desired));
        } else {
            apply_spot_internal_auto_hwm (
              ctx_, mesh_xsub_,
              spot_internal_auto_hwm_policy_t{auto_hwm_role_recv_ingress, ZLINK_CORE_SOCKET_XSUB,
                                              connected_peer_count_, active_peer_count_, 0, 0,
                                              false, true, auto_hwm_scope_shared, 1,
                                              message_unit_bytes_, true});
        }
    }

    if (routed_router_) {
        if (router_hwm_override) {
            const int desired = spot_node_router_admission_hwm (hwm_);
            if (desired > 0) {
                (void) routed_router_->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &desired,
                                                   sizeof (desired));
                (void) routed_router_->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &desired,
                                                   sizeof (desired));
            }
        } else {
            apply_spot_internal_auto_hwm (
              ctx_, routed_router_,
              spot_internal_auto_hwm_policy_t{auto_hwm_role_routed, ZLINK_CORE_SOCKET_ROUTER,
                                              connected_peer_count_, active_peer_count_, 0, 0, true,
                                              true, auto_hwm_scope_shared, 1, message_unit_bytes_,
                                              true});
        }
    }
}
}

#endif
