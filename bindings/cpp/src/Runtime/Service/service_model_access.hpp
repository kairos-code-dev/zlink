/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_SERVICE_MODEL_ACCESS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_SERVICE_MODEL_ACCESS_HPP_INCLUDED

#include "actor_model_access.hpp"
#include "../Core/routing_id_access.hpp"
#include <zlink/Contracts/Service/spot_node_models.hpp>

#include <zlink.h>

namespace zlink::detail
{

struct service_model_access_t
{
    static spot_node_status_t from_native (const zlink_spot_node_status_t &status_)
    {
        spot_node_status_t out;
        out.channel_name_ = fixed_string_to_string (status_.channel_name);
        out.local_endpoint_ = fixed_string_to_string (status_.local_endpoint);
        out.node_routing_id_ =
          status_.node_routing_id.size > 0
            ? std::optional<routing_id_t> (native_routing_id (status_.node_routing_id))
            : std::nullopt;
        out.state_ = static_cast<spot_node_state_t> (status_.state);
        out.configured_peer_count_ = status_.configured_peer_count;
        out.active_peer_count_ = status_.active_peer_count;
        out.connected_peer_count_ = status_.connected_peer_count;
        out.subject_count_ = status_.subject_count;
        out.ready_subject_count_ = status_.ready_subject_count;
        out.disconnected_sub_target_count_ = status_.disconnected_sub_target_count;
        out.disconnected_routed_target_count_ = status_.disconnected_routed_target_count;
        out.last_error_ = status_.last_error;
        out.last_changed_ms_ = status_.last_changed_ms;
        return out;
    }

    static spot_node_peer_entry_t from_native (const zlink_spot_node_peer_entry_t &entry_)
    {
        spot_node_peer_entry_t out;
        out.channel_name_ = fixed_string_to_string (entry_.channel_name);
        out.local_endpoint_ = fixed_string_to_string (entry_.local_endpoint);
        out.peer_endpoint_ = fixed_string_to_string (entry_.peer_endpoint);
        out.source_ = static_cast<spot_peer_source_t> (entry_.source);
        out.kind_ = static_cast<spot_peer_kind_t> (entry_.kind);
        out.state_ = static_cast<spot_peer_state_t> (entry_.state);
        out.weight_ = entry_.weight;
        out.connected_since_ms_ = entry_.connected_since_ms;
        out.last_changed_ms_ = entry_.last_changed_ms;
        return out;
    }

    static spot_node_subject_entry_t from_native (const zlink_spot_node_subject_entry_t &entry_)
    {
        spot_node_subject_entry_t out;
        out.role_ = static_cast<spot_role_t> (entry_.role);
        out.subject_ = fixed_string_to_string (entry_.subject);
        out.subject_kind_ = static_cast<subject_kind_t> (entry_.subject_kind);
        out.ready_peer_count_ = entry_.ready_peer_count;
        out.active_peer_count_ = entry_.active_peer_count;
        out.last_changed_ms_ = entry_.last_changed_ms;
        return out;
    }

    static spot_node_socket_entry_t from_native (const zlink_spot_node_socket_entry_t &entry_)
    {
        spot_node_socket_entry_t out;
        out.owner_ = static_cast<spot_node_socket_owner_t> (entry_.owner);
        out.owner_id_ = entry_.owner_id;
        out.owner_name_ = fixed_string_to_string (entry_.owner_name);
        out.socket_name_ = fixed_string_to_string (entry_.socket_name);
        out.socket_type_ = static_cast<spot_node_socket_type_t> (entry_.socket_type);
        out.auto_hwm_visible_ = entry_.auto_hwm_visible != 0;
        out.monitor_status_.source_kind =
          static_cast<monitor_source_kind> (entry_.monitor_status.source_kind);
        out.monitor_status_.state_flags = entry_.monitor_status.state_flags;
        out.monitor_status_.detail_flags = entry_.monitor_status.detail_flags;
        out.monitor_status_.snd_pending_msgs = entry_.monitor_status.snd_pending_msgs;
        out.monitor_status_.rcv_pending_msgs = entry_.monitor_status.rcv_pending_msgs;
        out.monitor_status_.auto_hwm_enabled = entry_.monitor_status.auto_hwm_enabled != 0;
        out.monitor_status_.auto_hwm_profile = entry_.monitor_status.auto_hwm_profile;
        out.monitor_status_.auto_hwm_role = entry_.monitor_status.auto_hwm_role;
        out.monitor_status_.auto_hwm_policy_class = entry_.monitor_status.auto_hwm_policy_class;
        out.monitor_status_.auto_hwm_unit_budget_bytes =
          entry_.monitor_status.auto_hwm_unit_budget_bytes;
        out.monitor_status_.auto_hwm_size_cap = entry_.monitor_status.auto_hwm_size_cap;
        out.monitor_status_.auto_hwm_socket_message_slots =
          entry_.monitor_status.auto_hwm_socket_message_slots;
        out.monitor_status_.auto_hwm_connection_bucket_enabled =
          entry_.monitor_status.auto_hwm_connection_bucket_enabled != 0;
        out.monitor_status_.auto_hwm_connection_bucket_count =
          entry_.monitor_status.auto_hwm_connection_bucket_count;
        out.monitor_status_.auto_hwm_connection_bucket_index =
          entry_.monitor_status.auto_hwm_connection_bucket_index;
        out.monitor_status_.auto_hwm_connection_bucket_hwm_4k =
          entry_.monitor_status.auto_hwm_connection_bucket_hwm_4k;
        out.monitor_status_.auto_hwm_connection_bucket_hysteresis_retained =
          entry_.monitor_status.auto_hwm_connection_bucket_hysteresis_retained != 0;
        out.monitor_status_.auto_hwm_effective_message_bytes =
          entry_.monitor_status.auto_hwm_effective_message_bytes;
        out.monitor_status_.auto_hwm_applied_sndhwm = entry_.monitor_status.auto_hwm_applied_sndhwm;
        out.monitor_status_.auto_hwm_applied_rcvhwm = entry_.monitor_status.auto_hwm_applied_rcvhwm;
        out.monitor_status_.auto_hwm_effective_sndbuf =
          entry_.monitor_status.auto_hwm_effective_sndbuf;
        out.monitor_status_.auto_hwm_effective_rcvbuf =
          entry_.monitor_status.auto_hwm_effective_rcvbuf;
        out.monitor_status_.auto_hwm_last_recalc_ms = entry_.monitor_status.auto_hwm_last_recalc_ms;
        out.monitor_status_.auto_hwm_last_recalc_reason =
          entry_.monitor_status.auto_hwm_last_recalc_reason;
        out.monitor_status_.auto_hwm_send_blocked_ratio_ppm =
          entry_.monitor_status.auto_hwm_send_blocked_ratio_ppm;
        out.monitor_status_.auto_hwm_deferred_sndhwm =
          entry_.monitor_status.auto_hwm_deferred_sndhwm;
        out.monitor_status_.auto_hwm_deferred_rcvhwm =
          entry_.monitor_status.auto_hwm_deferred_rcvhwm;
        return out;
    }

    static spot_node_spot_entry_t from_native (const zlink_spot_node_spot_entry_t &entry_)
    {
        spot_node_spot_entry_t out;
        out.spot_rid_ = native_routing_id (entry_.spot_rid);
        out.spot_kind_ = static_cast<spot_kind> (entry_.spot_kind);
        out.dispatch_handler_attached_ = entry_.dispatch_handler_attached != 0;
        out.joined_actor_count_ = entry_.joined_actor_count;
        out.pending_actor_join_count_ = entry_.pending_actor_join_count;
        out.route_synced_ = entry_.route_synced != 0;
        out.last_changed_ms_ = entry_.last_changed_ms;
        return out;
    }

    static spot_node_actor_entry_t from_native (const zlink_spot_node_actor_entry_t &entry_)
    {
        spot_node_actor_entry_t out;
        out.actor_ = actor_model_access_t::from_native (entry_.actor);
        out.current_spot_rid_ =
          entry_.current_spot_rid.size > 0
            ? std::optional<routing_id_t> (native_routing_id (entry_.current_spot_rid))
            : std::nullopt;
        out.current_spot_kind_ = static_cast<spot_kind> (entry_.current_spot_kind);
        out.route_synced_ = entry_.route_synced != 0;
        out.pending_message_count_ = entry_.pending_message_count;
        out.last_changed_ms_ = entry_.last_changed_ms;
        return out;
    }
};

} // namespace zlink::detail

#endif
