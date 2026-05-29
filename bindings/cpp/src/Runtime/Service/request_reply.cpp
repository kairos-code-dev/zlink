/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Service/spot.hpp>
#include <Runtime/Core/duration_conversion.hpp>
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

zlink_submit_result_t
submit_raw_request_part (detail::spot_operation_state_t &state_,
                         zlink_msg_t *part_out_,
                         zlink_part_flag_t part_flag_,
                         bool is_final_,
                         zlink_send_flags_t flags_,
                         detail::request_state_t *request_state_)
{
    const uint32_t timeout =
      is_final_ ? zlink::detail::native_timeout_ms (state_.timeout) : 0u;
    auto callback = is_final_ ? &detail::request_callback_trampoline : nullptr;
    void *userdata = is_final_ ? request_state_ : nullptr;

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

async_result_t<std::vector<message_t> >
submit_raw_request_async (detail::spot_operation_state_t &state_)
{
    ensure_raw_request_state (state_);

    std::unique_ptr<detail::request_state_t> request_state (
      detail::make_future_request_state ());
    std::future<std::vector<message_t> > future =
      request_state->promise->get_future ();
    std::vector<zlink_msg_t> native;
    if (detail::move_parts_to_native (state_.parts, native) != 0)
        throw last_error ();

    size_t failed_index = 0;
    const int rc = detail::submit_native_parts (
      native, failed_index,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
           bool is_final_) {
          return submit_raw_request_part (state_, part_out_, part_flag_,
                                          is_final_, ZLINK_SEND_FLAGS_NONE,
                                          request_state.get ());
      });
    if (rc != 0) {
        detail::close_native_parts (native, failed_index);
        throw last_error ();
    }

    request_state.release ();
    return async_result_t<std::vector<message_t> > (
      std::move (future),
      zlink::detail::make_socket_request_progress (state_.raw_socket));
}

bool submit_raw_request_callback (detail::spot_operation_state_t &state_,
                                  request_callback_t callback_)
{
    ensure_raw_request_state (state_);

    std::unique_ptr<detail::request_state_t> request_state (
      detail::make_callback_request_state (std::move (callback_)));
    std::vector<zlink_msg_t> native;
    if (detail::move_parts_to_native (state_.parts, native) != 0)
        throw last_error ();

    size_t failed_index = 0;
    const int rc = detail::submit_native_parts (
      native, failed_index,
      [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_,
           bool is_final_) {
          return submit_raw_request_part (
            state_, part_out_, part_flag_, is_final_,
            static_cast<zlink_send_flags_t> (static_cast<int> (state_.flags)),
            request_state.get ());
      });
    if (rc != 0) {
        detail::close_native_parts (native, failed_index);
        const submit_error_t err (static_cast<submit_result_t> (rc),
                                  zlink_errno ());
        if (state_.flags == send_flags_t::dontwait
            && err.result () == submit_result_t::backpressured)
            return false;
        throw err;
    }
    request_state.release ();
    return true;
}

} // namespace

request_submit_operation_t::~request_submit_operation_t () = default;
request_submit_operation_t::request_submit_operation_t (
  request_submit_operation_t &&) noexcept = default;
request_submit_operation_t &request_submit_operation_t::operator= (
  request_submit_operation_t &&) noexcept = default;

request_submit_operation_t::request_submit_operation_t (
  detail::spot_operation_state_t &&state_) :
    _state (
      std::make_unique<detail::spot_operation_state_t> (std::move (state_)))
{
}

detail::spot_operation_state_t &request_submit_operation_t::state () noexcept
{
    return (*_state);
}

const detail::spot_operation_state_t &
request_submit_operation_t::state () const noexcept
{
    return (*_state);
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
    _state (
      std::make_unique<detail::spot_operation_state_t> (std::move (state_)))
{
}

detail::spot_operation_state_t &request_operation_t::state () noexcept
{
    return (*_state);
}

const detail::spot_operation_state_t &
request_operation_t::state () const noexcept
{
    return (*_state);
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
    _state (
      std::make_unique<detail::spot_operation_state_t> (std::move (state_)))
{
}

detail::spot_operation_state_t &
request_callback_submit_operation_t::state () noexcept
{
    return (*_state);
}

const detail::spot_operation_state_t &
request_callback_submit_operation_t::state () const noexcept
{
    return (*_state);
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

} // namespace service
} // namespace zlink
