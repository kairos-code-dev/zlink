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

    ZLINK_CPP_NODISCARD subscribed_t subscribe ()
    {
        subscribed_t subscribed;
        const int rc = base_socket_t::subscribe (subscribed);
        throw_on_error (rc);
        return subscribed;
    }

    ZLINK_CPP_NODISCARD maybe_t<subscribed_t> try_subscribe ()
    {
        subscribed_t subscribed;
        const int rc = base_socket_t::subscribe (subscribed, recv_flag::dontwait);
        if (rc == 0)
            return maybe_t<subscribed_t> (std::move (subscribed));
        if (errno == EAGAIN)
            return maybe_t<subscribed_t> ();
        throw_on_error (rc);
        return maybe_t<subscribed_t> ();
    }

    ZLINK_CPP_NODISCARD int
    subscribe_handler (zlink_subscribe_handler_fn handler_,
                       void *userdata_ = NULL)
    {
        return base_socket_t::subscribe_handler (handler_, userdata_);
    }

  protected:
    subscriber_socket_t (context_t &ctx_, socket_type type_) : base_socket_t (ctx_, type_) {}
};

} // namespace zlink

#endif
