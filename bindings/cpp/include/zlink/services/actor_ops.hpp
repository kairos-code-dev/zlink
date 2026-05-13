/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_ACTOR_OPS_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_ACTOR_OPS_HPP_INCLUDED

#define ZLINK_CPP_SERVICES_SPOT_NODE_NO_SPOT_INCLUDE
#include "spot_node.hpp"
#undef ZLINK_CPP_SERVICES_SPOT_NODE_NO_SPOT_INCLUDE
#include "../request_reply.hpp"

namespace zlink
{
namespace service
{

// --- Actor operation builders ---

namespace detail
{

struct actor_join_state_t
{
    void *node = NULL;
    actor_ref_t actor;
    routing_id_t dest_node_rid {zlink::detail::unchecked_empty_routing_id ()};
    routing_id_t dest_spot_rid {zlink::detail::unchecked_empty_routing_id ()};
    std::vector<message_t> parts;
    send_flags_t flags = send_flags_t::none;
    std::chrono::milliseconds timeout {};
};

struct actor_join_reply_state_t
{
    void *spot = NULL;
    actor_join_info_t info;
    bool accepted = false;
    std::vector<message_t> parts;
};

struct actor_payloadless_state_t
{
    void *node = NULL;
    actor_ref_t actor;
    routing_id_t aux_rid {zlink::detail::unchecked_empty_routing_id ()};
    bool has_aux_rid = false;
    std::string actor_id;
    std::chrono::milliseconds timeout {};
};

struct actor_bind_state_t
{
    void *stream = NULL;
    routing_id_t session_rid {zlink::detail::unchecked_empty_routing_id ()};
    actor_ref_t actor;
    std::string actor_id;
    std::chrono::milliseconds timeout {};
};

struct actor_join_result_state_t
{
    std::unique_ptr<std::promise<actor_join_result_t>> promise;
    actor_join_callback_t on_complete;
    actor_join_result_t result;
    std::vector<message_t> parts;
};

inline actor_join_result_state_t *make_future_actor_join_state ()
{
    actor_join_result_state_t *state = new actor_join_result_state_t ();
    state->promise.reset (new std::promise<actor_join_result_t> ());
    return state;
}

inline actor_join_result_state_t *
make_callback_actor_join_state (actor_join_callback_t callback_)
{
    actor_join_result_state_t *state = new actor_join_result_state_t ();
    state->on_complete = std::move (callback_);
    return state;
}

inline void actor_join_result_trampoline (
  const zlink_actor_join_result_t *result_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_)
{
    actor_join_result_state_t *state =
      static_cast<actor_join_result_state_t *> (userdata_);
    if (!state)
        return;
    std::unique_ptr<actor_join_result_state_t> holder (state);
    actor_join_result_t result;
    if (result_)
        result = actor_join_result_t (*result_);
    else
        result.result = request_result_t::internal_error;

    std::vector<message_t> parts =
      detail::take_parts_from_native (parts_, part_count_);

    if (holder->on_complete) {
        holder->on_complete (result, std::move (parts));
        return;
    }
    if (holder->promise) {
        if (result.result != request_result_t::ok)
            holder->promise->set_exception (std::make_exception_ptr (
              request_error_t (result.result)));
        else {
            holder->result = result;
            holder->parts = std::move (parts);
            holder->promise->set_value (holder->result);
        }
    }
}

struct actor_lookup_result_state_t
{
    std::unique_ptr<std::promise<actor_lookup_result_t>> promise;
    actor_lookup_callback_t on_complete;
};

inline actor_lookup_result_state_t *make_future_actor_lookup_state ()
{
    actor_lookup_result_state_t *state = new actor_lookup_result_state_t ();
    state->promise.reset (new std::promise<actor_lookup_result_t> ());
    return state;
}

inline actor_lookup_result_state_t *
make_callback_actor_lookup_state (actor_lookup_callback_t callback_)
{
    actor_lookup_result_state_t *state = new actor_lookup_result_state_t ();
    state->on_complete = std::move (callback_);
    return state;
}

inline void actor_lookup_result_trampoline (
  const zlink_actor_lookup_result_t *result_,
  void *userdata_)
{
    actor_lookup_result_state_t *state =
      static_cast<actor_lookup_result_state_t *> (userdata_);
    if (!state)
        return;
    std::unique_ptr<actor_lookup_result_state_t> holder (state);
    actor_lookup_result_t result;
    if (result_)
        result = actor_lookup_result_t (*result_);
    else
        result.result = request_result_t::internal_error;
    if (holder->on_complete) {
        holder->on_complete (result);
        return;
    }
    if (holder->promise) {
        if (result.result != request_result_t::ok)
            holder->promise->set_exception (std::make_exception_ptr (
              request_error_t (result.result)));
        else
            holder->promise->set_value (result);
    }
}

} // namespace detail

class actor_join_callback_ready_op_t;
class actor_join_ready_op_t;
class actor_bind_op_t;
class actor_unbind_op_t;

inline actor_bind_op_t
detail_make_actor_bind_op (detail::actor_bind_state_t state_);
inline actor_unbind_op_t
detail_make_actor_unbind_op (detail::actor_bind_state_t state_);

class actor_join_op_t
{
  public:
    actor_join_op_t (actor_join_op_t &&) noexcept = default;
    actor_join_op_t &operator= (actor_join_op_t &&) noexcept = default;

    actor_join_ready_op_t message (message_t &part_) &&;

  private:
    explicit actor_join_op_t (detail::actor_join_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::actor_join_state_t _state;
    friend class spot_node_t;
    friend class actor_t;
};

class actor_join_ready_op_t
{
  public:
    actor_join_ready_op_t (actor_join_ready_op_t &&) noexcept = default;
    actor_join_ready_op_t &
    operator= (actor_join_ready_op_t &&) noexcept = default;

    actor_join_ready_op_t &&message (message_t &part_) &&
    {
        _state.parts.push_back (std::move (part_));
        return std::move (*this);
    }

    actor_join_ready_op_t &&timeout (std::chrono::milliseconds timeout_) &&
    {
        _state.timeout = timeout_;
        return std::move (*this);
    }

    actor_join_callback_ready_op_t flags (int flags_) &&;
    async_result_t<actor_join_result_t> submit_async () &&;
    bool submit (actor_join_callback_t callback_) &&;

  private:
    explicit actor_join_ready_op_t (detail::actor_join_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::actor_join_state_t _state;
    friend class actor_join_op_t;
    friend class actor_join_callback_ready_op_t;
};

class actor_join_callback_ready_op_t
{
  public:
    actor_join_callback_ready_op_t (actor_join_callback_ready_op_t &&) noexcept =
      default;
    actor_join_callback_ready_op_t &
    operator= (actor_join_callback_ready_op_t &&) noexcept = default;

    actor_join_callback_ready_op_t &&message (message_t &part_) &&
    {
        _state.parts.push_back (std::move (part_));
        return std::move (*this);
    }

    actor_join_callback_ready_op_t &&
    timeout (std::chrono::milliseconds timeout_) &&
    {
        _state.timeout = timeout_;
        return std::move (*this);
    }

    actor_join_callback_ready_op_t &&flags (int flags_) &&
    {
        _state.flags = send_flags_t (flags_);
        return std::move (*this);
    }

    bool submit (actor_join_callback_t callback_) &&;

  private:
    explicit actor_join_callback_ready_op_t (detail::actor_join_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::actor_join_state_t _state;
    friend class actor_join_ready_op_t;
};

inline actor_join_ready_op_t actor_join_op_t::message (message_t &part_) &&
{
    _state.parts.push_back (std::move (part_));
    return actor_join_ready_op_t (std::move (_state));
}

namespace detail
{

inline int submit_actor_join (
  detail::actor_join_state_t &state_,
  detail::actor_join_result_state_t *result_state_)
{
    if (!state_.node || state_.parts.empty ())
        return ZLINK_SUBMIT_INVALID_HANDLE;

    std::vector<zlink_msg_t> native;
    if (detail::move_parts_to_native (state_.parts, native) != 0)
        return ZLINK_SUBMIT_INVALID_HANDLE;

    const zlink_submit_result_t rc = zlink_spot_node_actor_join_spot (
      state_.node, zlink::detail::actor_ref_native (state_.actor),
      zlink::detail::routing_id_native (state_.dest_node_rid),
      zlink::detail::routing_id_native (state_.dest_spot_rid),
      native.data (), native.size (),
      &detail::actor_join_result_trampoline, result_state_,
      static_cast<zlink_send_flags_t> (state_.flags),
      static_cast<uint32_t> (state_.timeout.count ()));

    if (rc != ZLINK_SUBMIT_OK)
        detail::restore_parts_from_native (state_.parts, native);
    return rc;
}

} // namespace detail

inline actor_join_callback_ready_op_t actor_join_ready_op_t::flags (int flags_) &&
{
    _state.flags = send_flags_t (flags_);
    return actor_join_callback_ready_op_t (std::move (_state));
}

inline async_result_t<actor_join_result_t>
actor_join_ready_op_t::submit_async () &&
{
    detail::actor_join_result_state_t *state =
      detail::make_future_actor_join_state ();
    std::future<actor_join_result_t> future = state->promise->get_future ();
    const int rc = detail::submit_actor_join (_state, state);
    if (rc != ZLINK_SUBMIT_OK) {
        delete state;
        throw submit_error_t (
          static_cast<submit_result_t> (rc), zlink_errno ());
    }
    return async_result_t<actor_join_result_t> (std::move (future));
}

inline bool
actor_join_ready_op_t::submit (actor_join_callback_t callback_) &&
{
    actor_join_callback_ready_op_t ready (std::move (_state));
    return std::move (ready).submit (std::move (callback_));
}

inline bool
actor_join_callback_ready_op_t::submit (actor_join_callback_t callback_) &&
{
    detail::actor_join_result_state_t *state =
      detail::make_callback_actor_join_state (std::move (callback_));
    const int rc = detail::submit_actor_join (_state, state);
    if (rc != ZLINK_SUBMIT_OK) {
        delete state;
        if (_state.flags == send_flags_t::dontwait
            && static_cast<submit_result_t> (rc)
                 == submit_result_t::backpressured)
            return false;
        throw submit_error_t (
          static_cast<submit_result_t> (rc), zlink_errno ());
    }
    return true;
}

class actor_join_reply_op_t
{
  public:
    actor_join_reply_op_t (actor_join_reply_op_t &&) noexcept = default;
    actor_join_reply_op_t &
    operator= (actor_join_reply_op_t &&) noexcept = default;

    actor_join_reply_op_t &&message (message_t &part_) &&
    {
        _state.parts.push_back (std::move (part_));
        return std::move (*this);
    }

    void submit () &&
    {
        if (!_state.spot)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

        zlink_actor_join_info_t native_info = _state.info.native ();
        if (_state.parts.empty ()) {
            const submit_result_t rc = static_cast<submit_result_t> (
              zlink_spot_actor_join_reply (
                _state.spot, &native_info,
                _state.accepted ? 1u : 0u, NULL, 0u));
            if (rc != submit_result_t::ok)
                throw submit_error_t (rc, zlink_errno ());
            return;
        }

        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (_state.parts, native) != 0)
            throw last_error ();
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_spot_actor_join_reply (
            _state.spot, &native_info,
            _state.accepted ? 1u : 0u, native.data (), native.size ()));
        if (rc != submit_result_t::ok) {
            detail::restore_parts_from_native (_state.parts, native);
            throw submit_error_t (rc, zlink_errno ());
        }
    }

  private:
    explicit actor_join_reply_op_t (detail::actor_join_reply_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::actor_join_reply_state_t _state;
    friend class spot_t;
};

namespace detail
{

// Payload-less request/reply (leave/destroy): wraps zlink_reply_handler_fn.
template<typename SubmitFn>
inline int submit_payloadless_request (
  detail::actor_payloadless_state_t &state_,
  detail::request_state_t *request_state_,
  SubmitFn submit_)
{
    if (!state_.node)
        return ZLINK_SUBMIT_INVALID_HANDLE;
    const zlink_submit_result_t rc = submit_ (
      state_, &detail::request_callback_trampoline, request_state_);
    return rc;
}

} // namespace detail

class actor_leave_op_t
{
  public:
    actor_leave_op_t (actor_leave_op_t &&) noexcept = default;
    actor_leave_op_t &operator= (actor_leave_op_t &&) noexcept = default;

    actor_leave_op_t &&timeout (std::chrono::milliseconds timeout_) &&
    {
        _state.timeout = timeout_;
        return std::move (*this);
    }

    async_result_t<std::vector<message_t>> submit_async () &&
    {
        detail::request_state_t *state = detail::make_future_request_state ();
        std::future<std::vector<message_t>> future =
          state->promise->get_future ();
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_spot_node_actor_leave_spot (
            _state.node, zlink::detail::actor_ref_native (_state.actor),
            zlink::detail::routing_id_native (_state.aux_rid),
            &detail::request_callback_trampoline, state,
            static_cast<uint32_t> (_state.timeout.count ())));
        if (rc != submit_result_t::ok) {
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return async_result_t<std::vector<message_t>> (std::move (future));
    }

    bool submit (request_callback_t callback_) &&
    {
        detail::request_state_t *state =
          detail::make_callback_request_state (std::move (callback_));
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_spot_node_actor_leave_spot (
            _state.node, zlink::detail::actor_ref_native (_state.actor),
            zlink::detail::routing_id_native (_state.aux_rid),
            &detail::request_callback_trampoline, state,
            static_cast<uint32_t> (_state.timeout.count ())));
        if (rc != submit_result_t::ok) {
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

  private:
    explicit actor_leave_op_t (detail::actor_payloadless_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::actor_payloadless_state_t _state;
    friend class spot_node_t;
    friend class actor_t;
};

class actor_destroy_op_t
{
  public:
    actor_destroy_op_t (actor_destroy_op_t &&) noexcept = default;
    actor_destroy_op_t &operator= (actor_destroy_op_t &&) noexcept = default;

    actor_destroy_op_t &&timeout (std::chrono::milliseconds timeout_) &&
    {
        _state.timeout = timeout_;
        return std::move (*this);
    }

    async_result_t<std::vector<message_t>> submit_async () &&
    {
        detail::request_state_t *state = detail::make_future_request_state ();
        std::future<std::vector<message_t>> future =
          state->promise->get_future ();
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_spot_node_actor_destroy (
            _state.node, zlink::detail::actor_ref_native (_state.actor),
            &detail::request_callback_trampoline, state,
            static_cast<uint32_t> (_state.timeout.count ())));
        if (rc != submit_result_t::ok) {
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return async_result_t<std::vector<message_t>> (std::move (future));
    }

    bool submit (request_callback_t callback_) &&
    {
        detail::request_state_t *state =
          detail::make_callback_request_state (std::move (callback_));
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_spot_node_actor_destroy (
            _state.node, zlink::detail::actor_ref_native (_state.actor),
            &detail::request_callback_trampoline, state,
            static_cast<uint32_t> (_state.timeout.count ())));
        if (rc != submit_result_t::ok) {
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

  private:
    explicit actor_destroy_op_t (detail::actor_payloadless_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::actor_payloadless_state_t _state;
    friend class spot_node_t;
};

class actor_lookup_op_t
{
  public:
    actor_lookup_op_t (actor_lookup_op_t &&) noexcept = default;
    actor_lookup_op_t &operator= (actor_lookup_op_t &&) noexcept = default;

    actor_lookup_op_t &&timeout (std::chrono::milliseconds timeout_) &&
    {
        _state.timeout = timeout_;
        return std::move (*this);
    }

    async_result_t<actor_lookup_result_t> submit_async () &&
    {
        detail::actor_lookup_result_state_t *state =
          detail::make_future_actor_lookup_state ();
        std::future<actor_lookup_result_t> future =
          state->promise->get_future ();
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_remote_actor_get_ref (
            _state.node, zlink::detail::routing_id_native (_state.aux_rid),
            _state.actor_id.c_str (),
            &detail::actor_lookup_result_trampoline, state,
            static_cast<uint32_t> (_state.timeout.count ())));
        if (rc != submit_result_t::ok) {
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return async_result_t<actor_lookup_result_t> (std::move (future));
    }

    bool submit (actor_lookup_callback_t callback_) &&
    {
        detail::actor_lookup_result_state_t *state =
          detail::make_callback_actor_lookup_state (std::move (callback_));
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_remote_actor_get_ref (
            _state.node, zlink::detail::routing_id_native (_state.aux_rid),
            _state.actor_id.c_str (),
            &detail::actor_lookup_result_trampoline, state,
            static_cast<uint32_t> (_state.timeout.count ())));
        if (rc != submit_result_t::ok) {
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

  private:
    explicit actor_lookup_op_t (detail::actor_payloadless_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::actor_payloadless_state_t _state;
    friend class spot_node_t;
};

class actor_bind_op_t
{
  public:
    actor_bind_op_t (actor_bind_op_t &&) noexcept = default;
    actor_bind_op_t &operator= (actor_bind_op_t &&) noexcept = default;

    actor_bind_op_t &&timeout (std::chrono::milliseconds timeout_) &&
    {
        _state.timeout = timeout_;
        return std::move (*this);
    }

    async_result_t<std::vector<message_t>> submit_async () &&
    {
        detail::request_state_t *state = detail::make_future_request_state ();
        std::future<std::vector<message_t>> future =
          state->promise->get_future ();
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_stream_bind_actor (
            _state.stream,
            zlink::detail::routing_id_native (_state.session_rid),
            zlink::detail::actor_ref_native (_state.actor),
            &detail::request_callback_trampoline, state,
            static_cast<uint32_t> (_state.timeout.count ())));
        if (rc != submit_result_t::ok) {
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return async_result_t<std::vector<message_t>> (std::move (future));
    }

    bool submit (request_callback_t callback_) &&
    {
        detail::request_state_t *state =
          detail::make_callback_request_state (std::move (callback_));
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_stream_bind_actor (
            _state.stream,
            zlink::detail::routing_id_native (_state.session_rid),
            zlink::detail::actor_ref_native (_state.actor),
            &detail::request_callback_trampoline, state,
            static_cast<uint32_t> (_state.timeout.count ())));
        if (rc != submit_result_t::ok) {
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

  private:
    explicit actor_bind_op_t (detail::actor_bind_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::actor_bind_state_t _state;
    friend inline actor_bind_op_t
    detail_make_actor_bind_op (detail::actor_bind_state_t state_);
};

class actor_unbind_op_t
{
  public:
    actor_unbind_op_t (actor_unbind_op_t &&) noexcept = default;
    actor_unbind_op_t &operator= (actor_unbind_op_t &&) noexcept = default;

    actor_unbind_op_t &&timeout (std::chrono::milliseconds timeout_) &&
    {
        _state.timeout = timeout_;
        return std::move (*this);
    }

    async_result_t<std::vector<message_t>> submit_async () &&
    {
        detail::request_state_t *state = detail::make_future_request_state ();
        std::future<std::vector<message_t>> future =
          state->promise->get_future ();
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_stream_unbind_actor (
            _state.stream,
            zlink::detail::routing_id_native (_state.session_rid),
            _state.actor_id.c_str (),
            &detail::request_callback_trampoline, state,
            static_cast<uint32_t> (_state.timeout.count ())));
        if (rc != submit_result_t::ok) {
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return async_result_t<std::vector<message_t>> (std::move (future));
    }

    bool submit (request_callback_t callback_) &&
    {
        detail::request_state_t *state =
          detail::make_callback_request_state (std::move (callback_));
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_stream_unbind_actor (
            _state.stream,
            zlink::detail::routing_id_native (_state.session_rid),
            _state.actor_id.c_str (),
            &detail::request_callback_trampoline, state,
            static_cast<uint32_t> (_state.timeout.count ())));
        if (rc != submit_result_t::ok) {
            delete state;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

  private:
    explicit actor_unbind_op_t (detail::actor_bind_state_t state_)
        : _state (std::move (state_))
    {
    }

    detail::actor_bind_state_t _state;
    friend inline actor_unbind_op_t
    detail_make_actor_unbind_op (detail::actor_bind_state_t state_);
};

inline actor_bind_op_t
detail_make_actor_bind_op (detail::actor_bind_state_t state_)
{
    return actor_bind_op_t (std::move (state_));
}

inline actor_unbind_op_t
detail_make_actor_unbind_op (detail::actor_bind_state_t state_)
{
    return actor_unbind_op_t (std::move (state_));
}

inline actor_destroy_op_t spot_node_t::destroy_actor (
  const actor_ref_t &actor_)
{
    detail::actor_payloadless_state_t state;
    state.node = _node;
    state.actor = actor_;
    return actor_destroy_op_t (std::move (state));
}

inline actor_join_op_t spot_node_t::join_actor (
  const actor_ref_t &actor_,
  const routing_id_t &dest_node_rid_,
  const routing_id_t &dest_spot_rid_)
{
    detail::actor_join_state_t state;
    state.node = _node;
    state.actor = actor_;
    state.dest_node_rid = dest_node_rid_;
    state.dest_spot_rid = dest_spot_rid_;
    return actor_join_op_t (std::move (state));
}

inline actor_leave_op_t spot_node_t::leave_actor (
  const actor_ref_t &actor_,
  const routing_id_t &current_spot_rid_)
{
    detail::actor_payloadless_state_t state;
    state.node = _node;
    state.actor = actor_;
    state.aux_rid = current_spot_rid_;
    state.has_aux_rid = true;
    return actor_leave_op_t (std::move (state));
}

inline actor_lookup_op_t spot_node_t::remote_actor_get_ref (
  const routing_id_t &target_node_rid_, const std::string &actor_id_)
{
    zlink::detail::validate_bounded_c_string (
      actor_id_, ZLINK_ACTOR_ID_MAX - 1u, "actor_id");
    detail::actor_payloadless_state_t state;
    state.node = _node;
    state.aux_rid = target_node_rid_;
    state.has_aux_rid = true;
    state.actor_id = actor_id_;
    return actor_lookup_op_t (std::move (state));
}

inline send_op_t spot_node_t::send_bound_session_msg (
  const actor_ref_t &actor_)
{
    detail::spot_op_state_t state;
    state.kind = detail::spot_op_kind_t::bound_session_send;
    state.node = this;
    state.actor = actor_;
    return send_op_t (std::move (state));
}


} // namespace service
} // namespace zlink

#endif
