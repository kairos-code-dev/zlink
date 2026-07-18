/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_SPOT_STATE_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_SPOT_STATE_HPP_INCLUDED

#include <memory>
#include <vector>

#include <zlink/Contracts/Core/routing_id.hpp>
#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/Contracts/Messaging/received.hpp>
#include <zlink/Contracts/Sockets/results.hpp>
#include <zlink/Contracts/Service/actor_models.hpp>
#include <zlink/Contracts/Service/operation_builder_base.hpp>
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
    request_to_actor,
    reply_to_spot,
    reply_to_router,
    // Operation kinds for built-in received_t.send()/reply() builders. The state uses
    // a borrowed reference to the originating received_t.
    received_send,
    received_reply,
    // Operation kind for spot_node_t::send_bound_session_msg(actor).
    bound_session_send,
    actor_send,
    // Operation kind for stream_socket_t::send_bound_actor(session, actor_id).
    stream_bound_actor_send
};

struct spot_operation_state_t
{
    spot_operation_kind_t kind = spot_operation_kind_t::publish;

    struct routing_target_t
    {
        std::optional<routing_id_t> first_rid;
        std::optional<routing_id_t> second_rid;
        zlink_routing_id_t first_rid_native_cache{};
        zlink_routing_id_t second_rid_native_cache{};
        bool has_first_rid_native_cache = false;
        bool has_second_rid_native_cache = false;

        void reset () noexcept
        {
            first_rid.reset ();
            second_rid.reset ();
            first_rid_native_cache = zlink_routing_id_t{};
            second_rid_native_cache = zlink_routing_id_t{};
            has_first_rid_native_cache = false;
            has_second_rid_native_cache = false;
        }
    };

    struct message_parts_t
    {
        std::optional<message_t> single_part;
        message_t *single_part_source = nullptr;
        bool discard_single_part_on_backpressure = false;
        std::vector<message_t> parts;

        void reset () noexcept
        {
            single_part.reset ();
            single_part_source = nullptr;
            discard_single_part_on_backpressure = false;
            parts.clear ();
        }
    };

    struct raw_command_t
    {
        void *socket = nullptr;
        std::string topic;
        routing_target_t target;

        void reset () noexcept
        {
            socket = nullptr;
            topic.clear ();
            target.reset ();
        }
    };

    struct spot_command_t
    {
        spot_t *spot = nullptr;
        std::string topic;
        std::string channel_name;
        routing_target_t target;
        uint64_t request_seq = 0;

        void reset () noexcept
        {
            spot = nullptr;
            topic.clear ();
            channel_name.clear ();
            target.reset ();
            request_seq = 0;
        }
    };

    struct received_command_t
    {
        received_t *received = nullptr;

        void reset () noexcept { received = nullptr; }
    };

    struct actor_command_t
    {
        spot_node_t *node = nullptr;
        std::optional<actor_ref_t> actor;

        void reset () noexcept
        {
            node = nullptr;
            actor.reset ();
        }
    };

    struct stream_actor_command_t
    {
        void *stream = nullptr;
        routing_target_t target;
        std::string actor_id;

        void reset () noexcept
        {
            stream = nullptr;
            target.reset ();
            actor_id.clear ();
        }
    };

    message_parts_t message;
    raw_command_t raw;
    spot_command_t spot;
    received_command_t received;
    actor_command_t actor;
    stream_actor_command_t stream_actor;
    send_flags_t flags = send_flags_t::none;
    std::chrono::milliseconds timeout{};
};

inline void cache_first_rid_native (spot_operation_state_t::routing_target_t &target_,
                                    const routing_id_t &rid_) noexcept
{
    target_.first_rid_native_cache = *zlink::detail::routing_id_native (rid_);
    target_.has_first_rid_native_cache = true;
    target_.first_rid.reset ();
}

inline void cache_second_rid_native (spot_operation_state_t::routing_target_t &target_,
                                     const routing_id_t &rid_) noexcept
{
    target_.second_rid_native_cache = *zlink::detail::routing_id_native (rid_);
    target_.has_second_rid_native_cache = true;
    target_.second_rid.reset ();
}

inline const zlink_routing_id_t *target_first_rid_native (
  const spot_operation_state_t::routing_target_t &target_) noexcept
{
    if (target_.has_first_rid_native_cache)
        return &target_.first_rid_native_cache;
    if (target_.first_rid.has_value ())
        return zlink::detail::routing_id_native (*target_.first_rid);
    return nullptr;
}

inline const zlink_routing_id_t *target_second_rid_native (
  const spot_operation_state_t::routing_target_t &target_) noexcept
{
    if (target_.has_second_rid_native_cache)
        return &target_.second_rid_native_cache;
    if (target_.second_rid.has_value ())
        return zlink::detail::routing_id_native (*target_.second_rid);
    return nullptr;
}

inline bool has_send_parts (const spot_operation_state_t &state_) noexcept
{
    return state_.message.single_part.has_value () || state_.message.single_part_source
           || !state_.message.parts.empty ();
}

inline size_t send_part_count (const spot_operation_state_t &state_) noexcept
{
    return state_.message.single_part.has_value () || state_.message.single_part_source
             ? 1u
             : state_.message.parts.size ();
}

inline message_t &send_single_part (spot_operation_state_t &state_) noexcept
{
    if (state_.message.single_part.has_value ())
        return *state_.message.single_part;
    if (state_.message.single_part_source)
        return *state_.message.single_part_source;
    return state_.message.parts.front ();
}

inline void append_send_part (spot_operation_state_t &state_, message_t &part_)
{
    if (state_.message.single_part.has_value ()) {
        state_.message.parts.push_back (std::move (*state_.message.single_part));
        state_.message.single_part.reset ();
        state_.message.single_part_source = nullptr;
    } else if (state_.message.single_part_source) {
        state_.message.parts.push_back (std::move (*state_.message.single_part_source));
        state_.message.single_part_source = nullptr;
    }
    state_.message.parts.push_back (std::move (part_));
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

inline void restore_single_send_part_to_source (spot_operation_state_t &state_) noexcept
{
    if (!state_.message.single_part_source || !state_.message.single_part.has_value ()
        || !state_.message.single_part->valid ())
        return;
    *state_.message.single_part_source = std::move (*state_.message.single_part);
    state_.message.single_part_source = nullptr;
}

inline void restore_single_send_part_to_source (spot_operation_state_t &state_,
                                                std::vector<message_t> &parts_) noexcept
{
    if (!state_.message.single_part_source || parts_.size () != 1u || !parts_[0].valid ())
        return;
    *state_.message.single_part_source = std::move (parts_[0]);
    state_.message.single_part_source = nullptr;
}

inline void restore_send_parts_to_state (spot_operation_state_t &state_,
                                         std::vector<message_t> &parts_) noexcept
{
    const bool had_single_part_source = state_.message.single_part_source != nullptr;
    restore_single_send_part_to_source (state_, parts_);
    if (had_single_part_source || parts_.empty ())
        return;
    if (parts_.size () == 1u && state_.message.single_part.has_value ()) {
        state_.message.single_part = std::move (parts_[0]);
        return;
    }
    state_.message.parts = std::move (parts_);
}

// Thread-local pool of spot_operation_state_t to avoid per-send heap alloc.
// Each send/request/reply chain acquires one pooled state at the entry factory
// and returns it on submit success. The pool keeps string/vector capacity so
// repeated PAIR/DEALER/PUBSUB sends with empty topic or short topic do not
// trigger malloc/free per call.
inline void reset_for_reuse (spot_operation_state_t &state_) noexcept
{
    // RAW_SEND_HOT_PATH: pair/dealer send only populates the message, raw
    // socket, and flags fields. Keep unrelated service command state out of
    // this per-message reset; every other operation still takes the complete
    // reset below before the pooled state becomes reusable.
    if (state_.kind == spot_operation_kind_t::raw_send) {
        state_.kind = spot_operation_kind_t::publish;
        state_.message.reset ();
        state_.raw.socket = nullptr;
        state_.flags = send_flags_t::none;
        return;
    }

    state_.kind = spot_operation_kind_t::publish;
    state_.message.reset ();
    state_.raw.reset ();
    state_.spot.reset ();
    state_.received.reset ();
    state_.actor.reset ();
    state_.stream_actor.reset ();
    state_.flags = send_flags_t::none;
    state_.timeout = std::chrono::milliseconds{};
}

inline std::vector<std::unique_ptr<spot_operation_state_t>> &state_pool () noexcept
{
    static thread_local std::vector<std::unique_ptr<spot_operation_state_t>> pool;
    return pool;
}

inline std::unique_ptr<spot_operation_state_t> acquire_state ()
{
    auto &pool = state_pool ();
    if (!pool.empty ()) {
        auto state = std::move (pool.back ());
        pool.pop_back ();
        return state;
    }
    return std::make_unique<spot_operation_state_t> ();
}

inline void release_state (std::unique_ptr<spot_operation_state_t> state_ptr_) noexcept
{
    if (!state_ptr_)
        return;
    constexpr size_t k_pool_cap = 8;
    auto &pool = state_pool ();
    if (pool.size () >= k_pool_cap)
        return;
    reset_for_reuse (*state_ptr_);
    pool.push_back (std::move (state_ptr_));
}

inline void pooled_operation_state_policy_t::destroy (
  std::unique_ptr<spot_operation_state_t> state_ptr_) noexcept
{
    release_state (std::move (state_ptr_));
}

} // namespace detail


} // namespace service
} // namespace zlink

#endif
