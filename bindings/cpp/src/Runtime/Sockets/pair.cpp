/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Sockets/message_socket_contracts.hpp>

#include <Runtime/Sockets/detail.hpp>
#include <Runtime/Sockets/socket_access.hpp>
#include <Runtime/Service/spot_state.hpp>
#include <zlink/Contracts/Service/spot.hpp>

namespace zlink
{

pair_socket_t::pair_socket_t (context_t &ctx_)
    : message_socket_t (ctx_, socket_type::pair)
{
}

service::send_operation_t pair_socket_t::send ()
{
    service::detail::spot_operation_state_t state;
    state.kind = service::detail::spot_operation_kind_t::raw_send;
    state.raw_socket = detail::native_handle (*this);
    return service::send_operation_t (std::move (state));
}

int pair_socket_t::recv (received_t &out_, recv_flags_t flags_)
{
    return socket_t::receive (out_, flags_);
}

int pair_socket_t::recv (message_t &part_out_, recv_flags_t flags_)
{
    return detail::recv_single_part_message (detail::native_handle (*this), NULL, part_out_, flags_);
}

void pair_socket_t::set_send_ready_handler (std::function<void()> handler_)
{
    socket_t::set_send_ready_handler (std::move (handler_));
}

} // namespace zlink
