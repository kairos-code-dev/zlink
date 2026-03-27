/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SUBSCRIBER_SOCKET_HPP_INCLUDED
#define ZLINK_CPP_SUBSCRIBER_SOCKET_HPP_INCLUDED

#include "base_socket.hpp"

namespace zlink
{

class subscriber_socket_t : public base_socket_t
{
  public:
    ZLINK_CPP_NODISCARD int set_subscription (const std::string &filter_)
    {
        return socket_base ().set_subscription (filter_);
    }

    ZLINK_CPP_NODISCARD int unset_subscription (const std::string &filter_)
    {
        return socket_base ().unset_subscription (filter_);
    }

    ZLINK_CPP_NODISCARD int subscription_at (size_t index_,
                                             std::string &filter_,
                                             bool *is_pattern_ = NULL)
    {
        return socket_base ().subscription_at (index_, filter_, is_pattern_);
    }

    ZLINK_CPP_NODISCARD int recv (std::string &topic_id_out_,
                                  message_t &part_,
                                  recv_flag flags_ = recv_flag::none)
    {
        return socket_base ().subscribe (topic_id_out_, part_, flags_);
    }

    ZLINK_CPP_NODISCARD int recv (std::string &topic_id_out_,
                                  std::vector<message_t> &parts_out_,
                                  recv_flag flags_ = recv_flag::none)
    {
        return socket_base ().subscribe (topic_id_out_, parts_out_, flags_);
    }

    ZLINK_CPP_NODISCARD int recv (zlink_routing_id_t &source_rid_out_,
                                  std::string &topic_id_out_,
                                  message_t &part_,
                                  recv_flag flags_ = recv_flag::none)
    {
        return socket_base ().subscribe (
          source_rid_out_, topic_id_out_, part_, flags_);
    }

    ZLINK_CPP_NODISCARD int
    recv (zlink_routing_id_t &source_rid_out_,
          std::string &topic_id_out_,
          std::vector<message_t> &parts_out_,
          recv_flag flags_ = recv_flag::none)
    {
        return socket_base ().subscribe (
          source_rid_out_, topic_id_out_, parts_out_, flags_);
    }

  protected:
    subscriber_socket_t (context_t &ctx_, socket_type type_) : base_socket_t (ctx_, type_) {}
};

} // namespace zlink

#endif
