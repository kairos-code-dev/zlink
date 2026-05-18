/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/auto_hwm_policy.hpp"
#include "core/internal_defs.hpp"
#include "zlink.h"

#include <algorithm>
#include <limits.h>

namespace
{
const uint64_t mib = 1024ull * 1024ull;
const uint64_t auto_hwm_stream_message_bytes = 1024ull;
const uint64_t auto_hwm_message_bytes = 4096ull;
const int auto_hwm_compact_buffer_floor = 128 * 1024;
const int auto_hwm_default_buffer_floor = 256 * 1024;
const uint64_t auto_hwm_max_buffer_bytes = 16ull * mib;

struct profile_hwm_t
{
    uint32_t message_hwm;
    uint32_t stream_hwm;
    uint32_t control_hwm;
    uint32_t message_cap;
    uint32_t stream_cap;
};

uint32_t clamp_size_to_u32 (size_t value_)
{
    return value_ > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t> (value_);
}

int clamp_u64_to_int (uint64_t value_)
{
    return value_ > static_cast<uint64_t> (INT_MAX) ? INT_MAX
                                                    : static_cast<int> (value_);
}

zlink_auto_hwm_profile_t normalize_profile (zlink_auto_hwm_profile_t profile_)
{
    switch (profile_) {
        case ZLINK_AUTO_HWM_PROFILE_COMPACT:
        case ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY:
        case ZLINK_AUTO_HWM_PROFILE_BALANCED:
        case ZLINK_AUTO_HWM_PROFILE_THROUGHPUT:
            return profile_;
        default:
            return ZLINK_AUTO_HWM_PROFILE_BALANCED;
    }
}

profile_hwm_t profile_hwm (zlink_auto_hwm_profile_t profile_)
{
    switch (normalize_profile (profile_)) {
        case ZLINK_AUTO_HWM_PROFILE_COMPACT:
            return profile_hwm_t{64, 8, 8, 256, 32};
        case ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY:
            return profile_hwm_t{128, 16, 16, 512, 64};
        case ZLINK_AUTO_HWM_PROFILE_THROUGHPUT:
            return profile_hwm_t{512, 256, 32, 4096, 512};
        case ZLINK_AUTO_HWM_PROFILE_BALANCED:
        default:
            return profile_hwm_t{256, 64, 16, 1024, 128};
    }
}

bool stream_policy_class (zlink::auto_hwm_policy_class_t policy_class_)
{
    return policy_class_ == zlink::auto_hwm_policy_stream;
}

uint32_t basis_hwm_for_class (zlink_auto_hwm_profile_t profile_,
                              zlink::auto_hwm_policy_class_t policy_class_)
{
    const profile_hwm_t hwm = profile_hwm (profile_);
    switch (policy_class_) {
        case zlink::auto_hwm_policy_fanout:
        case zlink::auto_hwm_policy_spot_data:
        case zlink::auto_hwm_policy_routed:
        case zlink::auto_hwm_policy_recv_ingress:
        case zlink::auto_hwm_policy_peer_queue:
            return hwm.message_hwm;
        case zlink::auto_hwm_policy_stream:
            return hwm.stream_hwm;
        case zlink::auto_hwm_policy_control:
            return hwm.control_hwm;
        default:
            return 0;
    }
}

uint32_t size_cap_for_class (zlink_auto_hwm_profile_t profile_,
                             zlink::auto_hwm_policy_class_t policy_class_)
{
    if (policy_class_ == zlink::auto_hwm_policy_none)
        return 0;
    const profile_hwm_t hwm = profile_hwm (profile_);
    return stream_policy_class (policy_class_) ? hwm.stream_cap
                                               : hwm.message_cap;
}

int buffer_floor_for_profile (zlink_auto_hwm_profile_t profile_)
{
    return normalize_profile (profile_) == ZLINK_AUTO_HWM_PROFILE_COMPACT
             ? auto_hwm_compact_buffer_floor
             : auto_hwm_default_buffer_floor;
}

uint64_t effective_message_bytes (int socket_type_, int override_)
{
    if (override_ > 0)
        return static_cast<uint64_t> (override_);
    return socket_type_ == ZLINK_CORE_SOCKET_STREAM ? auto_hwm_stream_message_bytes
                                                    : auto_hwm_message_bytes;
}

uint64_t ceil_div (uint64_t numerator_, uint64_t denominator_)
{
    if (denominator_ == 0)
        return 0;
    return (numerator_ + denominator_ - 1) / denominator_;
}

uint64_t clamp_hwm_to_cap (uint64_t slots_, uint32_t size_cap_)
{
    if (size_cap_ == 0)
        return 0;
    uint64_t hwm = std::min<uint64_t> (slots_, size_cap_);
    if (hwm == 0)
        return 1;
    return hwm;
}

int auto_buffer_bytes_for_message (uint64_t message_bytes_,
                                   int default_buffer_bytes_)
{
    if (message_bytes_ == 0)
        return default_buffer_bytes_;

    uint64_t target = message_bytes_ * 4ull;
    if (target < static_cast<uint64_t> (default_buffer_bytes_))
        target = static_cast<uint64_t> (default_buffer_bytes_);
    if (target > auto_hwm_max_buffer_bytes)
        target = auto_hwm_max_buffer_bytes;
    if (target > static_cast<uint64_t> (INT_MAX))
        return INT_MAX;
    return static_cast<int> (target);
}
}

zlink::auto_hwm_context_plan_t::auto_hwm_context_plan_t () :
    enabled (false),
    profile (ZLINK_AUTO_HWM_PROFILE_BALANCED),
    message_unit_bytes (0)
{
}

zlink::auto_hwm_socket_plan_t::auto_hwm_socket_plan_t () :
    role (auto_hwm_role_none),
    policy_class (auto_hwm_policy_none),
    scope (auto_hwm_scope_none),
    scope_count (1),
    socket_message_slots (0),
    effective_message_bytes (auto_hwm_message_bytes),
    unit_budget_bytes (0),
    size_cap (0),
    pending_messages (0),
    sndhwm (0),
    rcvhwm (0),
    manual_sndbuf (false),
    manual_rcvbuf (false),
    requested_sndbuf (-1),
    requested_rcvbuf (-1),
    effective_sndbuf (-1),
    effective_rcvbuf (-1)
{
}

zlink::auto_hwm_role_t zlink::auto_hwm_default_role_for_socket_type (
  int socket_type_)
{
    switch (socket_type_) {
        case ZLINK_CORE_SOCKET_PAIR:
            return auto_hwm_role_peer_queue;
        case ZLINK_CORE_SOCKET_DEALER:
            return auto_hwm_role_peer_queue;
        case ZLINK_CORE_SOCKET_ROUTER:
            return auto_hwm_role_routed;
        case ZLINK_CORE_SOCKET_STREAM:
            return auto_hwm_role_stream;
        case ZLINK_CORE_SOCKET_PUB:
        case ZLINK_CORE_SOCKET_XPUB:
            return auto_hwm_role_fanout;
        case ZLINK_CORE_SOCKET_SUB:
        case ZLINK_CORE_SOCKET_XSUB:
            return auto_hwm_role_recv_ingress;
        default:
            return auto_hwm_role_none;
    }
}

zlink::auto_hwm_policy_class_t zlink::auto_hwm_policy_class_for_role (
  auto_hwm_role_t role_,
  int socket_type_)
{
    switch (role_) {
        case auto_hwm_role_control:
            return auto_hwm_policy_control;
        case auto_hwm_role_routed:
            return auto_hwm_policy_routed;
        case auto_hwm_role_fanout:
            return auto_hwm_policy_fanout;
        case auto_hwm_role_spot_data:
            return auto_hwm_policy_spot_data;
        case auto_hwm_role_recv_ingress:
            return auto_hwm_policy_recv_ingress;
        case auto_hwm_role_peer_queue:
            return auto_hwm_policy_peer_queue;
        case auto_hwm_role_stream:
            return auto_hwm_policy_stream;
        case auto_hwm_role_none:
        default:
            break;
    }

    switch (socket_type_) {
        case ZLINK_CORE_SOCKET_PUB:
        case ZLINK_CORE_SOCKET_XPUB:
            return auto_hwm_policy_fanout;
        case ZLINK_CORE_SOCKET_SUB:
        case ZLINK_CORE_SOCKET_XSUB:
            return auto_hwm_policy_recv_ingress;
        case ZLINK_CORE_SOCKET_ROUTER:
            return auto_hwm_policy_routed;
        case ZLINK_CORE_SOCKET_DEALER:
        case ZLINK_CORE_SOCKET_PAIR:
            return auto_hwm_policy_peer_queue;
        case ZLINK_CORE_SOCKET_STREAM:
            return auto_hwm_policy_stream;
        default:
            return auto_hwm_policy_none;
    }
}

void zlink::auto_hwm_context_plan_make (bool enabled_,
                                        zlink_auto_hwm_profile_t profile_,
                                        auto_hwm_context_plan_t *out_,
                                        int message_unit_bytes_)
{
    if (!out_)
        return;

    *out_ = auto_hwm_context_plan_t ();
    out_->enabled = enabled_;
    out_->profile = normalize_profile (profile_);
    out_->message_unit_bytes = message_unit_bytes_ > 0 ? message_unit_bytes_ : 0;
}

void zlink::auto_hwm_socket_plan_prepare (
  auto_hwm_role_t role_,
  int socket_type_,
  size_t managed_connections_,
  size_t active_hwm_connections_,
  auto_hwm_socket_plan_t *out_,
  int message_unit_bytes_,
  int sndbuf_,
  int rcvbuf_,
  bool manual_sndbuf_,
                                    bool manual_rcvbuf_,
                                    auto_hwm_scope_t scope_,
                                    size_t scope_count_,
                                    bool buffer_cost_enabled_)
{
    if (!out_)
        return;

    (void) buffer_cost_enabled_;

    *out_ = auto_hwm_socket_plan_t ();
    out_->role = role_;
    out_->policy_class = auto_hwm_policy_class_for_role (role_, socket_type_);
    out_->scope = scope_;
    out_->manual_sndbuf = manual_sndbuf_;
    out_->manual_rcvbuf = manual_rcvbuf_;
    out_->effective_message_bytes =
      effective_message_bytes (socket_type_, message_unit_bytes_);
    const uint32_t buffer_connections =
      std::max<uint32_t> (
        clamp_size_to_u32 (
          std::max (managed_connections_, active_hwm_connections_)),
        1u);

    const int auto_buffer_floor =
      buffer_floor_for_profile (ZLINK_CTX_AUTO_HWM_PROFILE_DFLT);
    const int auto_sndbuf_value = auto_buffer_bytes_for_message (
      out_->effective_message_bytes, auto_buffer_floor);
    const int auto_rcvbuf_value = auto_buffer_bytes_for_message (
      out_->effective_message_bytes, auto_buffer_floor);

    out_->requested_sndbuf = manual_sndbuf_ ? sndbuf_ : auto_sndbuf_value;
    out_->requested_rcvbuf = manual_rcvbuf_ ? rcvbuf_ : auto_rcvbuf_value;
    out_->effective_sndbuf = out_->requested_sndbuf;
    out_->effective_rcvbuf = out_->requested_rcvbuf;

    (void) buffer_connections;
    out_->scope_count = clamp_size_to_u32 (scope_count_ > 0 ? scope_count_ : 1);
    out_->unit_budget_bytes =
      static_cast<uint64_t> (
        basis_hwm_for_class (ZLINK_AUTO_HWM_PROFILE_BALANCED,
                             out_->policy_class))
      * effective_message_bytes (socket_type_, 0);
    out_->size_cap =
      size_cap_for_class (ZLINK_AUTO_HWM_PROFILE_BALANCED,
                          out_->policy_class);
}

void zlink::auto_hwm_context_finalize (auto_hwm_context_plan_t *context_,
                                       auto_hwm_socket_plan_t *plans_,
                                       size_t plan_count_)
{
    if (!context_ || !plans_)
        return;

    for (size_t i = 0; i != plan_count_; ++i) {
        auto_hwm_socket_plan_t &plan = plans_[i];
        const uint32_t basis_hwm =
          basis_hwm_for_class (context_->profile, plan.policy_class);
        const uint64_t basis_message_bytes =
          stream_policy_class (plan.policy_class) ? auto_hwm_stream_message_bytes
                                                  : auto_hwm_message_bytes;
        plan.unit_budget_bytes =
          static_cast<uint64_t> (basis_hwm) * basis_message_bytes;
        plan.size_cap = size_cap_for_class (context_->profile,
                                            plan.policy_class);
        const int auto_buffer_floor =
          buffer_floor_for_profile (context_->profile);
        if (!plan.manual_sndbuf) {
            plan.requested_sndbuf = auto_buffer_bytes_for_message (
              plan.effective_message_bytes, auto_buffer_floor);
            plan.effective_sndbuf = plan.requested_sndbuf;
        }
        if (!plan.manual_rcvbuf) {
            plan.requested_rcvbuf = auto_buffer_bytes_for_message (
              plan.effective_message_bytes, auto_buffer_floor);
            plan.effective_rcvbuf = plan.requested_rcvbuf;
        }
        plan.socket_message_slots =
          ceil_div (plan.unit_budget_bytes, plan.effective_message_bytes);

        const uint64_t final_hwm =
          clamp_hwm_to_cap (plan.socket_message_slots, plan.size_cap);
        plan.sndhwm = clamp_u64_to_int (final_hwm);
        plan.rcvhwm = clamp_u64_to_int (final_hwm);
    }
}

void zlink::auto_hwm_socket_plan_for_role (
  const auto_hwm_context_plan_t &context_,
  auto_hwm_role_t role_,
  int socket_type_,
  size_t managed_connections_,
  size_t active_hwm_connections_,
  auto_hwm_socket_plan_t *out_,
  int message_unit_bytes_,
  int sndbuf_,
  int rcvbuf_,
  bool manual_sndbuf_,
  bool manual_rcvbuf_,
  auto_hwm_scope_t scope_,
  size_t scope_count_,
  bool buffer_cost_enabled_)
{
    if (!out_)
        return;

    auto_hwm_socket_plan_prepare (
      role_, socket_type_, managed_connections_, active_hwm_connections_, out_,
      message_unit_bytes_, sndbuf_, rcvbuf_, manual_sndbuf_, manual_rcvbuf_,
      scope_, scope_count_, buffer_cost_enabled_);

    auto_hwm_context_plan_t adjusted_context = context_;
    auto_hwm_context_finalize (&adjusted_context, out_, 1);
}
