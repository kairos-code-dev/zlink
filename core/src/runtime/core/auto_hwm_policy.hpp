/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_AUTO_HWM_POLICY_HPP_INCLUDED__
#define __ZLINK_AUTO_HWM_POLICY_HPP_INCLUDED__

#include <stddef.h>
#include <stdint.h>

#include "zlink_enum.h"

namespace zlink
{
enum auto_hwm_role_t
{
    auto_hwm_role_none = 0,
    auto_hwm_role_control = 1,
    auto_hwm_role_routed = 2,
    auto_hwm_role_fanout = 3,
    auto_hwm_role_recv_ingress = 4,
    auto_hwm_role_spot_data = 5,
    auto_hwm_role_peer_queue = 6,
    auto_hwm_role_stream = 7
};

enum auto_hwm_policy_class_t
{
    auto_hwm_policy_none = 0,
    auto_hwm_policy_fanout = 1,
    auto_hwm_policy_spot_data = 2,
    auto_hwm_policy_recv_ingress = 3,
    auto_hwm_policy_routed = 4,
    auto_hwm_policy_peer_queue = 5,
    auto_hwm_policy_stream = 6,
    auto_hwm_policy_control = 7
};

enum auto_hwm_scope_t
{
    auto_hwm_scope_none = 0,
    auto_hwm_scope_shared = 1,
    auto_hwm_scope_per_spot = 2
};

struct auto_hwm_context_plan_t
{
    auto_hwm_context_plan_t ();

    bool enabled;
    zlink_auto_hwm_profile_t profile;
    int message_unit_bytes;
};

struct auto_hwm_socket_plan_t
{
    auto_hwm_socket_plan_t ();

    auto_hwm_role_t role;
    auto_hwm_policy_class_t policy_class;
    auto_hwm_scope_t scope;
    uint32_t scope_count;
    uint64_t socket_message_slots;
    uint64_t effective_message_bytes;
    uint64_t unit_budget_bytes;
    uint32_t size_cap;
    uint64_t pending_messages;
    int sndhwm;
    int rcvhwm;
    bool manual_sndbuf;
    bool manual_rcvbuf;
    int requested_sndbuf;
    int requested_rcvbuf;
    int effective_sndbuf;
    int effective_rcvbuf;
};

auto_hwm_role_t auto_hwm_default_role_for_socket_type (int socket_type_);
auto_hwm_policy_class_t auto_hwm_policy_class_for_role (auto_hwm_role_t role_, int socket_type_);
void auto_hwm_context_plan_make (bool enabled_,
                                 zlink_auto_hwm_profile_t profile_,
                                 auto_hwm_context_plan_t *out_,
                                 int message_unit_bytes_ = 0);
void auto_hwm_socket_plan_prepare (auto_hwm_role_t role_,
                                   int socket_type_,
                                   size_t managed_connections_,
                                   size_t active_hwm_connections_,
                                   auto_hwm_socket_plan_t *out_,
                                   int message_unit_bytes_ = 0,
                                   int sndbuf_ = -1,
                                   int rcvbuf_ = -1,
                                   bool manual_sndbuf_ = false,
                                   bool manual_rcvbuf_ = false,
                                   auto_hwm_scope_t scope_ = auto_hwm_scope_none,
                                   size_t scope_count_ = 1,
                                   bool buffer_cost_enabled_ = true);
void auto_hwm_context_finalize (auto_hwm_context_plan_t *context_,
                                auto_hwm_socket_plan_t *plans_,
                                size_t plan_count_);
void auto_hwm_socket_plan_for_role (const auto_hwm_context_plan_t &context_,
                                    auto_hwm_role_t role_,
                                    int socket_type_,
                                    size_t managed_connections_,
                                    size_t active_hwm_connections_,
                                    auto_hwm_socket_plan_t *out_,
                                    int message_unit_bytes_ = 0,
                                    int sndbuf_ = -1,
                                    int rcvbuf_ = -1,
                                    bool manual_sndbuf_ = false,
                                    bool manual_rcvbuf_ = false,
                                    auto_hwm_scope_t scope_ = auto_hwm_scope_none,
                                    size_t scope_count_ = 1,
                                    bool buffer_cost_enabled_ = true);
}

#endif
