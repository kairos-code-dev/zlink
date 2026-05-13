/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKETS_PUB_HPP_INCLUDED
#define ZLINK_CPP_SOCKETS_PUB_HPP_INCLUDED

#include "detail.hpp"

namespace zlink
{

class pub_socket_t : public publisher_socket_t
{
  public:
    explicit pub_socket_t (context_t &ctx_)
        : publisher_socket_t (ctx_, socket_type::pub)
    {
    }

    service::send_op_t publish (const std::string &topic_id_);

    void on_send_ready (std::function<void()> handler_)
    {
        base_socket_t::on_send_ready (std::move (handler_));
    }

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    pub_socket_options_t options ()
    {
        return pub_socket_options_t (handle ());
    }

  private:
    using publisher_socket_t::on_send_ready;
};

} // namespace zlink

#endif
