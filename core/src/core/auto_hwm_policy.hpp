/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_AUTO_HWM_POLICY_HPP_INCLUDED__
#define __ZLINK_AUTO_HWM_POLICY_HPP_INCLUDED__

#include <stddef.h>
#include <stdint.h>

namespace zlink
{
enum auto_hwm_role_t
{
    auto_hwm_role_none = 0,
    auto_hwm_role_control = 1,
    auto_hwm_role_routed = 2,
    auto_hwm_role_fanout = 3,
    auto_hwm_role_recv_ingress = 4
};

struct auto_hwm_context_plan_t
{
    auto_hwm_context_plan_t ();

    bool enabled;
    int total_memory_budget_mb;
    uint64_t total_memory_budget_bytes;
    uint64_t queue_budget_bytes;
    uint64_t transport_budget_bytes;
    uint64_t runtime_reserve_bytes;
    uint64_t effective_message_bytes;
};

struct auto_hwm_socket_plan_t
{
    auto_hwm_socket_plan_t ();

    auto_hwm_role_t role;
    uint64_t group_budget_bytes;
    uint64_t group_message_slots;
    uint32_t managed_connections;
    uint32_t active_hwm_connections;
    uint32_t planning_transport_connections;
    uint32_t base_floor_per_connection;
    int sndhwm;
    int rcvhwm;
    int requested_sndbuf;
    int requested_rcvbuf;
    int effective_sndbuf;
    int effective_rcvbuf;
};

auto_hwm_role_t auto_hwm_default_role_for_socket_type (int socket_type_);
void auto_hwm_context_plan_from_budget_mb (bool enabled_,
                                           int total_memory_budget_mb_,
                                           auto_hwm_context_plan_t *out_);
void auto_hwm_socket_plan_for_role (const auto_hwm_context_plan_t &context_,
                                    auto_hwm_role_t role_,
                                    int socket_type_,
                                    size_t managed_connections_,
                                    size_t active_hwm_connections_,
                                    auto_hwm_socket_plan_t *out_);
}

#endif
