/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Service/spot.hpp>
#include <Runtime/Core/operation_state_owner.hpp>
#include <Runtime/Service/detail.hpp>
#include <Runtime/Service/spot_operation_submit.hpp>

namespace zlink
{
namespace service
{

namespace
{

bool is_raw_request_kind (detail::spot_operation_kind_t kind_) noexcept
{
    return kind_ == detail::spot_operation_kind_t::raw_request
           || kind_ == detail::spot_operation_kind_t::raw_routed_request
           || kind_ == detail::spot_operation_kind_t::raw_router_request_spot;
}

void ensure_raw_request_state (const detail::spot_operation_state_t &state_)
{
    if (!state_.raw_socket)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    if (state_.kind != detail::spot_operation_kind_t::raw_request
        && !state_.first_rid)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    if (state_.kind == detail::spot_operation_kind_t::raw_router_request_spot
        && !state_.second_rid)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
}

zlink_submit_result_t submit_raw_request_part (
  detail::spot_operation_state_t &state_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t part_flag_,
  bool is_final_,
  zlink_send_flags_t flags_,
  detail::request_state_t *request_state_)
{
    const uint32_t timeout =
      is_final_ ? static_cast<uint32_t> (state_.timeout.count ()) : 0u;
    zlink_request_callback_t callback =
      is_final_ ? &detail::request_callback_trampoline : NULL;
    void *userdata = is_final_ ? request_state_ : NULL;

    switch (state_.kind) {
        case detail::spot_operation_kind_t::raw_request:
            return zlink_dealer_request_part (state_.raw_socket, part_out_,
                                              flags_, part_flag_, timeout,
                                              callback, userdata);
        case detail::spot_operation_kind_t::raw_routed_request:
            return zlink_router_request_part (
              state_.raw_socket,
              zlink::detail::routing_id_native (*state_.first_rid), part_out_,
              flags_, part_flag_, timeout, callback, userdata);
        case detail::spot_operation_kind_t::raw_router_request_spot:
            return zlink_router_request_spot_part (
              state_.raw_socket,
              zlink::detail::routing_id_native (*state_.first_rid),
              zlink::detail::routing_id_native (*state_.second_rid), part_out_,
              callback, userdata, flags_, part_flag_, timeout);
        default:
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
}

async_result_t<std::vector<message_t>>
submit_raw_request_async (detail::spot_operation_state_t &state_)
{
    ensure_raw_request_state (state_);

    detail::request_state_t *request_state =
      detail::make_future_request_state ();
    std::future<std::vector<message_t>> future =
      request_state->promise->get_future ();
    std::vector<zlink_msg_t> native;
    if (detail::move_parts_to_native (state_.parts, native) != 0) {
        delete request_state;
        throw last_error ();
    }

    size_t failed_index = 0;
    const int rc = detail::submit_native_parts (
      native, failed_index,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
           bool is_final_) {
          return submit_raw_request_part (state_, part_out_, part_flag_,
                                          is_final_, ZLINK_SEND_FLAGS_NONE,
                                          request_state);
      });
    if (rc != 0) {
        detail::close_native_parts (native, failed_index);
        delete request_state;
        throw last_error ();
    }

    return async_result_t<std::vector<message_t>> (
      std::move (future),
      zlink::detail::make_socket_request_progress (state_.raw_socket));
}

bool submit_raw_request_callback (detail::spot_operation_state_t &state_,
                                  request_callback_t callback_)
{
    ensure_raw_request_state (state_);

    detail::request_state_t *request_state =
      detail::make_callback_request_state (std::move (callback_));
    std::vector<zlink_msg_t> native;
    if (detail::move_parts_to_native (state_.parts, native) != 0) {
        delete request_state;
        throw last_error ();
    }

    size_t failed_index = 0;
    const int rc = detail::submit_native_parts (
      native, failed_index,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
           bool is_final_) {
          return submit_raw_request_part (
            state_, part_out_, part_flag_, is_final_,
            static_cast<zlink_send_flags_t> (static_cast<int> (state_.flags)),
            request_state);
      });
    if (rc != 0) {
        detail::close_native_parts (native, failed_index);
        delete request_state;
        const submit_error_t err (static_cast<submit_result_t> (rc),
                                  zlink_errno ());
        if (state_.flags == send_flags_t::dontwait
            && err.result () == submit_result_t::backpressured)
            return false;
        throw err;
    }
    return true;
}

void submit_raw_reply (detail::spot_operation_state_t &state_)
{
    if (!state_.raw_socket || !state_.first_rid)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    if (state_.kind == detail::spot_operation_kind_t::raw_router_reply_spot
        && !state_.second_rid)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    zlink::detail::throw_if_reply_flags_unsupported (state_.flags);
    std::vector<zlink_msg_t> native;
    if (detail::move_parts_to_native (state_.parts, native) != 0)
        throw last_error ();

    size_t failed_index = 0;
    const int rc = detail::submit_native_parts (
      native, failed_index,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
          if (state_.kind == detail::spot_operation_kind_t::raw_reply) {
              return zlink_router_reply_part (
                state_.raw_socket,
                zlink::detail::routing_id_native (*state_.first_rid),
                state_.request_seq, part_out_, part_flag_);
          }
          return zlink_router_reply_spot_part (
            state_.raw_socket,
            zlink::detail::routing_id_native (*state_.first_rid),
            zlink::detail::routing_id_native (*state_.second_rid),
            state_.request_seq, part_out_, part_flag_);
      });
    if (rc != 0) {
        detail::restore_parts_from_native (state_.parts, native, failed_index);
        throw last_error ();
    }
}

} // namespace

send_submit_operation_t::~send_submit_operation_t () = default;
send_submit_operation_t::send_submit_operation_t (
  send_submit_operation_t &&) noexcept = default;
send_submit_operation_t &send_submit_operation_t::operator= (
  send_submit_operation_t &&) noexcept = default;

send_submit_operation_t::send_submit_operation_t (
  detail::spot_operation_state_t &&state_) :
    _state (zlink::detail::make_operation_state (std::move (state_)))
{
}

detail::spot_operation_state_t &send_submit_operation_t::state () noexcept
{
    return zlink::detail::operation_state (_state);
}

const detail::spot_operation_state_t &
send_submit_operation_t::state () const noexcept
{
    return zlink::detail::operation_state (_state);
}

send_submit_operation_t &&send_submit_operation_t::message (message_t &part_) &&
{
    detail::append_send_part (state (), part_);
    return std::move (*this);
}

send_submit_operation_t &&
send_submit_operation_t::message (message_t &&part_) &&
{
    state ().single_part.emplace (std::move (part_));
    state ().single_part_source = NULL;
    state ().discard_single_part_on_backpressure = true;
    return std::move (*this);
}

send_submit_operation_t &&send_submit_operation_t::flags (int flags_) &&
{
    state ().flags = send_flags_t (flags_);
    return std::move (*this);
}

send_operation_t::~send_operation_t () = default;
send_operation_t::send_operation_t (send_operation_t &&) noexcept = default;
send_operation_t &
send_operation_t::operator= (send_operation_t &&) noexcept = default;

send_operation_t::send_operation_t (detail::spot_operation_state_t &&state_) :
    _state (zlink::detail::make_operation_state (std::move (state_)))
{
}

detail::spot_operation_state_t &send_operation_t::state () noexcept
{
    return zlink::detail::operation_state (_state);
}

const detail::spot_operation_state_t &send_operation_t::state () const noexcept
{
    return zlink::detail::operation_state (_state);
}

send_submit_operation_t send_operation_t::message (message_t &part_) &&
{
    state ().single_part_source = &part_;
    if (!detail::can_borrow_single_send_part (state ().kind))
        state ().single_part.emplace (std::move (part_));
    return send_submit_operation_t (std::move (state ()));
}

send_submit_operation_t send_operation_t::message (message_t &&part_) &&
{
    state ().single_part.emplace (std::move (part_));
    state ().single_part_source = NULL;
    state ().discard_single_part_on_backpressure = true;
    return send_submit_operation_t (std::move (state ()));
}

request_submit_operation_t::~request_submit_operation_t () = default;
request_submit_operation_t::request_submit_operation_t (
  request_submit_operation_t &&) noexcept = default;
request_submit_operation_t &request_submit_operation_t::operator= (
  request_submit_operation_t &&) noexcept = default;

request_submit_operation_t::request_submit_operation_t (
  detail::spot_operation_state_t &&state_) :
    _state (zlink::detail::make_operation_state (std::move (state_)))
{
}

detail::spot_operation_state_t &request_submit_operation_t::state () noexcept
{
    return zlink::detail::operation_state (_state);
}

const detail::spot_operation_state_t &
request_submit_operation_t::state () const noexcept
{
    return zlink::detail::operation_state (_state);
}

request_submit_operation_t &&
request_submit_operation_t::message (message_t &part_) &&
{
    state ().parts.push_back (std::move (part_));
    return std::move (*this);
}

request_submit_operation_t &&
request_submit_operation_t::timeout (std::chrono::milliseconds timeout_) &&
{
    state ().timeout = timeout_;
    return std::move (*this);
}

request_operation_t::~request_operation_t () = default;
request_operation_t::request_operation_t (request_operation_t &&) noexcept =
  default;
request_operation_t &
request_operation_t::operator= (request_operation_t &&) noexcept = default;

request_operation_t::request_operation_t (
  detail::spot_operation_state_t &&state_) :
    _state (zlink::detail::make_operation_state (std::move (state_)))
{
}

detail::spot_operation_state_t &request_operation_t::state () noexcept
{
    return zlink::detail::operation_state (_state);
}

const detail::spot_operation_state_t &
request_operation_t::state () const noexcept
{
    return zlink::detail::operation_state (_state);
}

request_submit_operation_t request_operation_t::message (message_t &part_) &&
{
    state ().parts.push_back (std::move (part_));
    return request_submit_operation_t (std::move (state ()));
}

request_callback_submit_operation_t::~request_callback_submit_operation_t () =
  default;
request_callback_submit_operation_t::request_callback_submit_operation_t (
  request_callback_submit_operation_t &&) noexcept = default;
request_callback_submit_operation_t &
request_callback_submit_operation_t::operator= (
  request_callback_submit_operation_t &&) noexcept = default;

request_callback_submit_operation_t::request_callback_submit_operation_t (
  detail::spot_operation_state_t &&state_) :
    _state (zlink::detail::make_operation_state (std::move (state_)))
{
}

detail::spot_operation_state_t &
request_callback_submit_operation_t::state () noexcept
{
    return zlink::detail::operation_state (_state);
}

const detail::spot_operation_state_t &
request_callback_submit_operation_t::state () const noexcept
{
    return zlink::detail::operation_state (_state);
}

request_callback_submit_operation_t &&
request_callback_submit_operation_t::message (message_t &part_) &&
{
    state ().parts.push_back (std::move (part_));
    return std::move (*this);
}

request_callback_submit_operation_t &&
request_callback_submit_operation_t::timeout (
  std::chrono::milliseconds timeout_) &&
{
    state ().timeout = timeout_;
    return std::move (*this);
}

request_callback_submit_operation_t &&
request_callback_submit_operation_t::flags (int flags_) &&
{
    state ().flags = send_flags_t (flags_);
    return std::move (*this);
}

reply_submit_operation_t::~reply_submit_operation_t () = default;
reply_submit_operation_t::reply_submit_operation_t (
  reply_submit_operation_t &&) noexcept = default;
reply_submit_operation_t &reply_submit_operation_t::operator= (
  reply_submit_operation_t &&) noexcept = default;

reply_submit_operation_t::reply_submit_operation_t (
  detail::spot_operation_state_t &&state_) :
    _state (zlink::detail::make_operation_state (std::move (state_)))
{
}

detail::spot_operation_state_t &reply_submit_operation_t::state () noexcept
{
    return zlink::detail::operation_state (_state);
}

const detail::spot_operation_state_t &
reply_submit_operation_t::state () const noexcept
{
    return zlink::detail::operation_state (_state);
}

reply_submit_operation_t &&
reply_submit_operation_t::message (message_t &part_) &&
{
    state ().parts.push_back (std::move (part_));
    return std::move (*this);
}

reply_submit_operation_t &&reply_submit_operation_t::flags (int flags_) &&
{
    state ().flags = send_flags_t (flags_);
    return std::move (*this);
}

reply_operation_t::~reply_operation_t () = default;
reply_operation_t::reply_operation_t (reply_operation_t &&) noexcept = default;
reply_operation_t &
reply_operation_t::operator= (reply_operation_t &&) noexcept = default;

reply_operation_t::reply_operation_t (detail::spot_operation_state_t &&state_) :
    _state (zlink::detail::make_operation_state (std::move (state_)))
{
}

detail::spot_operation_state_t &reply_operation_t::state () noexcept
{
    return zlink::detail::operation_state (_state);
}

const detail::spot_operation_state_t &reply_operation_t::state () const noexcept
{
    return zlink::detail::operation_state (_state);
}

reply_submit_operation_t reply_operation_t::message (message_t &part_) &&
{
    state ().parts.push_back (std::move (part_));
    return reply_submit_operation_t (std::move (state ()));
}

bool send_submit_operation_t::submit () &&
{
    auto &state = this->state ();
    if (!detail::has_send_parts (state))
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (state.kind) {
        case detail::spot_operation_kind_t::raw_send:
        case detail::spot_operation_kind_t::raw_routed_send:
        case detail::spot_operation_kind_t::raw_publish:
        case detail::spot_operation_kind_t::raw_router_send_spot:
            return detail::submit_raw_send_state (state);
        case detail::spot_operation_kind_t::received_send: {
            if (!state.received || !state.received->has_send_fn ())
                throw submit_error_t (submit_result_t::invalid_argument,
                                      EINVAL);
            if (detail::send_part_count (state) == 1u
                && state.received->_runtime
                && state.received->_runtime->_send_context_handle
                && state.received->_runtime->_send_context_kind
                     != received_t::send_context_kind_t::none
                && state.received->_routing_id.has_value ()) {
                message_t &part = detail::send_single_part (state);
                if (!part.valid ()) {
                    detail::restore_single_send_part_to_source (state);
                    throw submit_error_t (submit_result_t::invalid_argument,
                                          EINVAL);
                }

                zlink_submit_result_t direct_rc = ZLINK_SUBMIT_INVALID_ARGUMENT;
                void *send_context_handle = reinterpret_cast<void *> (
                  state.received->_runtime->_send_context_handle);
                if (state.received->_runtime->_send_context_kind
                    == received_t::send_context_kind_t::socket_rid) {
                    direct_rc =
                      zlink_send_part_rid (send_context_handle,
                                           zlink::detail::routing_id_native (
                                             *state.received->_routing_id),
                                           zlink::detail::native_handle (part),
                                           static_cast<zlink_send_flags_t> (
                                             static_cast<int> (state.flags)),
                                           ZLINK_PART_FINAL);
                } else if (state.received->_spot_rid.has_value ()) {
                    direct_rc = zlink_router_send_spot_part (
                      send_context_handle,
                      zlink::detail::routing_id_native (
                        *state.received->_routing_id),
                      zlink::detail::routing_id_native (
                        *state.received->_spot_rid),
                      zlink::detail::native_handle (part),
                      static_cast<zlink_send_flags_t> (
                        static_cast<int> (state.flags)),
                      ZLINK_PART_FINAL);
                }

                const submit_result_t result =
                  static_cast<submit_result_t> (direct_rc);
                if (result == submit_result_t::ok) {
                    zlink::detail::mark_sent (part);
                    return true;
                }
                detail::restore_single_send_part_to_source (state);
                if (state.flags == send_flags_t::dontwait
                    && result == submit_result_t::backpressured)
                    return false;
                throw submit_error_t (result, zlink_errno ());
            }
            std::vector<message_t> parts = detail::take_send_parts (state);
            const bool sent =
              state.received->invoke_send_fn (parts, state.flags);
            if (!sent)
                detail::restore_single_send_part_to_source (state, parts);
            return sent;
        }
        case detail::spot_operation_kind_t::bound_session_send:
            return detail::submit_bound_session_send_state (state);
        case detail::spot_operation_kind_t::stream_bound_actor_send:
            return detail::submit_stream_bound_actor_send_state (state);
        default:
            break;
    }

    if (!state.spot)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (state.kind) {
        case detail::spot_operation_kind_t::publish:
            if (detail::send_part_count (state) == 1u
                && state.flags == send_flags_t::dontwait
                && state.discard_single_part_on_backpressure
                && state.single_part.has_value ()
                && !state.single_part_source) {
                return state.spot->publish_discard_on_backpressure (
                  state.topic, *state.single_part);
            }
            return detail::send_part_count (state) == 1u
                     ? state.spot->publish (state.topic,
                                            detail::send_single_part (state),
                                            state.flags)
                     : state.spot->publish (state.topic, state.parts,
                                            state.flags);
        case detail::spot_operation_kind_t::send_channel:
            return detail::send_part_count (state) == 1u
                     ? state.spot->send_channel (
                         state.channel_name, detail::send_single_part (state),
                         state.flags)
                     : state.spot->send_channel (state.channel_name,
                                                 state.parts, state.flags);
        case detail::spot_operation_kind_t::send_to_spot:
            if (!state.first_rid || !state.second_rid)
                throw submit_error_t (submit_result_t::invalid_argument,
                                      EINVAL);
            return detail::send_part_count (state) == 1u
                     ? state.spot->send_to_spot (
                         *state.first_rid, *state.second_rid,
                         std::move (detail::send_single_part (state)),
                         state.flags)
                     : state.spot->send_to_spot (*state.first_rid,
                                                 *state.second_rid, state.parts,
                                                 state.flags);
        default:
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

request_callback_submit_operation_t
request_submit_operation_t::flags (int flags_) &&
{
    auto &state = this->state ();
    state.flags = send_flags_t (flags_);
    return request_callback_submit_operation_t (std::move (state));
}

async_result_t<std::vector<message_t> >
request_submit_operation_t::submit_async () &&
{
    auto &state = this->state ();
    if (state.parts.empty ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    if (is_raw_request_kind (state.kind))
        return submit_raw_request_async (state);

    if (!state.spot)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (state.kind) {
        case detail::spot_operation_kind_t::request_channel:
            return state.spot->request_channel (state.channel_name, state.parts,
                                                state.timeout);
        case detail::spot_operation_kind_t::request_to_spot:
            if (!state.first_rid || !state.second_rid)
                throw submit_error_t (submit_result_t::invalid_argument,
                                      EINVAL);
            return state.spot->request_to_spot (
              *state.first_rid, *state.second_rid,
              std::move (state.parts.front ()), state.timeout);
        case detail::spot_operation_kind_t::request_to_router:
            if (!state.first_rid)
                throw submit_error_t (submit_result_t::invalid_argument,
                                      EINVAL);
            return state.spot->request_to_router (
              *state.first_rid, std::move (state.parts.front ()),
              state.timeout);
        default:
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

bool request_submit_operation_t::submit (request_callback_t callback_) &&
{
    auto &state = this->state ();
    request_callback_submit_operation_t ready (std::move (state));
    return std::move (ready).submit (std::move (callback_));
}

bool request_callback_submit_operation_t::submit (
  request_callback_t callback_) &&
{
    auto &state = this->state ();
    if (state.parts.empty ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    if (is_raw_request_kind (state.kind))
        return submit_raw_request_callback (state, std::move (callback_));

    if (!state.spot)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (state.kind) {
        case detail::spot_operation_kind_t::request_channel:
            return state.spot->request_channel (state.channel_name, state.parts,
                                                std::move (callback_),
                                                state.flags, state.timeout);
        case detail::spot_operation_kind_t::request_to_spot:
            if (!state.first_rid || !state.second_rid)
                throw submit_error_t (submit_result_t::invalid_argument,
                                      EINVAL);
            return state.spot->request_to_spot (
              *state.first_rid, *state.second_rid,
              std::move (state.parts.front ()), std::move (callback_),
              state.flags, state.timeout);
        case detail::spot_operation_kind_t::request_to_router:
            if (!state.first_rid)
                throw submit_error_t (submit_result_t::invalid_argument,
                                      EINVAL);
            return state.spot->request_to_router (
              *state.first_rid, std::move (state.parts.front ()),
              std::move (callback_), state.flags, state.timeout);
        default:
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

void reply_submit_operation_t::submit () &&
{
    auto &state = this->state ();
    if (state.parts.empty ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    if (state.kind == detail::spot_operation_kind_t::received_reply) {
        if (!state.received || !state.received->has_reply_fn ())
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        state.received->invoke_reply_fn (state.parts, state.flags);
        return;
    }

    if (state.kind == detail::spot_operation_kind_t::raw_reply
        || state.kind == detail::spot_operation_kind_t::raw_router_reply_spot) {
        submit_raw_reply (state);
        return;
    }

    if (!state.spot)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (state.kind) {
        case detail::spot_operation_kind_t::reply_to_spot:
            if (!state.first_rid || !state.second_rid)
                throw submit_error_t (submit_result_t::invalid_argument,
                                      EINVAL);
            state.spot->reply_to_spot (
              *state.first_rid, *state.second_rid, state.request_seq,
              std::move (state.parts.front ()), state.flags);
            return;
        case detail::spot_operation_kind_t::reply_to_router:
            if (!state.first_rid)
                throw submit_error_t (submit_result_t::invalid_argument,
                                      EINVAL);
            state.spot->reply_to_router (*state.first_rid, state.request_seq,
                                         std::move (state.parts.front ()),
                                         state.flags);
            return;
        default:
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

} // namespace service
} // namespace zlink
