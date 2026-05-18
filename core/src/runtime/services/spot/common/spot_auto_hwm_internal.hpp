/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_AUTO_HWM_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_AUTO_HWM_INTERNAL_HPP_INCLUDED__

#include "core/auto_hwm_policy.hpp"
#include "core/internal_defs.hpp"
#include "core/ctx.hpp"
#include "sockets/common/socket_base.hpp"

#include <algorithm>

namespace zlink
{
namespace spot_auto_hwm_internal
{
inline uint32_t spot_routed_small_message_cap (
  zlink_auto_hwm_profile_t profile_)
{
    switch (profile_) {
        case ZLINK_AUTO_HWM_PROFILE_COMPACT:
            return 32;
        case ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY:
            return 64;
        case ZLINK_AUTO_HWM_PROFILE_THROUGHPUT:
            return 128;
        case ZLINK_AUTO_HWM_PROFILE_BALANCED:
        default:
            return 128;
    }
}

inline void apply_spot_routed_latency_floor (
  auto_hwm_socket_plan_t *socket_plan_,
  zlink_auto_hwm_profile_t profile_)
{
    if (!socket_plan_
        || socket_plan_->policy_class != auto_hwm_policy_routed
        || socket_plan_->effective_message_bytes > 16ull * 1024ull)
        return;

    const uint32_t target_cap = spot_routed_small_message_cap (profile_);
    if (socket_plan_->size_cap < target_cap)
        socket_plan_->size_cap = target_cap;
    if (socket_plan_->sndhwm > 0
        && socket_plan_->sndhwm < static_cast<int> (target_cap))
        socket_plan_->sndhwm = static_cast<int> (target_cap);
    if (socket_plan_->rcvhwm > 0
        && socket_plan_->rcvhwm < static_cast<int> (target_cap))
        socket_plan_->rcvhwm = static_cast<int> (target_cap);
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
    bool apply_sndbuf;
    bool apply_rcvbuf;
    auto_hwm_scope_t scope;
    size_t scope_count;
    int message_unit_bytes;
};

inline auto_hwm_socket_plan_t spot_internal_auto_hwm_plan (
  ctx_t *ctx_,
  const spot_internal_auto_hwm_policy_t &policy_)
{
    const zlink_auto_hwm_profile_t profile =
      ctx_ ? ctx_->auto_hwm_profile () : ZLINK_CTX_AUTO_HWM_PROFILE_DFLT;
    auto_hwm_context_plan_t context_plan;
    auto_hwm_context_plan_make (
      ctx_ && ctx_->get (ZLINK_CTX_OPT_AUTO_HWM_ENABLE) != 0, profile,
      &context_plan, ctx_ ? ctx_->auto_hwm_msg_unit_bytes () : 0);

    auto_hwm_socket_plan_t socket_plan;
    const auto_hwm_scope_t scope =
      policy_.scope == auto_hwm_scope_none ? auto_hwm_scope_shared
                                           : policy_.scope;
    size_t scope_count = policy_.scope_count;
    if (scope_count == 0)
        scope_count = std::max<size_t> (
          std::max<size_t> (policy_.managed_connections,
                            policy_.active_connections),
          1u);
    auto_hwm_socket_plan_for_role (
      context_plan, policy_.role, policy_.socket_type,
      policy_.managed_connections, policy_.active_connections, &socket_plan,
      policy_.message_unit_bytes > 0 ? policy_.message_unit_bytes
                                     : context_plan.message_unit_bytes,
      -1, -1, false, false, scope, scope_count, true);
    spot_auto_hwm_internal::apply_spot_routed_latency_floor (&socket_plan,
                                                             profile);
    return socket_plan;
}

inline int spot_internal_auto_hwm_default_hwm (
  ctx_t *ctx_,
  auto_hwm_role_t role_,
  int socket_type_,
  bool recv_side_,
  int floor_ = 0,
  size_t managed_connections_ = 0,
  size_t active_connections_ = 0)
{
    const spot_internal_auto_hwm_policy_t policy = {
      role_, socket_type_, managed_connections_, active_connections_, floor_,
      floor_, false, false, false, false, auto_hwm_scope_none, 1, 0};
    const auto_hwm_socket_plan_t socket_plan =
      spot_internal_auto_hwm_plan (ctx_, policy);
    const int value = recv_side_ ? socket_plan.rcvhwm : socket_plan.sndhwm;
    return std::max (floor_, value);
}

inline void apply_spot_internal_auto_hwm (
  ctx_t *ctx_,
  socket_base_t *socket_,
  const spot_internal_auto_hwm_policy_t &policy_)
{
    if (!ctx_ || !socket_)
        return;

    const auto_hwm_socket_plan_t socket_plan =
      spot_internal_auto_hwm_plan (ctx_, policy_);
    const auto_hwm_scope_t scope =
      policy_.scope == auto_hwm_scope_none ? auto_hwm_scope_shared
                                           : policy_.scope;
    size_t scope_count = policy_.scope_count;
    if (scope_count == 0)
        scope_count = std::max<size_t> (
          std::max<size_t> (policy_.managed_connections,
                            policy_.active_connections),
          1u);
    socket_->set_auto_hwm_role (policy_.role);
    socket_->set_auto_hwm_scope (scope, scope_count);
    const int sndhwm = socket_plan.sndhwm;
    const int rcvhwm = socket_plan.rcvhwm;

    if (policy_.apply_sndhwm) {
        (void) socket_->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &sndhwm,
                                    sizeof (sndhwm));
    }
    if (policy_.apply_rcvhwm) {
        (void) socket_->setsockopt (ZLINK_INTERNAL_OPT_RCVHWM, &rcvhwm,
                                    sizeof (rcvhwm));
    }
    if (policy_.apply_sndbuf) {
        (void) socket_->setsockopt (ZLINK_INTERNAL_OPT_SNDBUF,
                                    &socket_plan.requested_sndbuf,
                                    sizeof (socket_plan.requested_sndbuf));
    }
    if (policy_.apply_rcvbuf) {
        (void) socket_->setsockopt (ZLINK_INTERNAL_OPT_RCVBUF,
                                    &socket_plan.requested_rcvbuf,
                                    sizeof (socket_plan.requested_rcvbuf));
    }
    socket_->clear_auto_hwm_manual_overrides (
      policy_.apply_sndhwm, policy_.apply_rcvhwm, policy_.apply_sndbuf,
      policy_.apply_rcvbuf);
}
}

#endif
