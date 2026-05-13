/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKETS_STREAM_HPP_INCLUDED
#define ZLINK_CPP_SOCKETS_STREAM_HPP_INCLUDED

#include "detail.hpp"

namespace zlink
{

class stream_socket_t : public routed_message_socket_t
{
  public:
    explicit stream_socket_t (context_t &ctx_)
        : routed_message_socket_t (ctx_, socket_type::stream)
    {
    }

    service::send_op_t send (const routing_id_t &target_rid_);

    // Receive one message into a caller-provided received_t.
    // Returns 0 on success, -1 on error (errno set).
    int recv (received_t &out_,
              recv_flags_t flags_ = recv_flags_t::none)
    {
        return base_socket_t::receive (out_, flags_);
    }

    template<class Handler>
    void on_packet (Handler &&handler_)
    {
        _packet_handler.reset (
          new packet_handler_model_t<typename std::decay<Handler>::type> (
            std::forward<Handler> (handler_)));
        if (base_socket_t::on_packet (
              &stream_socket_t::packet_trampoline, this)
            != 0)
            detail::throw_handler_error_from_errno (zlink_errno ());
    }

    void on_send_ready (std::function<void()> handler_)
    {
        base_socket_t::on_send_ready (std::move (handler_));
    }

    void set_routing_id (const routing_id_t &routing_id_)
    {
        if (base_socket_t::set_routing_id_raw (
              routing_id_.data (), routing_id_.size ())
            != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    void get_routing_id (routing_id_t &routing_id_) const
    {
        if (base_socket_t::get_routing_id_raw (routing_id_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    stream_socket_options_t options ()
    {
        return stream_socket_options_t (handle ());
    }

    service::actor_bind_op_t bind_actor (const routing_id_t &session_rid_,
                                         const actor_ref_t &actor_);

    service::actor_unbind_op_t unbind_actor (
      const routing_id_t &session_rid_,
      const std::string &actor_id_);

    service::send_op_t send_bound_actor (
      const routing_id_t &session_rid_,
      const std::string &actor_id_);

  private:
    static void packet_trampoline (void *,
                                   const zlink_routing_id_t *source_rid_,
                                   zlink_msg_t *header_,
                                   zlink_msg_t *body_,
                                   void *userdata_)
    {
        stream_socket_t *self = static_cast<stream_socket_t *> (userdata_);
        if (!self || !self->_packet_handler)
            return;
        const routing_id_t source =
          (source_rid_ && source_rid_->size > 0)
            ? zlink::detail::borrowed_routing_id (*source_rid_)
            : zlink::detail::unchecked_empty_routing_id ();
        message_t header {message_t::no_init_t ()};
        message_t body {message_t::no_init_t ()};
        header.adopt (header_);
        body.adopt (body_);
        self->_packet_handler->call (
          source, std::move (header), std::move (body));
    }

    struct packet_handler_base_t
    {
        virtual ~packet_handler_base_t () {}
        virtual void call (const routing_id_t &source_,
                           message_t &&header_,
                           message_t &&body_) = 0;
    };

    template<class Handler>
    struct packet_handler_model_t : packet_handler_base_t
    {
        explicit packet_handler_model_t (Handler &&handler_)
            : handler (std::move (handler_))
        {
        }

        explicit packet_handler_model_t (const Handler &handler_)
            : handler (handler_)
        {
        }

        void call (const routing_id_t &source_,
                   message_t &&header_,
                   message_t &&body_) override
        {
            handler (
              source_, std::move (header_), std::move (body_));
        }

        Handler handler;
    };

    std::unique_ptr<packet_handler_base_t> _packet_handler;
    using routed_message_socket_t::recv;
    using base_socket_t::connect;
    using base_socket_t::disconnect;
    using base_socket_t::disconnect_rid;
};

} // namespace zlink

#endif
