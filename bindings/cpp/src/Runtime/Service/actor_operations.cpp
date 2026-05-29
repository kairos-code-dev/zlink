/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Service/spot.hpp>
#include <Runtime/Core/duration_conversion.hpp>
#include <Runtime/Service/actor_detail.hpp>
#include <Runtime/Service/spot_access.hpp>
#include <Runtime/Native/native_parts.hpp>

namespace zlink
{
namespace service
{

namespace
{

template <typename Submit>
async_result_t<std::vector<message_t> >
submit_actor_request_async (Submit submit_)
{
    std::unique_ptr<detail::request_state_t> request_state (
      detail::make_future_request_state ());
    std::future<std::vector<message_t> > future =
      request_state->promise->get_future ();
    const submit_result_t rc =
      static_cast<submit_result_t> (submit_ (request_state.get ()));
    if (rc != submit_result_t::ok)
        throw submit_error_t (rc, zlink_errno ());
    request_state.release ();
    return async_result_t<std::vector<message_t> > (std::move (future));
}

template <typename Submit>
bool submit_actor_request_callback (request_callback_t callback_,
                                    Submit submit_)
{
    std::unique_ptr<detail::request_state_t> request_state (
      detail::make_callback_request_state (std::move (callback_)));
    const submit_result_t rc =
      static_cast<submit_result_t> (submit_ (request_state.get ()));
    if (rc != submit_result_t::ok)
        throw submit_error_t (rc, zlink_errno ());
    request_state.release ();
    return true;
}

} // namespace

actor_join_operation_t::~actor_join_operation_t () = default;
actor_join_operation_t::actor_join_operation_t (
  actor_join_operation_t &&) noexcept = default;
actor_join_operation_t &actor_join_operation_t::operator= (
  actor_join_operation_t &&) noexcept = default;

actor_join_operation_t::actor_join_operation_t (
  detail::actor_join_state_t &&state_) :
    _state (std::make_unique<detail::actor_join_state_t> (std::move (state_)))
{
}

detail::actor_join_state_t &actor_join_operation_t::state () noexcept
{
    return (*_state);
}

const detail::actor_join_state_t &
actor_join_operation_t::state () const noexcept
{
    return (*_state);
}

actor_join_submit_operation_t
actor_join_operation_t::message (message_t &part_) &&
{
    state ().parts.push_back (std::move (part_));
    return actor_join_submit_operation_t (std::move (state ()));
}

actor_join_submit_operation_t::~actor_join_submit_operation_t () = default;
actor_join_submit_operation_t::actor_join_submit_operation_t (
  actor_join_submit_operation_t &&) noexcept = default;
actor_join_submit_operation_t &actor_join_submit_operation_t::operator= (
  actor_join_submit_operation_t &&) noexcept = default;

actor_join_submit_operation_t::actor_join_submit_operation_t (
  detail::actor_join_state_t &&state_) :
    _state (std::make_unique<detail::actor_join_state_t> (std::move (state_)))
{
}

detail::actor_join_state_t &actor_join_submit_operation_t::state () noexcept
{
    return (*_state);
}

const detail::actor_join_state_t &
actor_join_submit_operation_t::state () const noexcept
{
    return (*_state);
}

actor_join_submit_operation_t &&
actor_join_submit_operation_t::message (message_t &part_) &&
{
    state ().parts.push_back (std::move (part_));
    return std::move (*this);
}

actor_join_submit_operation_t &&
actor_join_submit_operation_t::timeout (std::chrono::milliseconds timeout_) &&
{
    state ().timeout = timeout_;
    return std::move (*this);
}

actor_join_callback_submit_operation_t
actor_join_submit_operation_t::flags (int flags_) &&
{
    state ().flags = send_flags_t (flags_);
    return actor_join_callback_submit_operation_t (std::move (state ()));
}

async_result_t<actor_join_result_t>
actor_join_submit_operation_t::submit_async () &&
{
    std::unique_ptr<detail::actor_join_result_state_t> request_state (
      detail::make_future_actor_join_state ());
    std::future<actor_join_result_t> future =
      request_state->promise->get_future ();
    const int rc = detail::submit_actor_join (state (), request_state.get ());
    if (rc != ZLINK_SUBMIT_OK) {
        throw submit_error_t (static_cast<submit_result_t> (rc),
                              zlink_errno ());
    }
    request_state.release ();
    return async_result_t<actor_join_result_t> (std::move (future));
}

bool actor_join_submit_operation_t::submit (actor_join_callback_t callback_) &&
{
    actor_join_callback_submit_operation_t ready (std::move (state ()));
    return std::move (ready).submit (std::move (callback_));
}

actor_join_callback_submit_operation_t::
  ~actor_join_callback_submit_operation_t () = default;
actor_join_callback_submit_operation_t::actor_join_callback_submit_operation_t (
  actor_join_callback_submit_operation_t &&) noexcept = default;
actor_join_callback_submit_operation_t &
actor_join_callback_submit_operation_t::operator= (
  actor_join_callback_submit_operation_t &&) noexcept = default;

actor_join_callback_submit_operation_t::actor_join_callback_submit_operation_t (
  detail::actor_join_state_t &&state_) :
    _state (std::make_unique<detail::actor_join_state_t> (std::move (state_)))
{
}

detail::actor_join_state_t &
actor_join_callback_submit_operation_t::state () noexcept
{
    return (*_state);
}

const detail::actor_join_state_t &
actor_join_callback_submit_operation_t::state () const noexcept
{
    return (*_state);
}

actor_join_callback_submit_operation_t &&
actor_join_callback_submit_operation_t::message (message_t &part_) &&
{
    state ().parts.push_back (std::move (part_));
    return std::move (*this);
}

actor_join_callback_submit_operation_t &&
actor_join_callback_submit_operation_t::timeout (
  std::chrono::milliseconds timeout_) &&
{
    state ().timeout = timeout_;
    return std::move (*this);
}

actor_join_callback_submit_operation_t &&
actor_join_callback_submit_operation_t::flags (int flags_) &&
{
    state ().flags = send_flags_t (flags_);
    return std::move (*this);
}

bool actor_join_callback_submit_operation_t::submit (
  actor_join_callback_t callback_) &&
{
    std::unique_ptr<detail::actor_join_result_state_t> request_state (
      detail::make_callback_actor_join_state (std::move (callback_)));
    const int rc = detail::submit_actor_join (state (), request_state.get ());
    if (rc != ZLINK_SUBMIT_OK) {
        if (state ().flags == send_flags_t::dontwait
            && static_cast<submit_result_t> (rc)
                 == submit_result_t::backpressured)
            return false;
        throw submit_error_t (static_cast<submit_result_t> (rc),
                              zlink_errno ());
    }
    request_state.release ();
    return true;
}

actor_join_entry_spot_operation_t::~actor_join_entry_spot_operation_t () =
  default;
actor_join_entry_spot_operation_t::actor_join_entry_spot_operation_t (
  actor_join_entry_spot_operation_t &&) noexcept = default;
actor_join_entry_spot_operation_t &
actor_join_entry_spot_operation_t::operator= (
  actor_join_entry_spot_operation_t &&) noexcept = default;

actor_join_entry_spot_operation_t::actor_join_entry_spot_operation_t (
  detail::actor_payloadless_state_t &&state_) :
    _state (
      std::make_unique<detail::actor_payloadless_state_t> (std::move (state_)))
{
}

detail::actor_payloadless_state_t &
actor_join_entry_spot_operation_t::state () noexcept
{
    return (*_state);
}

const detail::actor_payloadless_state_t &
actor_join_entry_spot_operation_t::state () const noexcept
{
    return (*_state);
}

actor_join_entry_spot_operation_t &&actor_join_entry_spot_operation_t::timeout (
  std::chrono::milliseconds timeout_) &&
{
    state ().timeout = timeout_;
    return std::move (*this);
}

async_result_t<actor_join_entry_spot_result_t>
actor_join_entry_spot_operation_t::submit_async () &&
{
    std::unique_ptr<detail::actor_join_entry_spot_result_state_t>
      request_state (detail::make_future_actor_join_entry_spot_state ());
    std::future<actor_join_entry_spot_result_t> future =
      request_state->promise->get_future ();
    const submit_result_t rc =
      static_cast<submit_result_t> (zlink_spot_node_actor_join_entry_spot (
        state ().node, zlink::detail::actor_ref_native (state ().actor),
        zlink::detail::routing_id_native (state ().aux_rid),
        &detail::actor_join_entry_spot_result_trampoline, request_state.get (),
        zlink::detail::native_timeout_ms (state ().timeout)));
    if (rc != submit_result_t::ok)
        throw submit_error_t (rc, zlink_errno ());
    request_state.release ();
    return async_result_t<actor_join_entry_spot_result_t> (std::move (future));
}

bool actor_join_entry_spot_operation_t::submit (
  actor_join_entry_spot_callback_t callback_) &&
{
    std::unique_ptr<detail::actor_join_entry_spot_result_state_t>
      request_state (detail::make_callback_actor_join_entry_spot_state (
        std::move (callback_)));
    const submit_result_t rc =
      static_cast<submit_result_t> (zlink_spot_node_actor_join_entry_spot (
        state ().node, zlink::detail::actor_ref_native (state ().actor),
        zlink::detail::routing_id_native (state ().aux_rid),
        &detail::actor_join_entry_spot_result_trampoline, request_state.get (),
        zlink::detail::native_timeout_ms (state ().timeout)));
    if (rc != submit_result_t::ok)
        throw submit_error_t (rc, zlink_errno ());
    request_state.release ();
    return true;
}

actor_join_reply_operation_t::~actor_join_reply_operation_t () = default;
actor_join_reply_operation_t::actor_join_reply_operation_t (
  actor_join_reply_operation_t &&) noexcept = default;
actor_join_reply_operation_t &actor_join_reply_operation_t::operator= (
  actor_join_reply_operation_t &&) noexcept = default;

actor_join_reply_operation_t::actor_join_reply_operation_t (
  detail::actor_join_reply_state_t &&state_) :
    _state (
      std::make_unique<detail::actor_join_reply_state_t> (std::move (state_)))
{
}

detail::actor_join_reply_state_t &
actor_join_reply_operation_t::state () noexcept
{
    return (*_state);
}

const detail::actor_join_reply_state_t &
actor_join_reply_operation_t::state () const noexcept
{
    return (*_state);
}

actor_join_reply_operation_t &&
actor_join_reply_operation_t::message (message_t &part_) &&
{
    state ().parts.push_back (std::move (part_));
    return std::move (*this);
}

void actor_join_reply_operation_t::submit () &&
{
    if (!state ().spot)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    zlink_actor_join_info_t native_info =
      zlink::detail::actor_model_access_t::to_native (state ().info);
    if (state ().parts.empty ()) {
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_spot_actor_join_reply (state ().spot, &native_info,
                                       state ().join_result_code, nullptr, 0u));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
        return;
    }

    std::vector<zlink_msg_t> native;
    if (detail::move_parts_to_native (state ().parts, native) != 0)
        throw last_error ();
    const submit_result_t rc =
      static_cast<submit_result_t> (zlink_spot_actor_join_reply (
        state ().spot, &native_info, state ().join_result_code, native.data (),
        native.size ()));
    if (rc != submit_result_t::ok) {
        detail::restore_parts_from_native (state ().parts, native);
        throw submit_error_t (rc, zlink_errno ());
    }
}

actor_leave_operation_t::~actor_leave_operation_t () = default;
actor_leave_operation_t::actor_leave_operation_t (
  actor_leave_operation_t &&) noexcept = default;
actor_leave_operation_t &actor_leave_operation_t::operator= (
  actor_leave_operation_t &&) noexcept = default;

actor_leave_operation_t::actor_leave_operation_t (
  detail::actor_payloadless_state_t &&state_) :
    _state (
      std::make_unique<detail::actor_payloadless_state_t> (std::move (state_)))
{
}

detail::actor_payloadless_state_t &actor_leave_operation_t::state () noexcept
{
    return (*_state);
}

const detail::actor_payloadless_state_t &
actor_leave_operation_t::state () const noexcept
{
    return (*_state);
}

actor_leave_operation_t &&
actor_leave_operation_t::timeout (std::chrono::milliseconds timeout_) &&
{
    state ().timeout = timeout_;
    return std::move (*this);
}

async_result_t<std::vector<message_t> >
actor_leave_operation_t::submit_async () &&
{
    return submit_actor_request_async (
      [&] (detail::request_state_t *request_state) {
          return zlink_spot_node_actor_leave_spot (
            state ().node, zlink::detail::actor_ref_native (state ().actor),
            zlink::detail::routing_id_native (state ().aux_rid),
            &detail::request_callback_trampoline, request_state,
            zlink::detail::native_timeout_ms (state ().timeout));
      });
}

bool actor_leave_operation_t::submit (request_callback_t callback_) &&
{
    return submit_actor_request_callback (
      std::move (callback_), [&] (detail::request_state_t *request_state) {
          return zlink_spot_node_actor_leave_spot (
            state ().node, zlink::detail::actor_ref_native (state ().actor),
            zlink::detail::routing_id_native (state ().aux_rid),
            &detail::request_callback_trampoline, request_state,
            zlink::detail::native_timeout_ms (state ().timeout));
      });
}

actor_destroy_operation_t::~actor_destroy_operation_t () = default;
actor_destroy_operation_t::actor_destroy_operation_t (
  actor_destroy_operation_t &&) noexcept = default;
actor_destroy_operation_t &actor_destroy_operation_t::operator= (
  actor_destroy_operation_t &&) noexcept = default;

actor_destroy_operation_t::actor_destroy_operation_t (
  detail::actor_payloadless_state_t &&state_) :
    _state (
      std::make_unique<detail::actor_payloadless_state_t> (std::move (state_)))
{
}

detail::actor_payloadless_state_t &actor_destroy_operation_t::state () noexcept
{
    return (*_state);
}

const detail::actor_payloadless_state_t &
actor_destroy_operation_t::state () const noexcept
{
    return (*_state);
}

actor_destroy_operation_t &&
actor_destroy_operation_t::timeout (std::chrono::milliseconds timeout_) &&
{
    state ().timeout = timeout_;
    return std::move (*this);
}

async_result_t<std::vector<message_t> >
actor_destroy_operation_t::submit_async () &&
{
    return submit_actor_request_async (
      [&] (detail::request_state_t *request_state) {
          return zlink_spot_node_actor_destroy (
            state ().node, zlink::detail::actor_ref_native (state ().actor),
            &detail::request_callback_trampoline, request_state,
            zlink::detail::native_timeout_ms (state ().timeout));
      });
}

bool actor_destroy_operation_t::submit (request_callback_t callback_) &&
{
    return submit_actor_request_callback (
      std::move (callback_), [&] (detail::request_state_t *request_state) {
          return zlink_spot_node_actor_destroy (
            state ().node, zlink::detail::actor_ref_native (state ().actor),
            &detail::request_callback_trampoline, request_state,
            zlink::detail::native_timeout_ms (state ().timeout));
      });
}

actor_lookup_operation_t::~actor_lookup_operation_t () = default;
actor_lookup_operation_t::actor_lookup_operation_t (
  actor_lookup_operation_t &&) noexcept = default;
actor_lookup_operation_t &actor_lookup_operation_t::operator= (
  actor_lookup_operation_t &&) noexcept = default;

actor_lookup_operation_t::actor_lookup_operation_t (
  detail::actor_payloadless_state_t &&state_) :
    _state (
      std::make_unique<detail::actor_payloadless_state_t> (std::move (state_)))
{
}

detail::actor_payloadless_state_t &actor_lookup_operation_t::state () noexcept
{
    return (*_state);
}

const detail::actor_payloadless_state_t &
actor_lookup_operation_t::state () const noexcept
{
    return (*_state);
}

actor_lookup_operation_t &&
actor_lookup_operation_t::timeout (std::chrono::milliseconds timeout_) &&
{
    state ().timeout = timeout_;
    return std::move (*this);
}

async_result_t<actor_lookup_result_t>
actor_lookup_operation_t::submit_async () &&
{
    std::unique_ptr<detail::actor_lookup_result_state_t> request_state (
      detail::make_future_actor_lookup_state ());
    std::future<actor_lookup_result_t> future =
      request_state->promise->get_future ();
    const submit_result_t rc =
      static_cast<submit_result_t> (zlink_remote_actor_get_ref (
        state ().node, zlink::detail::routing_id_native (state ().aux_rid),
        state ().actor_id.c_str (), &detail::actor_lookup_result_trampoline,
        request_state.get (),
        zlink::detail::native_timeout_ms (state ().timeout)));
    if (rc != submit_result_t::ok)
        throw submit_error_t (rc, zlink_errno ());
    request_state.release ();
    return async_result_t<actor_lookup_result_t> (std::move (future));
}

bool actor_lookup_operation_t::submit (actor_lookup_callback_t callback_) &&
{
    std::unique_ptr<detail::actor_lookup_result_state_t> request_state (
      detail::make_callback_actor_lookup_state (std::move (callback_)));
    const submit_result_t rc =
      static_cast<submit_result_t> (zlink_remote_actor_get_ref (
        state ().node, zlink::detail::routing_id_native (state ().aux_rid),
        state ().actor_id.c_str (), &detail::actor_lookup_result_trampoline,
        request_state.get (),
        zlink::detail::native_timeout_ms (state ().timeout)));
    if (rc != submit_result_t::ok)
        throw submit_error_t (rc, zlink_errno ());
    request_state.release ();
    return true;
}

actor_bind_operation_t::~actor_bind_operation_t () = default;
actor_bind_operation_t::actor_bind_operation_t (
  actor_bind_operation_t &&) noexcept = default;
actor_bind_operation_t &actor_bind_operation_t::operator= (
  actor_bind_operation_t &&) noexcept = default;

actor_bind_operation_t::actor_bind_operation_t (
  detail::actor_bind_state_t &&state_) :
    _state (std::make_unique<detail::actor_bind_state_t> (std::move (state_)))
{
}

detail::actor_bind_state_t &actor_bind_operation_t::state () noexcept
{
    return (*_state);
}

const detail::actor_bind_state_t &
actor_bind_operation_t::state () const noexcept
{
    return (*_state);
}

actor_bind_operation_t &&
actor_bind_operation_t::timeout (std::chrono::milliseconds timeout_) &&
{
    state ().timeout = timeout_;
    return std::move (*this);
}

async_result_t<std::vector<message_t> >
actor_bind_operation_t::submit_async () &&
{
    return submit_actor_request_async (
      [&] (detail::request_state_t *request_state) {
          return zlink_stream_bind_actor (
            state ().stream,
            zlink::detail::routing_id_native (state ().session_rid),
            zlink::detail::actor_ref_native (state ().actor),
            &detail::request_callback_trampoline, request_state,
            zlink::detail::native_timeout_ms (state ().timeout));
      });
}

bool actor_bind_operation_t::submit (request_callback_t callback_) &&
{
    return submit_actor_request_callback (
      std::move (callback_), [&] (detail::request_state_t *request_state) {
          return zlink_stream_bind_actor (
            state ().stream,
            zlink::detail::routing_id_native (state ().session_rid),
            zlink::detail::actor_ref_native (state ().actor),
            &detail::request_callback_trampoline, request_state,
            zlink::detail::native_timeout_ms (state ().timeout));
      });
}

actor_unbind_operation_t::~actor_unbind_operation_t () = default;
actor_unbind_operation_t::actor_unbind_operation_t (
  actor_unbind_operation_t &&) noexcept = default;
actor_unbind_operation_t &actor_unbind_operation_t::operator= (
  actor_unbind_operation_t &&) noexcept = default;

actor_unbind_operation_t::actor_unbind_operation_t (
  detail::actor_bind_state_t &&state_) :
    _state (std::make_unique<detail::actor_bind_state_t> (std::move (state_)))
{
}

detail::actor_bind_state_t &actor_unbind_operation_t::state () noexcept
{
    return (*_state);
}

const detail::actor_bind_state_t &
actor_unbind_operation_t::state () const noexcept
{
    return (*_state);
}

actor_unbind_operation_t &&
actor_unbind_operation_t::timeout (std::chrono::milliseconds timeout_) &&
{
    state ().timeout = timeout_;
    return std::move (*this);
}

async_result_t<std::vector<message_t> >
actor_unbind_operation_t::submit_async () &&
{
    return submit_actor_request_async (
      [&] (detail::request_state_t *request_state) {
          return zlink_stream_unbind_actor (
            state ().stream,
            zlink::detail::routing_id_native (state ().session_rid),
            state ().actor_id.c_str (), &detail::request_callback_trampoline,
            request_state, zlink::detail::native_timeout_ms (state ().timeout));
      });
}

bool actor_unbind_operation_t::submit (request_callback_t callback_) &&
{
    return submit_actor_request_callback (
      std::move (callback_), [&] (detail::request_state_t *request_state) {
          return zlink_stream_unbind_actor (
            state ().stream,
            zlink::detail::routing_id_native (state ().session_rid),
            state ().actor_id.c_str (), &detail::request_callback_trampoline,
            request_state, zlink::detail::native_timeout_ms (state ().timeout));
      });
}

actor_bind_operation_t
detail_make_actor_bind_operation (detail::actor_bind_state_t &&state_)
{
    return actor_bind_operation_t (std::move (state_));
}

actor_unbind_operation_t
detail_make_actor_unbind_operation (detail::actor_bind_state_t &&state_)
{
    return actor_unbind_operation_t (std::move (state_));
}

actor_destroy_operation_t spot_node_t::destroy_actor (const actor_ref_t &actor_)
{
    detail::actor_payloadless_state_t state;
    state.node = zlink::detail::native_handle (*this);
    state.actor = actor_;
    return actor_destroy_operation_t (std::move (state));
}

actor_join_operation_t
spot_node_t::join_actor (const actor_ref_t &actor_,
                         const routing_id_t &dest_node_rid_,
                         const routing_id_t &dest_spot_rid_)
{
    detail::actor_join_state_t state;
    state.node = zlink::detail::native_handle (*this);
    state.actor = actor_;
    state.dest_node_rid = dest_node_rid_;
    state.dest_spot_rid = dest_spot_rid_;
    return actor_join_operation_t (std::move (state));
}

actor_join_entry_spot_operation_t
spot_node_t::join_actor_entry_spot (const actor_ref_t &actor_,
                                    const routing_id_t &dest_node_rid_)
{
    detail::actor_payloadless_state_t state;
    state.node = zlink::detail::native_handle (*this);
    state.actor = actor_;
    state.aux_rid = dest_node_rid_;
    state.has_aux_rid = true;
    return actor_join_entry_spot_operation_t (std::move (state));
}

actor_leave_operation_t
spot_node_t::leave_actor (const actor_ref_t &actor_,
                          const routing_id_t &current_spot_rid_)
{
    detail::actor_payloadless_state_t state;
    state.node = zlink::detail::native_handle (*this);
    state.actor = actor_;
    state.aux_rid = current_spot_rid_;
    state.has_aux_rid = true;
    return actor_leave_operation_t (std::move (state));
}

actor_lookup_operation_t
spot_node_t::remote_actor_get_ref (const routing_id_t &target_node_rid_,
                                   const std::string &actor_id_)
{
    zlink::detail::validate_bounded_c_string (
      actor_id_, ZLINK_ACTOR_ID_MAX - 1u, "actor_id");
    detail::actor_payloadless_state_t state;
    state.node = zlink::detail::native_handle (*this);
    state.aux_rid = target_node_rid_;
    state.has_aux_rid = true;
    state.actor_id = actor_id_;
    return actor_lookup_operation_t (std::move (state));
}

send_operation_t spot_node_t::send_bound_session_msg (const actor_ref_t &actor_)
{
    detail::spot_operation_state_t state;
    state.kind = detail::spot_operation_kind_t::bound_session_send;
    state.node = this;
    state.actor = actor_;
    return send_operation_t (std::move (state));
}

} // namespace service
} // namespace zlink
