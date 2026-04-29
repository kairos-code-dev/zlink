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
const int auto_hwm_default_sndbuf = 262144;
const int auto_hwm_default_rcvbuf = 262144;

struct profile_budget_t
{
    uint64_t fanout;
    uint64_t spot_data;
    uint64_t routed;
    uint64_t peer_queue;
    uint64_t stream;
    uint64_t recv_ingress;
    uint64_t control;
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
        case ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY:
        case ZLINK_AUTO_HWM_PROFILE_BALANCED:
        case ZLINK_AUTO_HWM_PROFILE_THROUGHPUT:
            return profile_;
        default:
            return ZLINK_AUTO_HWM_PROFILE_BALANCED;
    }
}

profile_budget_t profile_budget (zlink_auto_hwm_profile_t profile_)
{
    const uint64_t kib = 1024ull;
    switch (normalize_profile (profile_)) {
        case ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY:
            return profile_budget_t{512 * kib, 512 * kib, 256 * kib,
                                    256 * kib, 128 * kib, 128 * kib, 64 * kib};
        case ZLINK_AUTO_HWM_PROFILE_THROUGHPUT:
            return profile_budget_t{4 * mib, 4 * mib, 1 * mib, 1 * mib,
                                    512 * kib, 512 * kib, 128 * kib};
        case ZLINK_AUTO_HWM_PROFILE_BALANCED:
        default:
            return profile_budget_t{2 * mib, 2 * mib, 512 * kib, 512 * kib,
                                    256 * kib, 256 * kib, 64 * kib};
    }
}

uint64_t unit_budget_for_class (zlink_auto_hwm_profile_t profile_,
                                zlink::auto_hwm_policy_class_t policy_class_)
{
    const profile_budget_t budget = profile_budget (profile_);
    switch (policy_class_) {
        case zlink::auto_hwm_policy_fanout:
            return budget.fanout;
        case zlink::auto_hwm_policy_spot_data:
            return budget.spot_data;
        case zlink::auto_hwm_policy_routed:
            return budget.routed;
        case zlink::auto_hwm_policy_peer_queue:
            return budget.peer_queue;
        case zlink::auto_hwm_policy_stream:
            return budget.stream;
        case zlink::auto_hwm_policy_recv_ingress:
            return budget.recv_ingress;
        case zlink::auto_hwm_policy_control:
            return budget.control;
        default:
            return 0;
    }
}

uint64_t weight_units_for_class (zlink::auto_hwm_policy_class_t policy_class_)
{
    switch (policy_class_) {
        case zlink::auto_hwm_policy_fanout:
        case zlink::auto_hwm_policy_spot_data:
        case zlink::auto_hwm_policy_routed:
            return 8;
        case zlink::auto_hwm_policy_recv_ingress:
        case zlink::auto_hwm_policy_peer_queue:
        case zlink::auto_hwm_policy_stream:
            return 4;
        case zlink::auto_hwm_policy_control:
            return 1;
        default:
            return 0;
    }
}

uint32_t balanced_size_cap (zlink::auto_hwm_policy_class_t policy_class_,
                            uint64_t message_bytes_)
{
    if (policy_class_ == zlink::auto_hwm_policy_control)
        return 64;

    const bool fanout_like =
      policy_class_ == zlink::auto_hwm_policy_fanout
      || policy_class_ == zlink::auto_hwm_policy_spot_data;
    const bool peer_queue =
      policy_class_ == zlink::auto_hwm_policy_peer_queue;
    const bool routed_or_stream =
      policy_class_ == zlink::auto_hwm_policy_routed
      || policy_class_ == zlink::auto_hwm_policy_stream
      || policy_class_ == zlink::auto_hwm_policy_recv_ingress;

    if (message_bytes_ <= 16ull * 1024ull)
        return 64;
    if (message_bytes_ <= 64ull * 1024ull) {
        if (fanout_like)
            return 32;
        if (peer_queue)
            return 16;
        return routed_or_stream ? 64 : 32;
    }
    if (message_bytes_ <= 256ull * 1024ull) {
        if (fanout_like || peer_queue)
            return 8;
        return routed_or_stream ? 64 : 8;
    }
    if (fanout_like || peer_queue)
        return 4;
    return routed_or_stream ? 32 : 4;
}

uint32_t size_cap_for_class (zlink_auto_hwm_profile_t profile_,
                             zlink::auto_hwm_policy_class_t policy_class_,
                             uint64_t message_bytes_)
{
    if (policy_class_ == zlink::auto_hwm_policy_none)
        return 0;
    if (policy_class_ == zlink::auto_hwm_policy_control) {
        return normalize_profile (profile_) == ZLINK_AUTO_HWM_PROFILE_THROUGHPUT
                 ? 128
                 : 64;
    }

    const uint32_t balanced = balanced_size_cap (policy_class_, message_bytes_);
    switch (normalize_profile (profile_)) {
        case ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY:
            return std::max<uint32_t> (1, balanced / 2);
        case ZLINK_AUTO_HWM_PROFILE_THROUGHPUT: {
            uint32_t cap = balanced * 2;
            if (message_bytes_ > 256ull * 1024ull)
                cap = std::min<uint32_t> (cap, 8);
            return std::max<uint32_t> (1, cap);
        }
        case ZLINK_AUTO_HWM_PROFILE_BALANCED:
        default:
            return balanced;
    }
}
uint32_t transport_bootstrap_connections (int socket_type_,
                                          uint32_t planning_bootstrap_)
{
    if (planning_bootstrap_ > 0)
        return planning_bootstrap_;
    return socket_type_ == ZLINK_CORE_SOCKET_STREAM ? 5000u : 1u;
}

uint64_t effective_message_bytes (int socket_type_, int override_)
{
    if (override_ > 0)
        return static_cast<uint64_t> (override_);
    return socket_type_ == ZLINK_CORE_SOCKET_STREAM ? auto_hwm_stream_message_bytes
                                                    : auto_hwm_message_bytes;
}

uint64_t slots_from_budget (uint64_t budget_, uint64_t message_bytes_)
{
    if (message_bytes_ == 0)
        return 0;
    uint64_t slots = budget_ / message_bytes_;
    if (budget_ > 0 && slots == 0)
        slots = 1;
    return slots;
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
}

zlink::auto_hwm_context_plan_t::auto_hwm_context_plan_t () :
    enabled (false),
    profile (ZLINK_AUTO_HWM_PROFILE_BALANCED),
    total_memory_budget_mb (
      ZLINK_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB_DFLT),
    total_memory_budget_bytes (
      static_cast<uint64_t> (ZLINK_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB_DFLT)
      * mib),
    queue_budget_bytes (0),
    transport_budget_bytes (0),
    total_auto_buffer_bytes (0),
    runtime_reserve_bytes (0),
    total_planning_count (0)
{
}

zlink::auto_hwm_socket_plan_t::auto_hwm_socket_plan_t () :
    role (auto_hwm_role_none),
    policy_class (auto_hwm_policy_none),
    scope (auto_hwm_scope_none),
    scope_count (1),
    observed_count (0),
    planning_count (1),
    context_total_planning_count (1),
    socket_queue_share_bytes (0),
    socket_message_slots (0),
    auto_buffer_bytes (0),
    manual_buffer_bytes (0),
    buffer_connections (1),
    effective_message_bytes (auto_hwm_message_bytes),
    managed_connections (0),
    active_hwm_connections (0),
    base_floor_per_connection (1),
    unit_budget_bytes (0),
    size_cap (0),
    effective_publish_fanout (1),
    pending_messages (0),
    sndhwm (0),
    rcvhwm (0),
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

void zlink::auto_hwm_context_plan_from_budget_mb (bool enabled_,
                                                  int total_memory_budget_mb_,
                                                  auto_hwm_context_plan_t *out_,
                                                  zlink_auto_hwm_profile_t profile_)
{
    if (!out_)
        return;

    *out_ = auto_hwm_context_plan_t ();
    out_->enabled = enabled_;
    out_->profile = normalize_profile (profile_);
    out_->total_memory_budget_mb =
      total_memory_budget_mb_ > 0
        ? total_memory_budget_mb_
        : ZLINK_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB_DFLT;
    out_->total_memory_budget_bytes =
      static_cast<uint64_t> (out_->total_memory_budget_mb) * mib;
    out_->runtime_reserve_bytes = out_->total_memory_budget_bytes / 10ull;
    out_->transport_budget_bytes = 0;
    out_->total_auto_buffer_bytes = 0;
    out_->queue_budget_bytes =
      out_->total_memory_budget_bytes > out_->runtime_reserve_bytes
        ? out_->total_memory_budget_bytes - out_->runtime_reserve_bytes
        : 0;
    out_->total_planning_count = 0;
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
                                    bool buffer_cost_enabled_,
                                    uint32_t planning_bootstrap_)
{
    if (!out_)
        return;

    *out_ = auto_hwm_socket_plan_t ();
    out_->role = role_;
    out_->policy_class = auto_hwm_policy_class_for_role (role_, socket_type_);
    out_->scope = scope_;
    out_->managed_connections = clamp_size_to_u32 (managed_connections_);
    out_->active_hwm_connections = clamp_size_to_u32 (active_hwm_connections_);
    out_->effective_message_bytes =
      effective_message_bytes (socket_type_, message_unit_bytes_);

    const uint32_t bootstrap_connections =
      transport_bootstrap_connections (socket_type_, planning_bootstrap_);
    out_->observed_count = std::max (out_->managed_connections,
                                     out_->active_hwm_connections);
    out_->planning_count = std::max (out_->observed_count,
                                     bootstrap_connections);
    out_->effective_publish_fanout =
      out_->policy_class == auto_hwm_policy_spot_data
        ? std::max<uint32_t> (
            1u, std::min<uint32_t> (
                  std::max<uint32_t> (out_->observed_count, out_->scope_count),
                  bootstrap_connections > 0 ? bootstrap_connections : 1u))
        : out_->planning_count;
    if (out_->policy_class == auto_hwm_policy_spot_data)
        out_->planning_count = out_->effective_publish_fanout;
    out_->context_total_planning_count = out_->planning_count;
    out_->buffer_connections = out_->observed_count > 0 ? out_->observed_count
                                                         : 1u;

    out_->requested_sndbuf = manual_sndbuf_ ? sndbuf_ : auto_hwm_default_sndbuf;
    out_->requested_rcvbuf = manual_rcvbuf_ ? rcvbuf_ : auto_hwm_default_rcvbuf;
    out_->effective_sndbuf = out_->requested_sndbuf;
    out_->effective_rcvbuf = out_->requested_rcvbuf;

    const uint64_t auto_sndbuf =
      (!manual_sndbuf_ && buffer_cost_enabled_) ? auto_hwm_default_sndbuf : 0;
    const uint64_t auto_rcvbuf =
      (!manual_rcvbuf_ && buffer_cost_enabled_) ? auto_hwm_default_rcvbuf : 0;
    const uint64_t manual_sndbuf =
      manual_sndbuf_ && sndbuf_ > 0 ? static_cast<uint64_t> (sndbuf_) : 0;
    const uint64_t manual_rcvbuf =
      manual_rcvbuf_ && rcvbuf_ > 0 ? static_cast<uint64_t> (rcvbuf_) : 0;
    out_->auto_buffer_bytes =
      (auto_sndbuf + auto_rcvbuf) * out_->buffer_connections;
    out_->manual_buffer_bytes =
      (manual_sndbuf + manual_rcvbuf) * out_->buffer_connections;
    out_->scope_count = clamp_size_to_u32 (scope_count_ > 0 ? scope_count_ : 1);
    out_->base_floor_per_connection = 1;
    out_->unit_budget_bytes =
      unit_budget_for_class (ZLINK_AUTO_HWM_PROFILE_BALANCED,
                             out_->policy_class);
    out_->size_cap =
      size_cap_for_class (ZLINK_AUTO_HWM_PROFILE_BALANCED,
                          out_->policy_class, out_->effective_message_bytes);
}

void zlink::auto_hwm_context_finalize (auto_hwm_context_plan_t *context_,
                                       auto_hwm_socket_plan_t *plans_,
                                       size_t plan_count_)
{
    if (!context_ || !plans_)
        return;

    uint64_t total_auto_buffer_bytes = 0;
    uint64_t total_weight = 0;
    for (size_t i = 0; i != plan_count_; ++i) {
        total_auto_buffer_bytes += plans_[i].auto_buffer_bytes;
        plans_[i].unit_budget_bytes =
          unit_budget_for_class (context_->profile, plans_[i].policy_class);
        plans_[i].size_cap =
          size_cap_for_class (context_->profile, plans_[i].policy_class,
                              plans_[i].effective_message_bytes);
        const uint64_t class_weight =
          weight_units_for_class (plans_[i].policy_class);
        total_weight += class_weight * plans_[i].planning_count;
    }

    context_->total_auto_buffer_bytes = total_auto_buffer_bytes;
    context_->transport_budget_bytes = total_auto_buffer_bytes;
    context_->queue_budget_bytes =
      context_->total_memory_budget_bytes > context_->runtime_reserve_bytes
        ? context_->total_memory_budget_bytes - context_->runtime_reserve_bytes
        : 0;
    context_->total_planning_count = clamp_size_to_u32 (total_weight);

    if (total_weight == 0)
        total_weight = 1;

    for (size_t i = 0; i != plan_count_; ++i) {
        auto_hwm_socket_plan_t &plan = plans_[i];
        const uint64_t class_weight = weight_units_for_class (plan.policy_class);
        const uint64_t socket_weight = class_weight * plan.planning_count;
        plan.context_total_planning_count = clamp_size_to_u32 (total_weight);
        plan.socket_queue_share_bytes =
          (context_->queue_budget_bytes * socket_weight) / total_weight;

        const uint32_t socket_count =
          plan.planning_count > 0 ? plan.planning_count : 1;
        const uint64_t socket_budget_cap =
          plan.unit_budget_bytes * socket_count;
        const uint64_t socket_budget =
          std::min (socket_budget_cap, plan.socket_queue_share_bytes);
        const uint64_t per_connection_budget = socket_budget / socket_count;
        plan.socket_message_slots =
          slots_from_budget (per_connection_budget, plan.effective_message_bytes);

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
  bool buffer_cost_enabled_,
  uint32_t planning_bootstrap_)
{
    if (!out_)
        return;

    auto_hwm_socket_plan_prepare (
      role_, socket_type_, managed_connections_, active_hwm_connections_, out_,
      message_unit_bytes_, sndbuf_, rcvbuf_, manual_sndbuf_, manual_rcvbuf_,
      scope_, scope_count_, buffer_cost_enabled_, planning_bootstrap_);

    auto_hwm_context_plan_t adjusted_context = context_;
    auto_hwm_context_finalize (&adjusted_context, out_, 1);
}
