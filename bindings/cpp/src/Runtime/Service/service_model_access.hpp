/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_SERVICE_MODEL_ACCESS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_SERVICE_MODEL_ACCESS_HPP_INCLUDED

#include "actor_model_access.hpp"
#include "../Core/routing_id_access.hpp"
#include <zlink/Contracts/Service/mesh_node_models.hpp>

#include <zlink.h>

namespace zlink::detail
{

struct service_model_access_t
{
    static mesh_node_status_t from_native (const zlink_mesh_node_status_t &status_)
    {
        mesh_node_status_t out;
        out.routing_id_ = native_routing_id (status_.routing_id);
        out.mesh_name_ = fixed_cstr_to_string (status_.mesh_name, sizeof (status_.mesh_name));
        out.local_endpoint_ =
          fixed_cstr_to_string (status_.local_endpoint, sizeof (status_.local_endpoint));
        out.state_ = static_cast<mesh_node_state_t> (status_.state);
        out.lifecycle_generation_ = status_.lifecycle_generation;
        out.descriptor_revision_ = status_.descriptor_revision;
        out.channel_count_ = status_.channel_count;
        out.configured_peer_count_ = status_.configured_peer_count;
        out.admitted_peer_count_ = status_.admitted_peer_count;
        out.draining_peer_count_ = status_.draining_peer_count;
        out.pending_application_messages_ = status_.pending_application_messages;
        out.pending_infrastructure_messages_ = status_.pending_infrastructure_messages;
        out.pending_bytes_ = status_.pending_bytes;
        out.multicast_submitted_ = status_.multicast_submitted;
        out.multicast_dropped_targets_ = status_.multicast_dropped_targets;
        out.last_error_ = status_.last_error;
        out.last_changed_ms_ = status_.last_changed_ms;
        return out;
    }

    static mesh_peer_entry_t from_native (const zlink_mesh_peer_entry_t &entry_)
    {
        mesh_peer_entry_t out;
        out.routing_id_ = native_routing_id (entry_.routing_id);
        out.endpoint_ = fixed_cstr_to_string (entry_.endpoint, sizeof (entry_.endpoint));
        out.connection_intent_id_ = entry_.connection_intent_id;
        out.source_ = static_cast<mesh_peer_source_t> (entry_.source);
        out.state_ = static_cast<mesh_peer_state_t> (entry_.state);
        out.lifecycle_generation_ = entry_.lifecycle_generation;
        out.descriptor_revision_ = entry_.descriptor_revision;
        out.channel_count_ = entry_.channel_count;
        out.last_error_ = entry_.last_error;
        out.last_changed_ms_ = entry_.last_changed_ms;
        return out;
    }
};

} // namespace zlink::detail

#endif
