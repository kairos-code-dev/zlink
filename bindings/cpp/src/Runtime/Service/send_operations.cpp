/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Service/spot.hpp>
#include <Runtime/Messaging/received_access.hpp>
#include <Runtime/Service/detail.hpp>
#include <Runtime/Service/spot_operation_submit.hpp>
#include <Runtime/Service/spot_state.hpp>

namespace zlink
{
namespace service
{

send_submit_operation_t::~send_submit_operation_t ()
{
    detail::release_state (std::move (_state));
}
send_submit_operation_t::send_submit_operation_t (send_submit_operation_t &&) noexcept = default;
send_submit_operation_t &
send_submit_operation_t::operator= (send_submit_operation_t &&) noexcept = default;

send_submit_operation_t::send_submit_operation_t (detail::spot_operation_state_t &&state_) :
    _state (std::make_unique<detail::spot_operation_state_t> (std::move (state_)))
{
}

send_submit_operation_t::send_submit_operation_t (
  std::unique_ptr<detail::spot_operation_state_t> state_ptr_) noexcept :
    _state (std::move (state_ptr_))
{
}

detail::spot_operation_state_t &send_submit_operation_t::state () noexcept
{
    return (*_state);
}

const detail::spot_operation_state_t &send_submit_operation_t::state () const noexcept
{
    return (*_state);
}

send_submit_operation_t &&send_submit_operation_t::message (message_t &part_) &&
{
    detail::append_send_part (state (), part_);
    return std::move (*this);
}

send_submit_operation_t &&send_submit_operation_t::message (message_t &&part_) &&
{
    state ().single_part.emplace (std::move (part_));
    state ().single_part_source = nullptr;
    state ().discard_single_part_on_backpressure = true;
    return std::move (*this);
}

send_submit_operation_t &&send_submit_operation_t::flags (int flags_) &&
{
    state ().flags = send_flags_t (flags_);
    return std::move (*this);
}

send_operation_t::~send_operation_t ()
{
    detail::release_state (std::move (_state));
}
send_operation_t::send_operation_t (send_operation_t &&) noexcept = default;
send_operation_t &send_operation_t::operator= (send_operation_t &&) noexcept = default;

send_operation_t::send_operation_t (detail::spot_operation_state_t &&state_) :
    _state (std::make_unique<detail::spot_operation_state_t> (std::move (state_)))
{
}

send_operation_t::send_operation_t (
  std::unique_ptr<detail::spot_operation_state_t> state_ptr_) noexcept :
    _state (std::move (state_ptr_))
{
}

detail::spot_operation_state_t &send_operation_t::state () noexcept
{
    return (*_state);
}

const detail::spot_operation_state_t &send_operation_t::state () const noexcept
{
    return (*_state);
}

send_submit_operation_t send_operation_t::message (message_t &part_) &&
{
    state ().single_part_source = &part_;
    if (!detail::can_borrow_single_send_part (state ().kind))
        state ().single_part.emplace (std::move (part_));
    return send_submit_operation_t (std::move (_state));
}

send_submit_operation_t send_operation_t::message (message_t &&part_) &&
{
    state ().single_part.emplace (std::move (part_));
    state ().single_part_source = nullptr;
    state ().discard_single_part_on_backpressure = true;
    return send_submit_operation_t (std::move (_state));
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
            if (!state.received
                || !zlink::detail::received_access_t::has_send_context (*state.received))
                throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
            if (detail::send_part_count (state) == 1u) {
                message_t &part = detail::send_single_part (state);
                if (!part.valid ()) {
                    detail::restore_single_send_part_to_source (state);
                    throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
                }

                submit_result_t result = submit_result_t::invalid_argument;
                int result_errno = EINVAL;
                if (zlink::detail::received_access_t::submit_direct_send (
                      *state.received, part, state.flags, result, result_errno)) {
                    if (result == submit_result_t::ok)
                        return true;
                    detail::restore_single_send_part_to_source (state);
                    if (state.flags == send_flags_t::dontwait
                        && result == submit_result_t::backpressured)
                        return false;
                    throw submit_error_t (result, result_errno);
                }
            }
            std::vector<message_t> parts = detail::take_send_parts (state);
            const bool sent =
              zlink::detail::received_access_t::submit_send (*state.received, parts, state.flags);
            if (!sent)
                detail::restore_single_send_part_to_source (state, parts);
            return sent;
        }
        case detail::spot_operation_kind_t::bound_session_send:
        case detail::spot_operation_kind_t::actor_send:
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
            if (detail::send_part_count (state) == 1u && state.flags == send_flags_t::dontwait
                && state.discard_single_part_on_backpressure && state.single_part.has_value ()
                && !state.single_part_source) {
                return state.spot->publish_discard_on_backpressure (state.topic,
                                                                    *state.single_part);
            }
            return detail::send_part_count (state) == 1u
                     ? state.spot->publish (state.topic, detail::send_single_part (state),
                                            state.flags)
                     : state.spot->publish (state.topic, state.parts, state.flags);
        case detail::spot_operation_kind_t::send_channel:
            return detail::send_part_count (state) == 1u
                     ? state.spot->send_channel (state.channel_name,
                                                 detail::send_single_part (state), state.flags)
                     : state.spot->send_channel (state.channel_name, state.parts, state.flags);
        case detail::spot_operation_kind_t::send_to_spot:
            if (!state.first_rid || !state.second_rid)
                throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
            return detail::send_part_count (state) == 1u
                     ? state.spot->send_to_spot (*state.first_rid, *state.second_rid,
                                                 std::move (detail::send_single_part (state)),
                                                 state.flags)
                     : state.spot->send_to_spot (*state.first_rid, *state.second_rid, state.parts,
                                                 state.flags);
        default:
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    }
}

} // namespace service
} // namespace zlink
