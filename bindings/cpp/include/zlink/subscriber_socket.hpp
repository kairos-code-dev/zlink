/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SUBSCRIBER_SOCKET_HPP_INCLUDED
#define ZLINK_CPP_SUBSCRIBER_SOCKET_HPP_INCLUDED

#include "base_socket.hpp"
#include "error.hpp"

namespace zlink
{

class subscriber_socket_t : public base_socket_t
{
  public:
    ZLINK_CPP_NODISCARD int set_subscription (const std::string &filter_)
    {
        return base_socket_t::set_subscription (filter_);
    }

    ZLINK_CPP_NODISCARD int unset_subscription (const std::string &filter_)
    {
        return base_socket_t::unset_subscription (filter_);
    }

    ZLINK_CPP_NODISCARD int subscription_at (size_t index_,
                                             std::string &filter_,
                                             bool *is_pattern_ = NULL)
    {
        return base_socket_t::subscription_at (index_, filter_, is_pattern_);
    }

    ZLINK_CPP_NODISCARD topic_message_t subscribe ()
    {
        topic_message_t message;
        const int rc = base_socket_t::subscribe (message);
        throw_on_error (rc);
        return message;
    }

    ZLINK_CPP_NODISCARD int
    on_subscribe (zlink_subscribe_handler_fn handler_,
                  void *userdata_ = NULL)
    {
        return base_socket_t::on_subscribe (handler_, userdata_);
    }

  protected:
    subscriber_socket_t (context_t &ctx_, socket_type type_) : base_socket_t (ctx_, type_) {}
};

} // namespace zlink

#endif
