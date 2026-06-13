/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "message_socket_contracts.hpp"
#include "../Service/actor_models.hpp"

#include <vector>

namespace zlink
{

/// @brief Exchanges framed packets with raw TCP peers and can host actor gateways.
class stream_socket_t : public routed_message_socket_t
{
  public:
    explicit stream_socket_t (context_t &ctx_);

    service::send_operation_t send (const routing_id_t &target_rid_);

    // Receive one message into a caller-provided received_t.
    // Returns 0 on success, a recv_result_t value on receive failure or no data, and -1 only for binding-local failure with errno set.
    int recv (received_t &out_, recv_flags_t flags_ = recv_flags_t::none);

    void set_packet_handler (
      std::function<void (const routing_id_t &, message_t &&, message_t &&)> handler_);

    void set_send_ready_handler (std::function<void ()> handler_)
    {
        socket_t::set_send_ready_handler (std::move (handler_));
    }

    void set_routing_id (const routing_id_t &routing_id_);

    void get_routing_id (routing_id_t &routing_id_) const;

    stream_socket_options_t options () { return stream_socket_options_t (*this); }

    void attach_actor_gateway (service::spot_node_t &node_);

    service::actor_bind_operation_t bind_actor (const routing_id_t &session_rid_,
                                                const actor_ref_t &actor_);

    service::actor_unbind_operation_t unbind_actor (const routing_id_t &session_rid_,
                                                    const std::string &actor_id_);

    service::send_operation_t send_bound_actor (const routing_id_t &session_rid_,
                                                const std::string &actor_id_);

    std::vector<actor_ref_t> bound_actors (const routing_id_t &session_rid_);

  private:
    std::function<void (const routing_id_t &, message_t &&, message_t &&)> _packet_handler;
    using routed_message_socket_t::recv;
    using socket_t::connect;
    using socket_t::disconnect;
    using socket_t::disconnect_rid;
};

} // namespace zlink
