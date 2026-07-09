/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Sockets/stream_socket.hpp>
#include <Runtime/Core/routing_id_access.hpp>
#include <Runtime/Native/message_access.hpp>
#include <Runtime/Sockets/detail.hpp>
#include <Runtime/Sockets/socket_access.hpp>
#include <Runtime/Sockets/socket_callback_state.hpp>
#include <Runtime/Service/spot_state.hpp>
#include <Runtime/Service/actor_detail.hpp>
#include <Runtime/Service/actor_model_access.hpp>
#include <zlink/Contracts/Service/spot.hpp>

namespace zlink
{

stream_socket_t::stream_socket_t (context_t &ctx_) :
    routed_message_socket_t (ctx_, socket_type::stream)
{
}

service::send_operation_t stream_socket_t::send (const routing_id_t &target_rid_)
{
    auto state_ptr = service::detail::acquire_state ();
    state_ptr->kind = service::detail::spot_operation_kind_t::raw_routed_send;
    state_ptr->raw.socket = detail::native_handle (*this);
    state_ptr->raw.target.first_rid = target_rid_;
    return service::send_operation_t (std::move (state_ptr));
}

int stream_socket_t::recv (received_t &out_, recv_flags_t flags_)
{
    return socket_t::receive (out_, flags_);
}

void stream_socket_t::set_packet_handler (
  std::function<void (const routing_id_t &, message_t &&, message_t &&)> handler_)
{
    detail::socket_callback_state_t &state = callback_state ();
    state.packet_handler = std::move (handler_);
    auto trampoline = [] (void *, const zlink_routing_id_t *source_rid_, zlink_msg_t *header_,
                          zlink_msg_t *body_, void *userdata_) {
        auto *callback_state = static_cast<detail::socket_callback_state_t *> (userdata_);
        if (!callback_state || !callback_state->packet_handler)
            return;
        const routing_id_t source = zlink::detail::routing_id_from_native_pointer (source_rid_);
        message_t header{message_t::no_init_t ()};
        message_t body{message_t::no_init_t ()};
        zlink::detail::adopt_native_message (header, header_);
        zlink::detail::adopt_native_message (body, body_);
        callback_state->packet_handler (source, std::move (header), std::move (body));
    };
    if (zlink_stream_packet_handler (detail::native_handle (*this),
                                     static_cast<zlink_stream_packet_handler_fn> (+trampoline),
                                     &state)
        != 0)
        throw handler_error_t (detail::handler_result_from_errno (detail::current_errno ()),
                               detail::current_errno ());
}

void stream_socket_t::set_routing_id (const routing_id_t &routing_id_)
{
    detail::set_routing_id_or_throw (detail::native_handle (*this), routing_id_);
}

void stream_socket_t::get_routing_id (routing_id_t &routing_id_) const
{
    detail::get_routing_id_or_throw (const_cast<void *> (detail::native_handle (*this)),
                                     routing_id_);
}

service::actor_bind_operation_t stream_socket_t::bind_actor (const routing_id_t &session_rid_,
                                                             const actor_ref_t &actor_)
{
    service::detail::actor_bind_state_t state;
    state.stream = detail::native_handle (*this);
    state.session_rid = session_rid_;
    state.actor = actor_;
    return service::detail_make_actor_bind_operation (std::move (state));
}

service::actor_unbind_operation_t stream_socket_t::unbind_actor (const routing_id_t &session_rid_,
                                                                 const std::string &actor_id_)
{
    detail::validate_bounded_c_string (actor_id_, ZLINK_ACTOR_ID_MAX - 1u, "actor_id");
    service::detail::actor_bind_state_t state;
    state.stream = detail::native_handle (*this);
    state.session_rid = session_rid_;
    state.actor_id = actor_id_;
    return service::detail_make_actor_unbind_operation (std::move (state));
}

service::send_operation_t stream_socket_t::send_bound_actor (const routing_id_t &session_rid_,
                                                             const std::string &actor_id_)
{
    detail::validate_bounded_c_string (actor_id_, ZLINK_ACTOR_ID_MAX - 1u, "actor_id");
    auto state_ptr = service::detail::acquire_state ();
    state_ptr->kind = service::detail::spot_operation_kind_t::stream_bound_actor_send;
    state_ptr->stream_actor.stream = detail::native_handle (*this);
    state_ptr->stream_actor.target.first_rid = session_rid_;
    state_ptr->stream_actor.actor_id = actor_id_;
    return service::send_operation_t (std::move (state_ptr));
}

std::vector<actor_ref_t> stream_socket_t::bound_actors (const routing_id_t &session_rid_)
{
    zlink_routing_id_t session = detail::routing_id_native_value (session_rid_);
    size_t count = 0;
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_stream_bound_actors (detail::native_handle (*this), &session, nullptr, &count)));
    if (count == 0)
        return {};

    std::vector<zlink_actor_ref_t> entries (count);
    size_t actual = count;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_stream_bound_actors (
        detail::native_handle (*this), &session, entries.data (), &actual)));
    if (actual < entries.size ())
        entries.resize (actual);

    std::vector<actor_ref_t> out;
    out.reserve (entries.size ());
    for (const zlink_actor_ref_t &entry : entries)
        out.push_back (detail::actor_model_access_t::from_native (entry));
    return out;
}

} // namespace zlink
