/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_MESSAGE_SOCKET_HPP_INCLUDED
#define ZLINK_CPP_MESSAGE_SOCKET_HPP_INCLUDED

#include "base_socket.hpp"

namespace zlink
{

class message_socket_t : public base_socket_t
{
  public:
    ZLINK_CPP_NODISCARD int send (message_t &part_,
                                  send_flag flags_ = send_flag::none)
    {
        return socket_base ().send (part_, flags_);
    }

    ZLINK_CPP_NODISCARD int send (std::vector<message_t> &parts_,
                                  send_flag flags_ = send_flag::none)
    {
        return socket_base ().send (parts_, flags_);
    }

    ZLINK_CPP_NODISCARD int send (const zlink_routing_id_t &target_rid_,
                                  message_t &part_,
                                  send_flag flags_ = send_flag::none)
    {
        return socket_base ().send (target_rid_, part_, flags_);
    }

    ZLINK_CPP_NODISCARD int
    send (const zlink_routing_id_t &target_rid_,
          std::vector<message_t> &parts_,
          send_flag flags_ = send_flag::none)
    {
        return socket_base ().send (target_rid_, parts_, flags_);
    }

    ZLINK_CPP_NODISCARD int recv (message_t &part_,
                                  recv_flag flags_ = recv_flag::none)
    {
        return socket_base ().recv (part_, flags_);
    }

    ZLINK_CPP_NODISCARD int recv (std::vector<message_t> &parts_,
                                  recv_flag flags_ = recv_flag::none)
    {
        return socket_base ().recv (parts_, flags_);
    }

    ZLINK_CPP_NODISCARD int recv (zlink_routing_id_t &source_rid_,
                                  message_t &part_,
                                  recv_flag flags_ = recv_flag::none)
    {
        return socket_base ().recv (source_rid_, part_, flags_);
    }

    ZLINK_CPP_NODISCARD int
    recv (zlink_routing_id_t &source_rid_,
          std::vector<message_t> &parts_,
          recv_flag flags_ = recv_flag::none)
    {
        return socket_base ().recv (source_rid_, parts_, flags_);
    }

  protected:
    message_socket_t (context_t &ctx_, socket_type type_) : base_socket_t (ctx_, type_) {}
};

} // namespace zlink

#endif
