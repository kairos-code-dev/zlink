/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_ACTOR_MODEL_ACCESS_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_ACTOR_MODEL_ACCESS_HPP_INCLUDED

#include "../Core/routing_id_access.hpp"
#include <zlink/Contracts/Service/actor_models.hpp>

#include <zlink.h>

#include <cstring>

namespace zlink::detail
{

struct actor_model_access_t
{
    static actor_ref_t from_native (const zlink_actor_ref_t &native_)
    {
        return actor_ref_t (native_routing_id (native_.node_rid),
                            fixed_string_to_string (native_.actor_id), native_.generation);
    }

    static zlink_actor_ref_t to_native (const actor_ref_t &actor_) noexcept
    {
        zlink_actor_ref_t out;
        std::memset (&out, 0, sizeof (out));
        out.node_rid = routing_id_native_value (actor_._node_rid);
        const size_t max = sizeof (out.actor_id) - 1u;
        const size_t n = actor_._actor_id.size () < max ? actor_._actor_id.size () : max;
        if (n > 0)
            std::memcpy (out.actor_id, actor_._actor_id.data (), n);
        out.generation = actor_._generation;
        return out;
    }

    static const zlink_actor_ref_t *native_ptr (const actor_ref_t &actor_) noexcept
    {
        thread_local zlink_actor_ref_t native[8];
        thread_local size_t index = 0;
        zlink_actor_ref_t &slot = native[index++ % 8u];
        slot = to_native (actor_);
        return &slot;
    }

    static actor_recv_info_t from_native (const zlink_actor_recv_info_t &native_)
    {
        actor_recv_info_t out;
        out.actor = from_native (native_.actor);
        out.source_node_rid = native_routing_id (native_.source_node_rid);
        out.source_session_rid = native_routing_id (native_.source_session_rid);
        out.flags = native_.flags;
        return out;
    }

    static actor_join_info_t from_native (const zlink_actor_join_info_t &native_)
    {
        actor_join_info_t out;
        out.source_actor = from_native (native_.source_actor);
        out.target_actor = from_native (native_.target_actor);
        out.source_node_rid = native_routing_id (native_.source_node_rid);
        out.source_spot_rid = native_routing_id (native_.source_spot_rid);
        out.target_node_rid = native_routing_id (native_.target_node_rid);
        out.target_spot_rid = native_routing_id (native_.target_spot_rid);
        out.join_epoch = native_.join_epoch;
        out.flags = native_.flags;
        out._request_token = reinterpret_cast<std::uintptr_t> (native_.request);
        return out;
    }

    static zlink_actor_join_info_t to_native (const actor_join_info_t &info_) noexcept
    {
        zlink_actor_join_info_t out;
        std::memset (&out, 0, sizeof (out));
        out.source_actor = to_native (info_.source_actor);
        out.target_actor = to_native (info_.target_actor);
        out.source_node_rid = routing_id_native_value (info_.source_node_rid);
        out.source_spot_rid = routing_id_native_value (info_.source_spot_rid);
        out.target_node_rid = routing_id_native_value (info_.target_node_rid);
        out.target_spot_rid = routing_id_native_value (info_.target_spot_rid);
        out.join_epoch = info_.join_epoch;
        out.request = reinterpret_cast<void *> (info_._request_token);
        out.flags = info_.flags;
        return out;
    }

    static actor_join_result_t from_native (const zlink_actor_join_result_t &native_)
    {
        actor_join_result_t out;
        out.result = static_cast<request_result_t> (native_.result);
        out.join_result_code = native_.join_result_code;
        out.actor = from_native (native_.actor);
        out.joined_spot_rid = native_routing_id (native_.joined_spot_rid);
        out.join_epoch = native_.join_epoch;
        out.flags = native_.flags;
        return out;
    }

    static actor_join_entry_spot_result_t
    from_native (const zlink_actor_join_entry_spot_result_t &native_)
    {
        actor_join_entry_spot_result_t out;
        out.result = static_cast<request_result_t> (native_.result);
        out.actor = from_native (native_.actor);
        out.target_node_rid = native_routing_id (native_.target_node_rid);
        out.join_epoch = native_.join_epoch;
        out.flags = native_.flags;
        return out;
    }

    static actor_lookup_result_t from_native (const zlink_actor_lookup_result_t &native_)
    {
        actor_lookup_result_t out;
        out.result = static_cast<request_result_t> (native_.result);
        out.actor = from_native (native_.actor);
        out.flags = native_.flags;
        return out;
    }

    static spot_actor_lifecycle_info_t
    from_native (const zlink_spot_actor_lifecycle_info_t &native_)
    {
        spot_actor_lifecycle_info_t out;
        out.previous_actor = from_native (native_.previous_actor);
        out.current_actor = from_native (native_.current_actor);
        out.previous_spot_rid =
          native_.previous_spot_rid.size > 0
            ? std::optional<routing_id_t> (native_routing_id (native_.previous_spot_rid))
            : std::nullopt;
        out.current_spot_rid =
          native_.current_spot_rid.size > 0
            ? std::optional<routing_id_t> (native_routing_id (native_.current_spot_rid))
            : std::nullopt;
        out.join_epoch = native_.join_epoch;
        out.flags = native_.flags;
        return out;
    }

    static spot_actor_lifecycle_event_t
    from_native (const zlink_spot_actor_lifecycle_event_t &native_)
    {
        spot_actor_lifecycle_event_t out;
        out.kind = static_cast<spot_actor_lifecycle_event_kind_t> (native_.kind);
        out.info = from_native (native_.info);
        return out;
    }

    static actor_route_t from_native (const zlink_actor_route_t &native_)
    {
        actor_route_t out;
        out.actor = from_native (native_.actor);
        out.current_spot_rid =
          native_.current_spot_rid.size > 0
            ? std::optional<routing_id_t> (native_routing_id (native_.current_spot_rid))
            : std::nullopt;
        out.current_spot_kind = static_cast<spot_kind> (native_.current_spot_kind);
        return out;
    }

    static spot_route_t from_native (const zlink_spot_route_t &native_)
    {
        spot_route_t out;
        out.spot_rid = native_routing_id (native_.spot_rid);
        out.owner_node_rid = native_routing_id (native_.owner_node_rid);
        out.spot_kind_value = static_cast<spot_kind> (native_.spot_kind);
        return out;
    }

    static spot_dispatch_info_t from_native (const zlink_spot_dispatch_info_t &native_)
    {
        spot_dispatch_info_t out;
        out.event = static_cast<spot_dispatch_event_t> (native_.event);
        out.subject_kind = static_cast<spot_dispatch_subject_kind_t> (native_.subject_kind);
        if (out.subject_kind == spot_dispatch_subject_kind_t::actor && native_.subject) {
            out.actor = from_native (*static_cast<const zlink_actor_ref_t *> (native_.subject));
        }
        return out;
    }
};

inline const zlink_actor_ref_t *actor_ref_native (const actor_ref_t &actor_) noexcept
{
    return actor_model_access_t::native_ptr (actor_);
}

} // namespace zlink::detail

#endif
