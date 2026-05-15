/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_SPOT_STATE_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_SPOT_STATE_HPP_INCLUDED

#include "spot_common.hpp"

namespace zlink
{
namespace service
{

namespace detail
{
enum class spot_op_kind_t
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
    // Op kinds for built-in received_t.send()/reply() builders. The state uses
    // a borrowed reference to the originating received_t.
    received_send,
    received_reply,
    // Op kind for spot_node_t::send_bound_session_msg(actor).
    bound_session_send,
    // Op kind for stream_socket_t::send_bound_actor(session, actor_id).
    stream_bound_actor_send
};

struct spot_op_state_t
{
    spot_t *spot = NULL;
    spot_op_kind_t kind = spot_op_kind_t::publish;
    std::string topic;
    std::string channel_name;
    std::optional<routing_id_t> first_rid;
    std::optional<routing_id_t> second_rid;
    uint64_t request_seq = 0;
    std::optional<message_t> single_part;
    std::vector<message_t> parts;
    send_flags_t flags = send_flags_t::none;
    std::chrono::milliseconds timeout {};
    // Borrowed raw socket handle for raw socket send/publish builders.
    void *raw_socket = NULL;
    // Borrowed reference to the originating received_t for received_send /
    // received_reply ops. Lifetime is managed by the caller.
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

inline bool has_send_parts (const spot_op_state_t &state_) noexcept
{
    return state_.single_part.has_value () || !state_.parts.empty ();
}

inline size_t send_part_count (const spot_op_state_t &state_) noexcept
{
    return state_.single_part.has_value () ? 1u : state_.parts.size ();
}

inline message_t &send_single_part (spot_op_state_t &state_) noexcept
{
    return state_.single_part.has_value () ? *state_.single_part
                                           : state_.parts.front ();
}

inline void append_send_part (spot_op_state_t &state_, message_t &part_)
{
    if (state_.single_part.has_value ()) {
        state_.parts.push_back (std::move (*state_.single_part));
        state_.single_part.reset ();
    }
    state_.parts.push_back (std::move (part_));
}
} // namespace detail



} // namespace service
} // namespace zlink

#endif
