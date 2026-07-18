/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Sockets/message_socket_contracts.hpp>

#include <Runtime/Sockets/detail.hpp>
#include <Runtime/Sockets/socket_access.hpp>
#include <Runtime/Native/native_options.hpp>
#include <Runtime/Service/spot_state.hpp>
#include <zlink/Contracts/Messaging/operation_contracts.hpp>

namespace zlink
{

dealer_socket_t::dealer_socket_t (context_t &ctx_) :
    message_socket_t (ctx_, socket_type::dealer),
    _default_request_timeout (std::chrono::milliseconds ())
{
}

service::send_operation_t dealer_socket_t::send ()
{
    auto state_ptr = service::detail::acquire_state ();
    state_ptr->kind = service::detail::spot_operation_kind_t::raw_send;
    state_ptr->raw.socket = detail::native_handle (*this);
    return service::send_operation_t (std::move (state_ptr));
}

service::request_operation_t dealer_socket_t::request ()
{
    auto state_ptr = service::detail::acquire_state ();
    state_ptr->kind = service::detail::spot_operation_kind_t::raw_request;
    state_ptr->raw.socket = detail::native_handle (*this);
    return service::request_operation_t (std::move (state_ptr));
}

int dealer_socket_t::recv (received_t &out_, recv_flags_t flags_)
{
    return socket_t::receive (out_, flags_);
}

int dealer_socket_t::recv (message_t &part_out_, recv_flags_t flags_)
{
    return detail::recv_single_part_message (detail::native_handle (*this), nullptr, part_out_,
                                             flags_);
}

void dealer_socket_t::set_routing_id (const routing_id_t &routing_id_)
{
    detail::set_routing_id_or_throw (detail::native_handle (*this), routing_id_);
}

void dealer_socket_t::get_routing_id (routing_id_t &routing_id_) const
{
    detail::get_routing_id_or_throw (const_cast<void *> (detail::native_handle (*this)),
                                     routing_id_);
}

void dealer_socket_t::channel_name (const std::string &value_)
{
    detail::validate_bounded_c_string (value_, 255u, "channel_name");
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_socket_set_channel_name (detail::native_handle (*this), value_.c_str ())));
}

std::string dealer_socket_t::channel_name () const
{
    config_result_t result = config_result_t::ok;
    std::string value;
    const int rc = detail::read_growing_string (
      [&] (char *buffer_, size_t capacity_, size_t *size_out_) {
          result = static_cast<config_result_t> (zlink_socket_get_channel_name (
            const_cast<void *> (detail::native_handle (*this)), buffer_, capacity_, size_out_));
          return result == config_result_t::ok ? 0 : -1;
      },
      256u, value);
    if (rc != 0)
        throw config_error_t (result, zlink_errno ());
    return value;
}

} // namespace zlink
