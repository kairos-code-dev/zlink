/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Service/spot.hpp>
#include <Runtime/Service/detail.hpp>
#include <Runtime/Service/spot_submit.hpp>

namespace zlink
{
namespace service
{

send_submit_operation_t::~send_submit_operation_t () = default;
send_submit_operation_t::send_submit_operation_t (send_submit_operation_t &&) noexcept = default;
send_submit_operation_t &
send_submit_operation_t::operator= (send_submit_operation_t &&) noexcept = default;

send_submit_operation_t::send_submit_operation_t (detail::spot_op_state_t &&state_)
    : _state (new detail::spot_op_state_t (std::move (state_)))
{
}

detail::spot_op_state_t &send_submit_operation_t::state () noexcept
{
    return *_state;
}

const detail::spot_op_state_t &send_submit_operation_t::state () const noexcept
{
    return *_state;
}

send_submit_operation_t &&send_submit_operation_t::message (message_t &part_) &&
{
    detail::append_send_part (state (), part_);
    return std::move (*this);
}

send_submit_operation_t &&send_submit_operation_t::message (message_t &&part_) &&
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
send_operation_t &send_operation_t::operator= (send_operation_t &&) noexcept = default;

send_operation_t::send_operation_t (detail::spot_op_state_t &&state_)
    : _state (new detail::spot_op_state_t (std::move (state_)))
{
}

detail::spot_op_state_t &send_operation_t::state () noexcept
{
    return *_state;
}

const detail::spot_op_state_t &send_operation_t::state () const noexcept
{
    return *_state;
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
request_submit_operation_t::request_submit_operation_t (request_submit_operation_t &&) noexcept =
  default;
request_submit_operation_t &
request_submit_operation_t::operator= (request_submit_operation_t &&) noexcept = default;

request_submit_operation_t::request_submit_operation_t (detail::spot_op_state_t &&state_)
    : _state (new detail::spot_op_state_t (std::move (state_)))
{
}

detail::spot_op_state_t &request_submit_operation_t::state () noexcept
{
    return *_state;
}

const detail::spot_op_state_t &request_submit_operation_t::state () const noexcept
{
    return *_state;
}

request_submit_operation_t &&request_submit_operation_t::message (message_t &part_) &&
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
request_operation_t::request_operation_t (request_operation_t &&) noexcept = default;
request_operation_t &request_operation_t::operator= (request_operation_t &&) noexcept = default;

request_operation_t::request_operation_t (detail::spot_op_state_t &&state_)
    : _state (new detail::spot_op_state_t (std::move (state_)))
{
}

detail::spot_op_state_t &request_operation_t::state () noexcept
{
    return *_state;
}

const detail::spot_op_state_t &request_operation_t::state () const noexcept
{
    return *_state;
}

request_submit_operation_t request_operation_t::message (message_t &part_) &&
{
    state ().parts.push_back (std::move (part_));
    return request_submit_operation_t (std::move (state ()));
}

request_callback_submit_operation_t::~request_callback_submit_operation_t () = default;
request_callback_submit_operation_t::request_callback_submit_operation_t (
  request_callback_submit_operation_t &&) noexcept = default;
request_callback_submit_operation_t &
request_callback_submit_operation_t::operator= (
  request_callback_submit_operation_t &&) noexcept = default;

request_callback_submit_operation_t::request_callback_submit_operation_t (
  detail::spot_op_state_t &&state_)
    : _state (new detail::spot_op_state_t (std::move (state_)))
{
}

detail::spot_op_state_t &request_callback_submit_operation_t::state () noexcept
{
    return *_state;
}

const detail::spot_op_state_t &
request_callback_submit_operation_t::state () const noexcept
{
    return *_state;
}

request_callback_submit_operation_t &&
request_callback_submit_operation_t::message (message_t &part_) &&
{
    state ().parts.push_back (std::move (part_));
    return std::move (*this);
}

request_callback_submit_operation_t &&
request_callback_submit_operation_t::timeout (std::chrono::milliseconds timeout_) &&
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
reply_submit_operation_t::reply_submit_operation_t (reply_submit_operation_t &&) noexcept = default;
reply_submit_operation_t &
reply_submit_operation_t::operator= (reply_submit_operation_t &&) noexcept = default;

reply_submit_operation_t::reply_submit_operation_t (detail::spot_op_state_t &&state_)
    : _state (new detail::spot_op_state_t (std::move (state_)))
{
}

detail::spot_op_state_t &reply_submit_operation_t::state () noexcept
{
    return *_state;
}

const detail::spot_op_state_t &reply_submit_operation_t::state () const noexcept
{
    return *_state;
}

reply_submit_operation_t &&reply_submit_operation_t::message (message_t &part_) &&
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
reply_operation_t &reply_operation_t::operator= (reply_operation_t &&) noexcept = default;

reply_operation_t::reply_operation_t (detail::spot_op_state_t &&state_)
    : _state (new detail::spot_op_state_t (std::move (state_)))
{
}

detail::spot_op_state_t &reply_operation_t::state () noexcept
{
    return *_state;
}

const detail::spot_op_state_t &reply_operation_t::state () const noexcept
{
    return *_state;
}

reply_submit_operation_t reply_operation_t::message (message_t &part_) &&
{
    state ().parts.push_back (std::move (part_));
    return reply_submit_operation_t (std::move (state ()));
}

#define _state state ()

bool send_submit_operation_t::submit () &&
{
    if (!detail::has_send_parts (_state))
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (_state.kind) {
    case detail::spot_op_kind_t::raw_send:
    case detail::spot_op_kind_t::raw_routed_send:
    case detail::spot_op_kind_t::raw_publish:
    case detail::spot_op_kind_t::raw_router_send_spot:
        return detail::submit_raw_send_state (_state);
    case detail::spot_op_kind_t::received_send: {
        if (!_state.received || !_state.received->has_send_fn ())
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        if (detail::send_part_count (_state) == 1u
            && _state.received->_runtime
            && _state.received->_runtime->_send_context_handle
            && _state.received->_runtime->_send_context_kind
                 != received_t::send_context_kind_t::none
            && _state.received->_routing_id.has_value ()) {
            message_t &part = detail::send_single_part (_state);
            if (!part.valid ()) {
                detail::restore_single_send_part_to_source (_state);
                throw submit_error_t (
                  submit_result_t::invalid_argument, EINVAL);
            }

            zlink_submit_result_t direct_rc = ZLINK_SUBMIT_INVALID_ARGUMENT;
            void *send_context_handle = reinterpret_cast<void *> (
              _state.received->_runtime->_send_context_handle);
            if (_state.received->_runtime->_send_context_kind
                == received_t::send_context_kind_t::socket_rid) {
                direct_rc = zlink_send_part_rid (
                  send_context_handle,
                  zlink::detail::routing_id_native (
                    *_state.received->_routing_id),
                  zlink::detail::native_handle (part),
                  static_cast<zlink_send_flags_t> (static_cast<int> (_state.flags)),
                  ZLINK_PART_FINAL);
            } else if (_state.received->_spot_rid.has_value ()) {
                direct_rc = zlink_router_send_spot_part (
                  send_context_handle,
                  zlink::detail::routing_id_native (
                    *_state.received->_routing_id),
                  zlink::detail::routing_id_native (
                    *_state.received->_spot_rid),
                  zlink::detail::native_handle (part),
                  static_cast<zlink_send_flags_t> (static_cast<int> (_state.flags)),
                  ZLINK_PART_FINAL);
            }

            const submit_result_t result =
              static_cast<submit_result_t> (direct_rc);
            if (result == submit_result_t::ok) {
                zlink::detail::mark_sent (part);
                return true;
            }
            detail::restore_single_send_part_to_source (_state);
            if (_state.flags == send_flags_t::dontwait
                && result == submit_result_t::backpressured)
                return false;
            throw submit_error_t (result, zlink_errno ());
        }
        std::vector<message_t> parts = detail::take_send_parts (_state);
        const bool sent = _state.received->invoke_send_fn (parts, _state.flags);
        if (!sent)
            detail::restore_single_send_part_to_source (_state, parts);
        return sent;
    }
    case detail::spot_op_kind_t::bound_session_send:
        return detail::submit_bound_session_send_state (_state);
    case detail::spot_op_kind_t::stream_bound_actor_send:
        return detail::submit_stream_bound_actor_send_state (_state);
    default:
        break;
    }

    if (!_state.spot)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (_state.kind) {
    case detail::spot_op_kind_t::publish:
        if (detail::send_part_count (_state) == 1u
            && _state.flags == send_flags_t::dontwait
            && _state.discard_single_part_on_backpressure
            && _state.single_part.has_value ()
            && !_state.single_part_source) {
            return _state.spot->publish_discard_on_backpressure (
              _state.topic, *_state.single_part);
        }
        return detail::send_part_count (_state) == 1u
          ? _state.spot->publish (
              _state.topic, detail::send_single_part (_state), _state.flags)
          : _state.spot->publish (
              _state.topic, _state.parts, _state.flags);
    case detail::spot_op_kind_t::send_channel:
        return detail::send_part_count (_state) == 1u
          ? _state.spot->send_channel (
              _state.channel_name, detail::send_single_part (_state),
              _state.flags)
          : _state.spot->send_channel (
              _state.channel_name, _state.parts, _state.flags);
    case detail::spot_op_kind_t::send_to_spot:
        if (!_state.first_rid || !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        return detail::send_part_count (_state) == 1u
          ? _state.spot->send_to_spot (
              *_state.first_rid, *_state.second_rid,
              std::move (detail::send_single_part (_state)), _state.flags)
          : _state.spot->send_to_spot (
              *_state.first_rid, *_state.second_rid, _state.parts,
              _state.flags);
    default:
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

request_callback_submit_operation_t request_submit_operation_t::flags (int flags_) &&
{
    _state.flags = send_flags_t (flags_);
    return request_callback_submit_operation_t (std::move (_state));
}

async_result_t<std::vector<message_t>>
request_submit_operation_t::submit_async () &&
{
    if (_state.parts.empty ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    if (_state.kind == detail::spot_op_kind_t::raw_request
        || _state.kind == detail::spot_op_kind_t::raw_routed_request
        || _state.kind == detail::spot_op_kind_t::raw_router_request_spot) {
        if (!_state.raw_socket)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        if (_state.kind != detail::spot_op_kind_t::raw_request
            && !_state.first_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        if (_state.kind == detail::spot_op_kind_t::raw_router_request_spot
            && !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

        detail::request_state_t *request_state =
          detail::make_future_request_state ();
        std::future<std::vector<message_t>> future =
          request_state->promise->get_future ();
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (_state.parts, native) != 0) {
            delete request_state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
               bool is_final_) {
              const uint32_t timeout =
                is_final_ ? static_cast<uint32_t> (_state.timeout.count ()) : 0u;
              switch (_state.kind) {
              case detail::spot_op_kind_t::raw_request:
                  return zlink_dealer_request_part (
                    _state.raw_socket, part_out_, ZLINK_SEND_FLAGS_NONE,
                    part_flag_, timeout,
                    is_final_ ? &detail::request_callback_trampoline : NULL,
                    is_final_ ? request_state : NULL);
              case detail::spot_op_kind_t::raw_routed_request:
                  return zlink_router_request_part (
                    _state.raw_socket,
                    zlink::detail::routing_id_native (*_state.first_rid),
                    part_out_, ZLINK_SEND_FLAGS_NONE, part_flag_, timeout,
                    is_final_ ? &detail::request_callback_trampoline : NULL,
                    is_final_ ? request_state : NULL);
              case detail::spot_op_kind_t::raw_router_request_spot:
                  return zlink_router_request_spot_part (
                    _state.raw_socket,
                    zlink::detail::routing_id_native (*_state.first_rid),
                    zlink::detail::routing_id_native (*_state.second_rid),
                    part_out_,
                    is_final_ ? &detail::request_callback_trampoline : NULL,
                    is_final_ ? request_state : NULL, ZLINK_SEND_FLAGS_NONE,
                    part_flag_, timeout);
              default:
                  return ZLINK_SUBMIT_INVALID_ARGUMENT;
              }
          });
        if (rc != 0) {
            detail::close_native_parts (native, failed_index);
            delete request_state;
            throw last_error ();
        }
        return async_result_t<std::vector<message_t>> (
          std::move (future),
          zlink::detail::make_socket_request_progress (_state.raw_socket));
    }

    if (!_state.spot)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (_state.kind) {
    case detail::spot_op_kind_t::request_channel:
        return _state.spot->request_channel (
          _state.channel_name, _state.parts, _state.timeout);
    case detail::spot_op_kind_t::request_to_spot:
        if (!_state.first_rid || !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        return _state.spot->request_to_spot (
          *_state.first_rid, *_state.second_rid,
          std::move (_state.parts.front ()), _state.timeout);
    case detail::spot_op_kind_t::request_to_router:
        if (!_state.first_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        return _state.spot->request_to_router (
          *_state.first_rid, std::move (_state.parts.front ()),
          _state.timeout);
    default:
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

bool request_submit_operation_t::submit (request_callback_t callback_) &&
{
    request_callback_submit_operation_t ready (std::move (_state));
    return std::move (ready).submit (std::move (callback_));
}

bool request_callback_submit_operation_t::submit (request_callback_t callback_) &&
{
    if (_state.parts.empty ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    if (_state.kind == detail::spot_op_kind_t::raw_request
        || _state.kind == detail::spot_op_kind_t::raw_routed_request
        || _state.kind == detail::spot_op_kind_t::raw_router_request_spot) {
        if (!_state.raw_socket)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        if (_state.kind != detail::spot_op_kind_t::raw_request
            && !_state.first_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        if (_state.kind == detail::spot_op_kind_t::raw_router_request_spot
            && !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

        detail::request_state_t *request_state =
          detail::make_callback_request_state (std::move (callback_));
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (_state.parts, native) != 0) {
            delete request_state;
            throw last_error ();
        }
        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
               bool is_final_) {
              const uint32_t timeout =
                is_final_ ? static_cast<uint32_t> (_state.timeout.count ()) : 0u;
              switch (_state.kind) {
              case detail::spot_op_kind_t::raw_request:
                  return zlink_dealer_request_part (
                    _state.raw_socket, part_out_,
                    static_cast<zlink_send_flags_t> (static_cast<int> (_state.flags)),
                    part_flag_, timeout,
                    is_final_ ? &detail::request_callback_trampoline : NULL,
                    is_final_ ? request_state : NULL);
              case detail::spot_op_kind_t::raw_routed_request:
                  return zlink_router_request_part (
                    _state.raw_socket,
                    zlink::detail::routing_id_native (*_state.first_rid),
                    part_out_, static_cast<zlink_send_flags_t> (static_cast<int> (_state.flags)),
                    part_flag_, timeout,
                    is_final_ ? &detail::request_callback_trampoline : NULL,
                    is_final_ ? request_state : NULL);
              case detail::spot_op_kind_t::raw_router_request_spot:
                  return zlink_router_request_spot_part (
                    _state.raw_socket,
                    zlink::detail::routing_id_native (*_state.first_rid),
                    zlink::detail::routing_id_native (*_state.second_rid),
                    part_out_,
                    is_final_ ? &detail::request_callback_trampoline : NULL,
                    is_final_ ? request_state : NULL,
                    static_cast<zlink_send_flags_t> (static_cast<int> (_state.flags)),
                    part_flag_, timeout);
              default:
                  return ZLINK_SUBMIT_INVALID_ARGUMENT;
              }
          });
        if (rc != 0) {
            detail::close_native_parts (native, failed_index);
            delete request_state;
            const submit_error_t err (
              static_cast<submit_result_t> (rc), zlink_errno ());
            if (_state.flags == send_flags_t::dontwait
                && err.result () == submit_result_t::backpressured)
                return false;
            throw err;
        }
        return true;
    }

    if (!_state.spot)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (_state.kind) {
    case detail::spot_op_kind_t::request_channel:
        return _state.spot->request_channel (
          _state.channel_name, _state.parts, std::move (callback_),
          _state.flags, _state.timeout);
    case detail::spot_op_kind_t::request_to_spot:
        if (!_state.first_rid || !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        return _state.spot->request_to_spot (
          *_state.first_rid, *_state.second_rid,
          std::move (_state.parts.front ()), std::move (callback_),
          _state.flags, _state.timeout);
    case detail::spot_op_kind_t::request_to_router:
        if (!_state.first_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        return _state.spot->request_to_router (
          *_state.first_rid, std::move (_state.parts.front ()),
          std::move (callback_), _state.flags, _state.timeout);
    default:
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

void reply_submit_operation_t::submit () &&
{
    if (_state.parts.empty ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    if (_state.kind == detail::spot_op_kind_t::received_reply) {
        if (!_state.received || !_state.received->has_reply_fn ())
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        _state.received->invoke_reply_fn (_state.parts, _state.flags);
        return;
    }

    if (_state.kind == detail::spot_op_kind_t::raw_reply
        || _state.kind == detail::spot_op_kind_t::raw_router_reply_spot) {
        if (!_state.raw_socket || !_state.first_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        if (_state.kind == detail::spot_op_kind_t::raw_router_reply_spot
            && !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        zlink::detail::throw_if_reply_flags_unsupported (_state.flags);
        std::vector<zlink_msg_t> native;
        if (detail::move_parts_to_native (_state.parts, native) != 0)
            throw last_error ();
        size_t failed_index = 0;
        const int rc = detail::submit_native_parts (
          native, failed_index,
          [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
              if (_state.kind == detail::spot_op_kind_t::raw_reply) {
                  return zlink_router_reply_part (
                    _state.raw_socket,
                    zlink::detail::routing_id_native (*_state.first_rid),
                    _state.request_seq, part_out_, part_flag_);
              }
              return zlink_router_reply_spot_part (
                _state.raw_socket,
                zlink::detail::routing_id_native (*_state.first_rid),
                zlink::detail::routing_id_native (*_state.second_rid),
                _state.request_seq, part_out_, part_flag_);
          });
        if (rc != 0) {
            detail::restore_parts_from_native (
              _state.parts, native, failed_index);
            throw last_error ();
        }
        return;
    }

    if (!_state.spot)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    switch (_state.kind) {
    case detail::spot_op_kind_t::reply_to_spot:
        if (!_state.first_rid || !_state.second_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        _state.spot->reply_to_spot (
          *_state.first_rid, *_state.second_rid, _state.request_seq,
          std::move (_state.parts.front ()), _state.flags);
        return;
    case detail::spot_op_kind_t::reply_to_router:
        if (!_state.first_rid)
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
        _state.spot->reply_to_router (
          *_state.first_rid, _state.request_seq,
          std::move (_state.parts.front ()), _state.flags);
        return;
    default:
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

#undef _state

} // namespace service
} // namespace zlink
