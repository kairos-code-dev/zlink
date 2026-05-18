/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_RUNTIME_SERVICE_SPOT_SUBMIT_HPP_INCLUDED
#define ZLINK_CPP_RUNTIME_SERVICE_SPOT_SUBMIT_HPP_INCLUDED

#include "../../Contracts/Service/spot_state.hpp"
#include "../Native/native_parts.hpp"

namespace zlink
{
namespace service
{
namespace detail
{

inline std::vector<message_t> take_send_parts (spot_op_state_t &state_)
{
    std::vector<message_t> parts;
    if (state_.single_part.has_value ()) {
        parts.push_back (std::move (*state_.single_part));
        state_.single_part_source = NULL;
    } else if (state_.single_part_source) {
        parts.push_back (std::move (*state_.single_part_source));
        state_.single_part_source = NULL;
    } else {
        parts = std::move (state_.parts);
    }
    return parts;
}

inline bool submit_raw_send_state (spot_op_state_t &state_)
{
    const auto throw_invalid_argument = [&] () {
        restore_single_send_part_to_source (state_);
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);
    };
    if (!state_.raw_socket)
        throw_invalid_argument ();
    const zlink_routing_id_t *first_rid = state_first_rid_native (state_);
    const zlink_routing_id_t *second_rid = state_second_rid_native (state_);

    if (state_.kind == spot_op_kind_t::raw_routed_send && !first_rid)
        throw_invalid_argument ();
    if (state_.kind == spot_op_kind_t::raw_router_send_spot
        && (!first_rid || !second_rid))
        throw_invalid_argument ();
    if (state_.kind == spot_op_kind_t::raw_publish && state_.topic.empty ())
        throw_invalid_argument ();

    if (state_.single_part.has_value () || state_.single_part_source) {
        message_t &part = send_single_part (state_);
        if (!part.valid ())
            throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

        zlink_submit_result_t direct_rc = ZLINK_SUBMIT_INVALID_ARGUMENT;
        switch (state_.kind) {
        case spot_op_kind_t::raw_send:
            direct_rc = zlink_send_part (
              state_.raw_socket, zlink::detail::native_handle (part),
              static_cast<zlink_send_flags_t> (state_.flags), ZLINK_PART_FINAL);
            break;
        case spot_op_kind_t::raw_routed_send:
            direct_rc = zlink_send_part_rid (
              state_.raw_socket, first_rid,
              zlink::detail::native_handle (part),
              static_cast<zlink_send_flags_t> (state_.flags), ZLINK_PART_FINAL);
            break;
        case spot_op_kind_t::raw_publish:
            direct_rc = zlink_publish_part (
              state_.raw_socket, state_.topic.c_str (),
              zlink::detail::native_handle (part),
              static_cast<zlink_send_flags_t> (state_.flags), ZLINK_PART_FINAL);
            break;
        case spot_op_kind_t::raw_router_send_spot:
            direct_rc = zlink_router_send_spot_part (
              state_.raw_socket, first_rid, second_rid,
              zlink::detail::native_handle (part),
              static_cast<zlink_send_flags_t> (state_.flags), ZLINK_PART_FINAL);
            break;
        default:
            break;
        }

        const submit_result_t rc = static_cast<submit_result_t> (direct_rc);
        if (rc == submit_result_t::ok) {
            zlink::detail::mark_sent (part);
            return true;
        }
        restore_single_send_part_to_source (state_);
        if (state_.flags == send_flags_t::dontwait
            && rc == submit_result_t::backpressured) {
            return false;
        }
        throw submit_error_t (rc, zlink_errno ());
    }

    std::vector<message_t> parts = take_send_parts (state_);
    std::vector<zlink_msg_t> native;
    if (zlink::detail::move_parts_to_native (parts, native) != 0)
        throw last_error ();
    size_t failed_index = 0;
    const submit_result_t rc = static_cast<submit_result_t> (
      zlink::detail::submit_native_parts (
        native, failed_index,
        [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
            switch (state_.kind) {
            case spot_op_kind_t::raw_send:
                return zlink_send_part (
                  state_.raw_socket, part_out_,
                  static_cast<zlink_send_flags_t> (state_.flags), part_flag_);
            case spot_op_kind_t::raw_routed_send:
                return zlink_send_part_rid (
                  state_.raw_socket, first_rid, part_out_,
                  static_cast<zlink_send_flags_t> (state_.flags), part_flag_);
            case spot_op_kind_t::raw_publish:
                return zlink_publish_part (
                  state_.raw_socket, state_.topic.c_str (), part_out_,
                  static_cast<zlink_send_flags_t> (state_.flags), part_flag_);
            case spot_op_kind_t::raw_router_send_spot:
                return zlink_router_send_spot_part (
                  state_.raw_socket, first_rid, second_rid, part_out_,
                  static_cast<zlink_send_flags_t> (state_.flags), part_flag_);
            default:
                return ZLINK_SUBMIT_INVALID_ARGUMENT;
            }
        }));
    if (rc != submit_result_t::ok) {
        zlink::detail::close_native_parts (native, failed_index);
        if (state_.flags == send_flags_t::dontwait
            && rc == submit_result_t::backpressured) {
            restore_single_send_part_to_source (state_, parts);
            return false;
        }
        throw submit_error_t (rc, zlink_errno ());
    }
    return true;
}

inline bool submit_bound_session_send_state (spot_op_state_t &state_)
{
    if (!state_.node || !state_.actor)
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    std::vector<message_t> parts = take_send_parts (state_);
    for (size_t i = 0; i < parts.size (); ++i) {
        zlink_msg_t native;
        zlink::detail::move_to_native (parts[i], &native);
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_spot_node_actor_send_bound_session_msg (
            zlink::detail::native_handle (*state_.node),
            zlink::detail::actor_ref_native (*state_.actor),
            &native, static_cast<zlink_send_flags_t> (state_.flags)));
        if (rc != submit_result_t::ok) {
            parts[i].init ();
            (void) zlink_msg_move (
              zlink::detail::native_handle (parts[i]), &native);
            if (state_.flags == send_flags_t::dontwait
                && rc == submit_result_t::backpressured) {
                restore_single_send_part_to_source (state_, parts);
                return false;
            }
            throw submit_error_t (rc, zlink_errno ());
        }
    }
    return true;
}

inline bool submit_stream_bound_actor_send_state (spot_op_state_t &state_)
{
    if (!state_.stream || !state_.first_rid || state_.actor_id.empty ())
        throw submit_error_t (submit_result_t::invalid_argument, EINVAL);

    std::vector<message_t> parts = take_send_parts (state_);
    std::vector<zlink_msg_t> native;
    if (zlink::detail::move_parts_to_native (parts, native) != 0)
        throw last_error ();
    size_t failed_index = 0;
    const submit_result_t rc = static_cast<submit_result_t> (
      zlink::detail::submit_native_parts (
        native, failed_index,
        [&] (zlink_msg_t *part_out_, zlink_part_flag_t part_flag_, bool) {
            return zlink_stream_send_bound_actor_part (
              state_.stream, zlink::detail::routing_id_native (*state_.first_rid),
              state_.actor_id.c_str (), part_out_,
              static_cast<zlink_send_flags_t> (state_.flags), part_flag_);
        }));
    if (rc != submit_result_t::ok) {
        zlink::detail::close_native_parts (native, failed_index);
        if (state_.flags == send_flags_t::dontwait
            && rc == submit_result_t::backpressured) {
            restore_single_send_part_to_source (state_, parts);
            return false;
        }
        throw submit_error_t (rc, zlink_errno ());
    }
    return true;
}

} // namespace detail
} // namespace service
} // namespace zlink

#endif
