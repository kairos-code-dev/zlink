/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Service/spot.hpp>
#include <Runtime/Service/detail.hpp>
#include <Runtime/Service/spot_state.hpp>

namespace zlink
{
namespace service
{
namespace
{

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

reply_submit_operation_t::~reply_submit_operation_t () = default;
reply_submit_operation_t::reply_submit_operation_t (
  reply_submit_operation_t &&) noexcept = default;
reply_submit_operation_t &reply_submit_operation_t::operator= (
  reply_submit_operation_t &&) noexcept = default;

reply_submit_operation_t::reply_submit_operation_t (
  detail::spot_operation_state_t &&state_) :
    _state (
      std::make_unique<detail::spot_operation_state_t> (std::move (state_)))
{
}

detail::spot_operation_state_t &reply_submit_operation_t::state () noexcept
{
    return (*_state);
}

const detail::spot_operation_state_t &
reply_submit_operation_t::state () const noexcept
{
    return (*_state);
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
    _state (
      std::make_unique<detail::spot_operation_state_t> (std::move (state_)))
{
}

detail::spot_operation_state_t &reply_operation_t::state () noexcept
{
    return (*_state);
}

const detail::spot_operation_state_t &reply_operation_t::state () const noexcept
{
    return (*_state);
}

reply_submit_operation_t reply_operation_t::message (message_t &part_) &&
{
    state ().parts.push_back (std::move (part_));
    return reply_submit_operation_t (std::move (state ()));
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
