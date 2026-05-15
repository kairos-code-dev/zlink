/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_SPOT_SOCKET_OPS_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_SPOT_SOCKET_OPS_HPP_INCLUDED

#include "spot.hpp"

namespace zlink
{

inline service::send_op_t pair_socket_t::send ()
{
    service::detail::spot_op_state_t state;
    state.kind = service::detail::spot_op_kind_t::raw_send;
    state.raw_socket = handle ();
    return service::send_op_t (std::move (state));
}

inline service::send_op_t dealer_socket_t::send ()
{
    service::detail::spot_op_state_t state;
    state.kind = service::detail::spot_op_kind_t::raw_send;
    state.raw_socket = handle ();
    return service::send_op_t (std::move (state));
}

inline service::request_op_t dealer_socket_t::request ()
{
    service::detail::spot_op_state_t state;
    state.kind = service::detail::spot_op_kind_t::raw_request;
    state.raw_socket = handle ();
    return service::request_op_t (std::move (state));
}

inline service::send_op_t router_socket_t::send (
  const routing_id_t &target_rid_)
{
    service::detail::spot_op_state_t state;
    state.kind = service::detail::spot_op_kind_t::raw_routed_send;
    state.raw_socket = handle ();
    state.first_rid = target_rid_;
    return service::send_op_t (std::move (state));
}

inline service::request_op_t router_socket_t::request (
  const routing_id_t &routing_id_)
{
    service::detail::spot_op_state_t state;
    state.kind = service::detail::spot_op_kind_t::raw_routed_request;
    state.raw_socket = handle ();
    state.first_rid = routing_id_;
    return service::request_op_t (std::move (state));
}

inline service::reply_op_t router_socket_t::reply (
  const routing_id_t &routing_id_, uint64_t request_seq_)
{
    service::detail::spot_op_state_t state;
    state.kind = service::detail::spot_op_kind_t::raw_reply;
    state.raw_socket = handle ();
    state.first_rid = routing_id_;
    state.request_seq = request_seq_;
    return service::reply_op_t (std::move (state));
}

inline service::send_op_t router_socket_t::send_to_spot (
  const routing_id_t &dest_node_rid_, const routing_id_t &dest_spot_rid_)
{
    service::detail::spot_op_state_t state;
    state.kind = service::detail::spot_op_kind_t::raw_router_send_spot;
    state.raw_socket = handle ();
    state.first_rid = dest_node_rid_;
    state.second_rid = dest_spot_rid_;
    return service::send_op_t (std::move (state));
}

inline service::request_op_t router_socket_t::request_to_spot (
  const routing_id_t &dest_node_rid_, const routing_id_t &dest_spot_rid_)
{
    service::detail::spot_op_state_t state;
    state.kind = service::detail::spot_op_kind_t::raw_router_request_spot;
    state.raw_socket = handle ();
    state.first_rid = dest_node_rid_;
    state.second_rid = dest_spot_rid_;
    return service::request_op_t (std::move (state));
}

inline service::reply_op_t router_socket_t::reply_to_spot (
  const routing_id_t &dest_node_rid_, const routing_id_t &dest_spot_rid_,
  uint64_t request_seq_)
{
    service::detail::spot_op_state_t state;
    state.kind = service::detail::spot_op_kind_t::raw_router_reply_spot;
    state.raw_socket = handle ();
    state.first_rid = dest_node_rid_;
    state.second_rid = dest_spot_rid_;
    state.request_seq = request_seq_;
    return service::reply_op_t (std::move (state));
}

inline service::send_op_t stream_socket_t::send (
  const routing_id_t &target_rid_)
{
    service::detail::spot_op_state_t state;
    state.kind = service::detail::spot_op_kind_t::raw_routed_send;
    state.raw_socket = handle ();
    state.first_rid = target_rid_;
    return service::send_op_t (std::move (state));
}

inline service::send_op_t pub_socket_t::publish (
  const std::string &topic_id_)
{
    detail::validate_no_embedded_null (topic_id_, "topic");
    service::detail::spot_op_state_t state;
    state.kind = service::detail::spot_op_kind_t::raw_publish;
    state.raw_socket = handle ();
    state.topic = topic_id_;
    return service::send_op_t (std::move (state));
}

inline service::send_op_t xpub_socket_t::publish (
  const std::string &topic_id_)
{
    detail::validate_no_embedded_null (topic_id_, "topic");
    service::detail::spot_op_state_t state;
    state.kind = service::detail::spot_op_kind_t::raw_publish;
    state.raw_socket = handle ();
    state.topic = topic_id_;
    return service::send_op_t (std::move (state));
}

inline service::actor_bind_op_t stream_socket_t::bind_actor (
  const routing_id_t &session_rid_, const actor_ref_t &actor_)
{
    service::detail::actor_bind_state_t state;
    state.stream = handle ();
    state.session_rid = session_rid_;
    state.actor = actor_;
    return service::detail_make_actor_bind_op (std::move (state));
}

inline service::actor_unbind_op_t stream_socket_t::unbind_actor (
  const routing_id_t &session_rid_, const std::string &actor_id_)
{
    detail::validate_bounded_c_string (
      actor_id_, ZLINK_ACTOR_ID_MAX - 1u, "actor_id");
    service::detail::actor_bind_state_t state;
    state.stream = handle ();
    state.session_rid = session_rid_;
    state.actor_id = actor_id_;
    return service::detail_make_actor_unbind_op (std::move (state));
}

inline service::send_op_t stream_socket_t::send_bound_actor (
  const routing_id_t &session_rid_, const std::string &actor_id_)
{
    detail::validate_bounded_c_string (
      actor_id_, ZLINK_ACTOR_ID_MAX - 1u, "actor_id");
    service::detail::spot_op_state_t state;
    state.kind = service::detail::spot_op_kind_t::stream_bound_actor_send;
    state.stream = handle ();
    state.first_rid = session_rid_;
    state.actor_id = actor_id_;
    return service::send_op_t (std::move (state));
}

inline service::send_op_t received_t::send ()
{
    service::detail::spot_op_state_t state;
    state.kind = service::detail::spot_op_kind_t::received_send;
    state.received = this;
    return service::send_op_t (std::move (state));
}

inline service::reply_op_t received_t::reply ()
{
    service::detail::spot_op_state_t state;
    state.kind = service::detail::spot_op_kind_t::received_reply;
    state.received = this;
    return service::reply_op_t (std::move (state));
}

namespace detail
{
inline void *native_handle (service::spot_node_t &node_) noexcept
{
    return node_._node;
}

inline const void *native_handle (const service::spot_node_t &node_) noexcept
{
    return node_._node;
}

inline void *native_handle (service::spot_t &spot_) noexcept
{
    return spot_._spot;
}

inline const void *native_handle (const service::spot_t &spot_) noexcept
{
    return spot_._spot;
}
} // namespace detail


} // namespace zlink

#endif
