/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_PUBLISHER_SOCKET_HPP_INCLUDED
#define ZLINK_CPP_PUBLISHER_SOCKET_HPP_INCLUDED

#include "base_socket.hpp"

namespace zlink
{

class publisher_socket_t : public base_socket_t
{
  public:
    ZLINK_CPP_NODISCARD int publish (const std::string &topic_id_,
                                     message_t &part_,
                                     send_flag flags_ = send_flag::none)
    {
        return socket_base ().publish (topic_id_, part_, flags_);
    }

    ZLINK_CPP_NODISCARD int publish (const std::string &topic_id_,
                                     std::vector<message_t> &parts_,
                                     send_flag flags_ = send_flag::none)
    {
        return socket_base ().publish (topic_id_, parts_, flags_);
    }

  protected:
    publisher_socket_t (context_t &ctx_, socket_type type_) : base_socket_t (ctx_, type_) {}
};

} // namespace zlink

#endif
