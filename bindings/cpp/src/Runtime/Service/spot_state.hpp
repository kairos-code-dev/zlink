/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_SPOT_STATE_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_SPOT_STATE_HPP_INCLUDED

#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/Contracts/Messaging/received.hpp>
#include <zlink/Contracts/Sockets/results.hpp>
#include <zlink/Contracts/Service/actor_models.hpp>
#include "../Core/routing_id_access.hpp"

namespace zlink
{
namespace service
{

class spot_t;
class spot_node_t;

namespace detail
{
enum class spot_operation_kind_t
{
    raw_send,
    raw_routed_send,
    raw_publish,
    raw_router_send_spot,
    raw_request,
    raw_routed_request,
    raw_router_request_spot,
    raw_reply,
    raw_router_reply_spot,
    publish,
    send_channel,
    send_to_spot,
    request_channel,
    request_to_spot,
    request_to_router,
    reply_to_spot,
    reply_to_router,
    // Operation kinds for built-in received_t.send()/reply() builders. The state uses
    // a borrowed reference to the originating received_t.
    received_send,
    received_reply,
    // Operation kind for spot_node_t::send_bound_session_msg(actor).
    bound_session_send,
    // Operation kind for stream_socket_t::send_bound_actor(session, actor_id).
    stream_bound_actor_send
};

struct spot_operation_state_t
{
    spot_t *spot = NULL;
    spot_operation_kind_t kind = spot_operation_kind_t::publish;
    std::string topic;
    std::string channel_name;
    std::optional<routing_id_t> first_rid;
    std::optional<routing_id_t> second_rid;
    zlink_routing_id_t first_rid_native_cache{};
    zlink_routing_id_t second_rid_native_cache{};
    bool has_first_rid_native_cache = false;
    bool has_second_rid_native_cache = false;
    uint64_t request_seq = 0;
    std::optional<message_t> single_part;
    message_t *single_part_source = NULL;
    bool discard_single_part_on_backpressure = false;
    std::vector<message_t> parts;
    send_flags_t flags = send_flags_t::none;
    std::chrono::milliseconds timeout{};
    // Borrowed raw socket handle for raw socket send/publish builders.
    void *raw_socket = NULL;
    // Borrowed reference to the originating received_t for received_send /
    // received_reply operations. Lifetime is managed by the caller.
    received_t *received = NULL;
    // Borrowed spot_node_t for actor-based operations (bound_session_send).
    spot_node_t *node = NULL;
    // Borrowed stream handle for stream-bound actor sends.
    void *stream = NULL;
    // Actor reference for actor-based operations (bound_session_send).
    std::optional<actor_ref_t> actor;
    // Actor id for stream-bound actor sends.
    std::string actor_id;
};

inline void cache_first_rid_native (spot_operation_state_t &state_,
                                    const routing_id_t &rid_) noexcept
{
    state_.first_rid_native_cache = *zlink::detail::routing_id_native (rid_);
    state_.has_first_rid_native_cache = true;
    state_.first_rid.reset ();
}

inline void cache_second_rid_native (spot_operation_state_t &state_,
                                     const routing_id_t &rid_) noexcept
{
    state_.second_rid_native_cache = *zlink::detail::routing_id_native (rid_);
    state_.has_second_rid_native_cache = true;
    state_.second_rid.reset ();
}

inline const zlink_routing_id_t *
state_first_rid_native (const spot_operation_state_t &state_) noexcept
{
    if (state_.has_first_rid_native_cache)
        return &state_.first_rid_native_cache;
    if (state_.first_rid.has_value ())
        return zlink::detail::routing_id_native (*state_.first_rid);
    return NULL;
}

inline const zlink_routing_id_t *
state_second_rid_native (const spot_operation_state_t &state_) noexcept
{
    if (state_.has_second_rid_native_cache)
        return &state_.second_rid_native_cache;
    if (state_.second_rid.has_value ())
        return zlink::detail::routing_id_native (*state_.second_rid);
    return NULL;
}

inline bool has_send_parts (const spot_operation_state_t &state_) noexcept
{
    return state_.single_part.has_value () || state_.single_part_source
           || !state_.parts.empty ();
}

inline size_t send_part_count (const spot_operation_state_t &state_) noexcept
{
    return state_.single_part.has_value () || state_.single_part_source
             ? 1u
             : state_.parts.size ();
}

inline message_t &send_single_part (spot_operation_state_t &state_) noexcept
{
    if (state_.single_part.has_value ())
        return *state_.single_part;
    if (state_.single_part_source)
        return *state_.single_part_source;
    return state_.parts.front ();
}

inline void append_send_part (spot_operation_state_t &state_, message_t &part_)
{
    if (state_.single_part.has_value ()) {
        state_.parts.push_back (std::move (*state_.single_part));
        state_.single_part.reset ();
        state_.single_part_source = NULL;
    } else if (state_.single_part_source) {
        state_.parts.push_back (std::move (*state_.single_part_source));
        state_.single_part_source = NULL;
    }
    state_.parts.push_back (std::move (part_));
}

inline bool can_borrow_single_send_part (spot_operation_kind_t kind_) noexcept
{
    switch (kind_) {
        case spot_operation_kind_t::raw_send:
        case spot_operation_kind_t::raw_routed_send:
        case spot_operation_kind_t::raw_publish:
        case spot_operation_kind_t::raw_router_send_spot:
        case spot_operation_kind_t::publish:
            return true;
        default:
            return false;
    }
}

inline void
restore_single_send_part_to_source (spot_operation_state_t &state_) noexcept
{
    if (!state_.single_part_source || !state_.single_part.has_value ()
        || !state_.single_part->valid ())
        return;
    *state_.single_part_source = std::move (*state_.single_part);
    state_.single_part_source = NULL;
}

inline void
restore_single_send_part_to_source (spot_operation_state_t &state_,
                                    std::vector<message_t> &parts_) noexcept
{
    if (!state_.single_part_source || parts_.size () != 1u
        || !parts_[0].valid ())
        return;
    *state_.single_part_source = std::move (parts_[0]);
    state_.single_part_source = NULL;
}
} // namespace detail


} // namespace service
} // namespace zlink

#endif
