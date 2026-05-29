/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Sockets/routed_socket_contracts.hpp>

#include <Runtime/Sockets/detail.hpp>
#include <Runtime/Sockets/socket_access.hpp>
#include <Runtime/Messaging/received_access.hpp>
#include <Runtime/Service/spot_state.hpp>
#include <zlink/Contracts/Service/spot.hpp>

namespace zlink
{

router_socket_t::router_socket_t (context_t &ctx_) :
    routed_message_socket_t (ctx_, socket_type::router),
    _default_request_timeout (std::chrono::milliseconds ())
{
}

service::send_operation_t
router_socket_t::send (const routing_id_t &target_rid_)
{
    service::detail::spot_operation_state_t state;
    state.kind = service::detail::spot_operation_kind_t::raw_routed_send;
    state.raw_socket = detail::native_handle (*this);
    service::detail::cache_first_rid_native (state, target_rid_);
    return service::send_operation_t (std::move (state));
}

int router_socket_t::recv (received_t &out_, recv_flags_t flags_)
{
    const int rc = socket_t::receive (out_, flags_);
    if (rc != 0)
        return rc;
    if (out_.request_seq ().has_value () && out_.routing_id ().has_value ()) {
        void *router_handle_ = detail::native_handle (*this);
        const routing_id_t reply_node_rid = *out_.routing_id ();
        const uint64_t request_seq = *out_.request_seq ();
        detail::received_access_t::set_reply_fn (
          out_, detail::make_router_reply_fn (router_handle_, reply_node_rid,
                                              out_.spot_rid (), request_seq));
    }
    if (out_.routing_id ().has_value ()) {
        void *router_handle_ = detail::native_handle (*this);
        const routing_id_t send_node_rid = *out_.routing_id ();
        detail::received_access_t::set_send_fn (
          out_, detail::make_router_send_fn (router_handle_, send_node_rid,
                                             out_.spot_rid ()));
        if (out_.spot_rid ().has_value ()) {
            detail::received_access_t::set_router_spot_send_context (
              out_, router_handle_);
        } else {
            detail::received_access_t::set_socket_rid_send_context (
              out_, router_handle_);
        }
    }
    return 0;
}

int router_socket_t::recv (routing_id_t &source_rid_out_,
                           message_t &part_out_,
                           recv_flags_t flags_)
{
    return detail::recv_single_part_routed_message (
      detail::native_handle (*this), source_rid_out_, part_out_, flags_);
}

void router_socket_t::set_send_ready_handler (std::function<void ()> handler_)
{
    socket_t::set_send_ready_handler (std::move (handler_));
}

void router_socket_t::set_routing_id (const routing_id_t &routing_id_)
{
    if (socket_t::set_routing_id_raw (std::as_bytes (
          std::span<const uint8_t> (routing_id_.data (), routing_id_.size ())))
        != 0)
        throw config_error_t (detail::config_result_from_errno (zlink_errno ()),
                              zlink_errno ());
}

void router_socket_t::get_routing_id (routing_id_t &routing_id_) const
{
    if (socket_t::get_routing_id_raw (routing_id_) != 0)
        throw config_error_t (detail::config_result_from_errno (zlink_errno ()),
                              zlink_errno ());
}

service::request_operation_t
router_socket_t::request (const routing_id_t &routing_id_)
{
    service::detail::spot_operation_state_t state;
    state.kind = service::detail::spot_operation_kind_t::raw_routed_request;
    state.raw_socket = detail::native_handle (*this);
    state.first_rid = routing_id_;
    return service::request_operation_t (std::move (state));
}

service::reply_operation_t
router_socket_t::reply (const routing_id_t &routing_id_, uint64_t request_seq_)
{
    service::detail::spot_operation_state_t state;
    state.kind = service::detail::spot_operation_kind_t::raw_reply;
    state.raw_socket = detail::native_handle (*this);
    state.first_rid = routing_id_;
    state.request_seq = request_seq_;
    return service::reply_operation_t (std::move (state));
}

service::send_operation_t
router_socket_t::send_to_spot (const routing_id_t &dest_node_rid_,
                               const routing_id_t &dest_spot_rid_)
{
    service::detail::spot_operation_state_t state;
    state.kind = service::detail::spot_operation_kind_t::raw_router_send_spot;
    state.raw_socket = detail::native_handle (*this);
    state.first_rid = dest_node_rid_;
    state.second_rid = dest_spot_rid_;
    return service::send_operation_t (std::move (state));
}

service::request_operation_t
router_socket_t::request_to_spot (const routing_id_t &dest_node_rid_,
                                  const routing_id_t &dest_spot_rid_)
{
    service::detail::spot_operation_state_t state;
    state.kind =
      service::detail::spot_operation_kind_t::raw_router_request_spot;
    state.raw_socket = detail::native_handle (*this);
    state.first_rid = dest_node_rid_;
    state.second_rid = dest_spot_rid_;
    return service::request_operation_t (std::move (state));
}

service::reply_operation_t
router_socket_t::reply_to_spot (const routing_id_t &dest_node_rid_,
                                const routing_id_t &dest_spot_rid_,
                                uint64_t request_seq_)
{
    service::detail::spot_operation_state_t state;
    state.kind = service::detail::spot_operation_kind_t::raw_router_reply_spot;
    state.raw_socket = detail::native_handle (*this);
    state.first_rid = dest_node_rid_;
    state.second_rid = dest_spot_rid_;
    state.request_seq = request_seq_;
    return service::reply_operation_t (std::move (state));
}

} // namespace zlink
