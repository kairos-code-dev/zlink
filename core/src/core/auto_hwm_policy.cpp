/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/auto_hwm_policy.hpp"
#include "core/internal_defs.hpp"
#include "zlink.h"

#include <limits.h>

namespace
{
const uint64_t mib = 1024ull * 1024ull;
const uint64_t auto_hwm_stream_message_bytes = 1024ull;
const uint64_t auto_hwm_message_bytes = 4096ull;
const int auto_hwm_default_sndbuf = 262144;
const int auto_hwm_default_rcvbuf = 262144;

uint32_t clamp_size_to_u32 (size_t value_)
{
    return value_ > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t> (value_);
}

int clamp_u64_to_int (uint64_t value_)
{
    return value_ > static_cast<uint64_t> (INT_MAX) ? INT_MAX
                                                    : static_cast<int> (value_);
}

uint64_t role_budget_bytes (zlink::auto_hwm_role_t role_,
                            const zlink::auto_hwm_context_plan_t &context_)
{
    const uint64_t queue_budget = context_.queue_budget_bytes;
    const uint64_t control_budget = (queue_budget * 5ull) / 100ull;
    const uint64_t routed_budget = (queue_budget * 25ull) / 100ull;
    const uint64_t fanout_budget = (queue_budget * 50ull) / 100ull;
    const uint64_t recv_ingress_budget =
      queue_budget - control_budget - routed_budget - fanout_budget;

    switch (role_) {
        case zlink::auto_hwm_role_control:
            return control_budget;
        case zlink::auto_hwm_role_routed:
            return routed_budget;
        case zlink::auto_hwm_role_fanout:
            return fanout_budget;
        case zlink::auto_hwm_role_recv_ingress:
            return recv_ingress_budget;
        default:
            return 0;
    }
}

uint32_t base_floor_per_connection (zlink::auto_hwm_role_t role_,
                                    uint32_t managed_connections_)
{
    switch (role_) {
        case zlink::auto_hwm_role_control:
            return 4;
        case zlink::auto_hwm_role_routed:
            if (managed_connections_ <= 1000)
                return 8;
            if (managed_connections_ <= 5000)
                return 4;
            return 2;
        case zlink::auto_hwm_role_fanout:
            if (managed_connections_ <= 100)
                return 16;
            if (managed_connections_ <= 1000)
                return 8;
            if (managed_connections_ <= 5000)
                return 4;
            return 1;
        case zlink::auto_hwm_role_recv_ingress:
            if (managed_connections_ <= 1000)
                return 8;
            if (managed_connections_ <= 5000)
                return 4;
            return 2;
        default:
            return 0;
    }
}

uint32_t transport_bootstrap_connections (int socket_type_)
{
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

uint64_t clamp_floor_to_budget (uint32_t floor_, uint64_t slots_)
{
    if (slots_ == 0)
        return 1;
    if (slots_ < floor_)
        return slots_;
    return floor_ > slots_ ? floor_ : slots_;
}
}

zlink::auto_hwm_context_plan_t::auto_hwm_context_plan_t () :
    enabled (false),
    total_memory_budget_mb (
      ZLINK_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB_DFLT),
    total_memory_budget_bytes (
      static_cast<uint64_t> (ZLINK_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB_DFLT)
      * mib),
    queue_budget_bytes (0),
    transport_budget_bytes (0),
    runtime_reserve_bytes (0)
{
}

zlink::auto_hwm_socket_plan_t::auto_hwm_socket_plan_t () :
    role (auto_hwm_role_none),
    scope (auto_hwm_scope_none),
    scope_count (1),
    group_budget_bytes (0),
    role_group_budget_bytes (0),
    scope_group_budget_bytes (0),
    group_message_slots (0),
    auto_buffer_bytes (0),
    manual_buffer_bytes (0),
    buffer_connections (1),
    effective_message_bytes (auto_hwm_message_bytes),
    managed_connections (0),
    active_hwm_connections (0),
    planning_transport_connections (1),
    base_floor_per_connection (0),
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
            return auto_hwm_role_control;
        case ZLINK_CORE_SOCKET_DEALER:
        case ZLINK_CORE_SOCKET_ROUTER:
        case ZLINK_CORE_SOCKET_STREAM:
            return auto_hwm_role_routed;
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

void zlink::auto_hwm_context_plan_from_budget_mb (bool enabled_,
                                                  int total_memory_budget_mb_,
                                                  auto_hwm_context_plan_t *out_)
{
    if (!out_)
        return;

    *out_ = auto_hwm_context_plan_t ();
    out_->enabled = enabled_;
    out_->total_memory_budget_mb =
      total_memory_budget_mb_ > 0
        ? total_memory_budget_mb_
        : ZLINK_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB_DFLT;
    out_->total_memory_budget_bytes =
      static_cast<uint64_t> (out_->total_memory_budget_mb) * mib;
    out_->runtime_reserve_bytes = out_->total_memory_budget_bytes / 10ull;
    out_->transport_budget_bytes = 0;
    out_->queue_budget_bytes =
      out_->total_memory_budget_bytes > out_->runtime_reserve_bytes
        ? out_->total_memory_budget_bytes - out_->runtime_reserve_bytes
        : 0;
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

    *out_ = auto_hwm_socket_plan_t ();
    out_->role = role_;
    out_->scope = scope_;
    out_->managed_connections = clamp_size_to_u32 (managed_connections_);
    out_->active_hwm_connections = clamp_size_to_u32 (active_hwm_connections_);
    out_->effective_message_bytes =
      effective_message_bytes (socket_type_, message_unit_bytes_);

    const uint32_t bootstrap_connections =
      transport_bootstrap_connections (socket_type_);
    uint32_t planning_connections = 1;
    if (out_->managed_connections > planning_connections)
        planning_connections = out_->managed_connections;
    if (bootstrap_connections > planning_connections)
        planning_connections = bootstrap_connections;
    out_->planning_transport_connections = planning_connections;
    out_->buffer_connections = planning_connections;

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

    auto_hwm_context_plan_t adjusted_context = context_;
    adjusted_context.queue_budget_bytes =
      context_.total_memory_budget_bytes
          > context_.runtime_reserve_bytes + out_->auto_buffer_bytes
        ? context_.total_memory_budget_bytes - context_.runtime_reserve_bytes
            - out_->auto_buffer_bytes
        : 0;
    adjusted_context.transport_budget_bytes = out_->auto_buffer_bytes;

    out_->role_group_budget_bytes = role_budget_bytes (role_, adjusted_context);
    out_->scope_count = clamp_size_to_u32 (scope_count_ > 0 ? scope_count_ : 1);
    if (out_->scope == auto_hwm_scope_per_spot)
        out_->scope_group_budget_bytes =
          out_->role_group_budget_bytes / out_->scope_count;
    else
        out_->scope_group_budget_bytes = out_->role_group_budget_bytes;

    out_->group_budget_bytes = out_->scope_group_budget_bytes;
    out_->group_message_slots = slots_from_budget (
      out_->scope_group_budget_bytes, out_->effective_message_bytes);
    out_->base_floor_per_connection =
      base_floor_per_connection (role_, out_->managed_connections);

    uint32_t hwm_divisor = 1;
    if (out_->scope == auto_hwm_scope_shared)
        hwm_divisor = out_->scope_count > 0 ? out_->scope_count : 1;
    else if (out_->scope == auto_hwm_scope_none)
        hwm_divisor = out_->active_hwm_connections > 0
                        ? out_->active_hwm_connections
                        : 1;

    const uint64_t target_slots =
      hwm_divisor > 0 ? out_->group_message_slots / hwm_divisor
                      : out_->group_message_slots;
    const uint64_t final_hwm =
      clamp_floor_to_budget (out_->base_floor_per_connection, target_slots);
    out_->sndhwm = clamp_u64_to_int (final_hwm);
    out_->rcvhwm = clamp_u64_to_int (final_hwm);
}
