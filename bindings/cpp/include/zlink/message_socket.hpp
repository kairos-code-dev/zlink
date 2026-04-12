/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_MESSAGE_SOCKET_HPP_INCLUDED
#define ZLINK_CPP_MESSAGE_SOCKET_HPP_INCLUDED

#include "base_socket.hpp"
#include "error.hpp"

namespace zlink
{

class message_socket_t : public base_socket_t
{
  public:
    void send (message_t &part_)
    {
        const int rc = base_socket_t::send (part_);
        throw_on_error (rc);
    }

    void send (std::vector<message_t> &parts_)
    {
        const int rc = base_socket_t::send (parts_);
        throw_on_error (rc);
    }

    ZLINK_CPP_NODISCARD received_t recv ()
    {
        received_t received;
        const int rc = base_socket_t::receive (received);
        throw_on_error (rc);
        return received;
    }

    ZLINK_CPP_NODISCARD int on_receive (zlink_socket_msg_handler_fn handler_,
                                        void *userdata_ = NULL)
    {
        return base_socket_t::on_receive (handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD int
    on_send_ready (zlink_send_ready_handler_fn handler_,
                   void *userdata_ = NULL)
    {
        return base_socket_t::on_send_ready (handler_, userdata_);
    }

  protected:
    message_socket_t (context_t &ctx_, socket_type type_) : base_socket_t (ctx_, type_) {}
};

class routed_message_socket_t : public message_socket_t
{
  public:
    using message_socket_t::send;

    void send (const routing_id_t &target_rid_, message_t &part_)
    {
        const int rc = base_socket_t::send (target_rid_, part_);
        throw_on_error (rc);
    }

    void send (const routing_id_t &target_rid_,
               std::vector<message_t> &parts_)
    {
        const int rc = base_socket_t::send (target_rid_, parts_);
        throw_on_error (rc);
    }

  protected:
    routed_message_socket_t (context_t &ctx_, socket_type type_)
        : message_socket_t (ctx_, type_)
    {
    }
};

} // namespace zlink

#endif
