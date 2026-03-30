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
        const int rc = socket_base ().send (part_);
        throw_on_error (rc);
    }

    void send (std::vector<message_t> &parts_)
    {
        const int rc = socket_base ().send (parts_);
        throw_on_error (rc);
    }

    ZLINK_CPP_NODISCARD send_result_t try_send (message_t &part_)
    {
        send_result_t result = send_result_t::sent;
        const int rc = socket_base ().try_send (result, part_);
        throw_on_error (rc);
        return result;
    }

    ZLINK_CPP_NODISCARD send_result_t try_send (std::vector<message_t> &parts_)
    {
        send_result_t result = send_result_t::sent;
        const int rc = socket_base ().try_send (result, parts_);
        throw_on_error (rc);
        return result;
    }

    ZLINK_CPP_NODISCARD received_t receive ()
    {
        received_t received;
        const int rc = socket_base ().receive (received);
        throw_on_error (rc);
        return received;
    }

    ZLINK_CPP_NODISCARD maybe_t<received_t> try_receive ()
    {
        received_t received;
        const int rc = socket_base ().receive (received, recv_flag::dontwait);
        if (rc == 0)
            return maybe_t<received_t> (std::move (received));
        if (errno == EAGAIN)
            return maybe_t<received_t> ();
        throw_on_error (rc);
        return maybe_t<received_t> ();
    }

    ZLINK_CPP_NODISCARD int recv_handler (zlink_socket_msg_handler_fn handler_,
                                          void *userdata_ = NULL)
    {
        return socket_base ().recv_handler (handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD int
    send_ready_handler (zlink_send_ready_handler_fn handler_,
                        void *userdata_ = NULL)
    {
        return socket_base ().send_ready_handler (handler_, userdata_);
    }

  protected:
    message_socket_t (context_t &ctx_, socket_type type_) : base_socket_t (ctx_, type_) {}
};

class routed_message_socket_t : public message_socket_t
{
  public:
    using message_socket_t::send;

    void send (const zlink_routing_id_t &target_rid_, message_t &part_)
    {
        const int rc = socket_base ().send (target_rid_, part_);
        throw_on_error (rc);
    }

    void send (const zlink_routing_id_t &target_rid_,
               std::vector<message_t> &parts_)
    {
        const int rc = socket_base ().send (target_rid_, parts_);
        throw_on_error (rc);
    }

    ZLINK_CPP_NODISCARD send_result_t
    try_send (const zlink_routing_id_t &target_rid_, message_t &part_)
    {
        send_result_t result = send_result_t::sent;
        const int rc = socket_base ().try_send (result, target_rid_, part_);
        throw_on_error (rc);
        return result;
    }

    ZLINK_CPP_NODISCARD send_result_t
    try_send (const zlink_routing_id_t &target_rid_,
              std::vector<message_t> &parts_)
    {
        send_result_t result = send_result_t::sent;
        const int rc = socket_base ().try_send (result, target_rid_, parts_);
        throw_on_error (rc);
        return result;
    }

  protected:
    routed_message_socket_t (context_t &ctx_, socket_type type_)
        : message_socket_t (ctx_, type_)
    {
    }
};

} // namespace zlink

#endif
