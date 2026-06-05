/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_ACTOR_DETAIL_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_ACTOR_DETAIL_HPP_INCLUDED

#include "actor_model_access.hpp"
#include "../Core/duration_conversion.hpp"
#include "detail.hpp"
#include "spot_state.hpp"

namespace zlink
{
namespace service
{

class spot_node_t;
class spot_t;

namespace detail
{

struct actor_join_state_t
{
    void *node = nullptr;
    actor_ref_t actor;
    routing_id_t dest_node_rid{zlink::detail::unchecked_empty_routing_id ()};
    routing_id_t dest_spot_rid{zlink::detail::unchecked_empty_routing_id ()};
    std::vector<message_t> parts;
    send_flags_t flags = send_flags_t::none;
    std::chrono::milliseconds timeout{};
};

struct actor_join_reply_state_t
{
    void *spot = nullptr;
    actor_join_info_t info;
    int32_t join_result_code = 0;
    std::vector<message_t> parts;
};

struct actor_payloadless_state_t
{
    void *node = nullptr;
    actor_ref_t actor;
    routing_id_t aux_rid{zlink::detail::unchecked_empty_routing_id ()};
    bool has_aux_rid = false;
    std::string actor_id;
    std::chrono::milliseconds timeout{};
};

struct actor_bind_state_t
{
    void *stream = nullptr;
    routing_id_t session_rid{zlink::detail::unchecked_empty_routing_id ()};
    actor_ref_t actor;
    std::string actor_id;
    std::chrono::milliseconds timeout{};
};

struct actor_join_result_state_t
{
    std::unique_ptr<std::promise<actor_join_result_t>> promise;
    actor_join_callback_t on_complete;
    actor_join_result_t result;
    std::vector<message_t> parts;
};

struct actor_join_entry_spot_result_state_t
{
    std::unique_ptr<std::promise<actor_join_entry_spot_result_t>> promise;
    actor_join_entry_spot_callback_t on_complete;
};

inline actor_join_result_state_t *make_future_actor_join_state ()
{
    auto state = std::make_unique<actor_join_result_state_t> ();
    state->promise = std::make_unique<std::promise<actor_join_result_t>> ();
    return state.release ();
}

inline actor_join_result_state_t *make_callback_actor_join_state (actor_join_callback_t callback_)
{
    auto state = std::make_unique<actor_join_result_state_t> ();
    state->on_complete = std::move (callback_);
    return state.release ();
}

inline actor_join_entry_spot_result_state_t *make_future_actor_join_entry_spot_state ()
{
    auto state = std::make_unique<actor_join_entry_spot_result_state_t> ();
    state->promise = std::make_unique<std::promise<actor_join_entry_spot_result_t>> ();
    return state.release ();
}

inline actor_join_entry_spot_result_state_t *
make_callback_actor_join_entry_spot_state (actor_join_entry_spot_callback_t callback_)
{
    auto state = std::make_unique<actor_join_entry_spot_result_state_t> ();
    state->on_complete = std::move (callback_);
    return state.release ();
}

inline void actor_join_result_trampoline (const zlink_actor_join_result_t *result_,
                                          zlink_msg_t *parts_,
                                          size_t part_count_,
                                          void *userdata_)
{
    actor_join_result_state_t *state = static_cast<actor_join_result_state_t *> (userdata_);
    if (!state)
        return;
    std::unique_ptr<actor_join_result_state_t> holder (state);
    actor_join_result_t result;
    if (result_)
        result = zlink::detail::actor_model_access_t::from_native (*result_);
    else
        result.result = request_result_t::internal_error;

    std::vector<message_t> parts = detail::take_parts_from_native (parts_, part_count_);

    if (holder->on_complete) {
        holder->on_complete (result, std::move (parts));
        return;
    }
    if (holder->promise) {
        if (result.result != request_result_t::ok)
            holder->promise->set_exception (std::make_exception_ptr (request_error_t (result.result)));
        else {
            holder->result = result;
            holder->parts = std::move (parts);
            holder->promise->set_value (holder->result);
        }
    }
}

inline void actor_join_entry_spot_result_trampoline (const zlink_actor_join_entry_spot_result_t *result_,
                                                     void *userdata_)
{
    actor_join_entry_spot_result_state_t *state = static_cast<actor_join_entry_spot_result_state_t *> (userdata_);
    if (!state)
        return;
    std::unique_ptr<actor_join_entry_spot_result_state_t> holder (state);
    actor_join_entry_spot_result_t result;
    if (result_)
        result = zlink::detail::actor_model_access_t::from_native (*result_);
    else
        result.result = request_result_t::internal_error;
    if (holder->on_complete) {
        holder->on_complete (result);
        return;
    }
    if (holder->promise) {
        if (result.result != request_result_t::ok)
            holder->promise->set_exception (std::make_exception_ptr (request_error_t (result.result)));
        else
            holder->promise->set_value (result);
    }
}

struct actor_lookup_result_state_t
{
    std::unique_ptr<std::promise<actor_lookup_result_t>> promise;
    actor_lookup_callback_t on_complete;
};

inline actor_lookup_result_state_t *make_future_actor_lookup_state ()
{
    auto state = std::make_unique<actor_lookup_result_state_t> ();
    state->promise = std::make_unique<std::promise<actor_lookup_result_t>> ();
    return state.release ();
}

inline actor_lookup_result_state_t *make_callback_actor_lookup_state (actor_lookup_callback_t callback_)
{
    auto state = std::make_unique<actor_lookup_result_state_t> ();
    state->on_complete = std::move (callback_);
    return state.release ();
}

inline void actor_lookup_result_trampoline (const zlink_actor_lookup_result_t *result_, void *userdata_)
{
    actor_lookup_result_state_t *state = static_cast<actor_lookup_result_state_t *> (userdata_);
    if (!state)
        return;
    std::unique_ptr<actor_lookup_result_state_t> holder (state);
    actor_lookup_result_t result;
    if (result_)
        result = zlink::detail::actor_model_access_t::from_native (*result_);
    else
        result.result = request_result_t::internal_error;
    if (holder->on_complete) {
        holder->on_complete (result);
        return;
    }
    if (holder->promise) {
        if (result.result != request_result_t::ok)
            holder->promise->set_exception (std::make_exception_ptr (request_error_t (result.result)));
        else
            holder->promise->set_value (result);
    }
}

inline int submit_actor_join (detail::actor_join_state_t &state_, detail::actor_join_result_state_t *result_state_)
{
    if (!state_.node || state_.parts.empty ())
        return ZLINK_SUBMIT_INVALID_HANDLE;

    const zlink_routing_id_t dest_node_rid = zlink::detail::routing_id_native_value (state_.dest_node_rid);
    const zlink_routing_id_t dest_spot_rid = zlink::detail::routing_id_native_value (state_.dest_spot_rid);
    const int rc = submit_message_array (state_.parts, [&] (zlink_msg_t *native_, size_t part_count_) {
        return zlink_spot_node_actor_join_spot (state_.node, zlink::detail::actor_ref_native (state_.actor),
                                                &dest_node_rid, &dest_spot_rid, native_, part_count_,
                                                &detail::actor_join_result_trampoline, result_state_,
                                                static_cast<zlink_send_flags_t> (static_cast<int> (state_.flags)),
                                                zlink::detail::native_timeout_ms (state_.timeout));
    });
    return rc == -1 ? ZLINK_SUBMIT_INVALID_HANDLE : rc;
}

template <typename SubmitFn>
inline int submit_payloadless_request (detail::actor_payloadless_state_t &state_,
                                       detail::request_state_t *request_state_,
                                       SubmitFn submit_)
{
    if (!state_.node)
        return ZLINK_SUBMIT_INVALID_HANDLE;
    const zlink_submit_result_t rc = submit_ (state_, &detail::request_callback_trampoline, request_state_);
    return rc;
}

} // namespace detail
} // namespace service
} // namespace zlink

#endif
