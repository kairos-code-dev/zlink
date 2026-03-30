/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_PUBLISHER_SOCKET_HPP_INCLUDED
#define ZLINK_CPP_PUBLISHER_SOCKET_HPP_INCLUDED

#include "base_socket.hpp"
#include "error.hpp"

namespace zlink
{

class publisher_socket_t : public base_socket_t
{
  public:
    void publish (const std::string &topic_id_, message_t &part_)
    {
        const int rc = socket_base ().publish (topic_id_, part_);
        throw_on_error (rc);
    }

    void publish (const std::string &topic_id_, std::vector<message_t> &parts_)
    {
        const int rc = socket_base ().publish (topic_id_, parts_);
        throw_on_error (rc);
    }

    ZLINK_CPP_NODISCARD send_result_t try_publish (const std::string &topic_id_,
                                                   message_t &part_)
    {
        send_result_t result = send_result_t::sent;
        const int rc = socket_base ().try_publish (result, topic_id_, part_);
        throw_on_error (rc);
        return result;
    }

    ZLINK_CPP_NODISCARD send_result_t
    try_publish (const std::string &topic_id_, std::vector<message_t> &parts_)
    {
        send_result_t result = send_result_t::sent;
        const int rc = socket_base ().try_publish (result, topic_id_, parts_);
        throw_on_error (rc);
        return result;
    }

    ZLINK_CPP_NODISCARD int
    send_ready_handler (zlink_send_ready_handler_fn handler_,
                        void *userdata_ = NULL)
    {
        return socket_base ().send_ready_handler (handler_, userdata_);
    }

  protected:
    publisher_socket_t (context_t &ctx_, socket_type type_) : base_socket_t (ctx_, type_) {}
};

} // namespace zlink

#endif
