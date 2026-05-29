/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Sockets/message_socket_contracts.hpp>

#include <Runtime/Sockets/detail.hpp>
#include <Runtime/Sockets/socket_access.hpp>
#include <Runtime/Service/spot_state.hpp>
#include <zlink/Contracts/Service/spot.hpp>

namespace zlink
{

dealer_socket_t::dealer_socket_t (context_t &ctx_)
    : message_socket_t (ctx_, socket_type::dealer),
      _default_request_timeout (std::chrono::milliseconds ())
{
}

service::send_operation_t dealer_socket_t::send ()
{
    service::detail::spot_operation_state_t state;
    state.kind = service::detail::spot_operation_kind_t::raw_send;
    state.raw_socket = detail::native_handle (*this);
    return service::send_operation_t (std::move (state));
}

service::request_operation_t dealer_socket_t::request ()
{
    service::detail::spot_operation_state_t state;
    state.kind = service::detail::spot_operation_kind_t::raw_request;
    state.raw_socket = detail::native_handle (*this);
    return service::request_operation_t (std::move (state));
}

int dealer_socket_t::recv (received_t &out_, recv_flags_t flags_)
{
    return socket_t::receive (out_, flags_);
}

int dealer_socket_t::recv (message_t &part_out_, recv_flags_t flags_)
{
    return detail::recv_single_part_message (detail::native_handle (*this), nullptr, part_out_, flags_);
}

void dealer_socket_t::set_routing_id (const routing_id_t &routing_id_)
{
    if (socket_t::set_routing_id_raw (std::as_bytes (
          std::span<const uint8_t> (routing_id_.data (), routing_id_.size ())))
        != 0)
        throw config_error_t (
          detail::config_result_from_errno (zlink_errno ()), zlink_errno ());
}

void dealer_socket_t::get_routing_id (routing_id_t &routing_id_) const
{
    if (socket_t::get_routing_id_raw (routing_id_) != 0)
        throw config_error_t (
          detail::config_result_from_errno (zlink_errno ()), zlink_errno ());
}

void dealer_socket_t::channel_name (const std::string &value_)
{
    detail::validate_bounded_c_string (value_, 255u, "channel_name");
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_socket_set_channel_name (detail::native_handle (*this), value_.c_str ())));
}

std::string dealer_socket_t::channel_name () const
{
    size_t size = 0;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_socket_get_channel_name (
          const_cast<void *> (detail::native_handle (*this)), nullptr, 0, &size)));
    std::vector<char> buffer (size);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_socket_get_channel_name (
          const_cast<void *> (detail::native_handle (*this)),
          buffer.empty () ? nullptr : buffer.data (), buffer.size (), &size)));
    return std::string (buffer.data (), size);
}

} // namespace zlink
