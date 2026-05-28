/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKETS_SUB_HPP_INCLUDED
#define ZLINK_CPP_SOCKETS_SUB_HPP_INCLUDED

#include "../../Runtime/Sockets/detail.hpp"

namespace zlink
{

class sub_socket_t : public subscriber_socket_t
{
  public:
    explicit sub_socket_t (context_t &ctx_)
        : subscriber_socket_t (ctx_, socket_type::sub)
    {
    }

    void set_subscription (const std::string &filter_)
    {
        if (base_socket_t::set_subscription (filter_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    void unset_subscription (const std::string &filter_)
    {
        if (base_socket_t::unset_subscription (filter_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    void subscription_at (size_t index_, std::string &filter_out_,
                          bool *is_pattern_out_ = NULL)
    {
        if (base_socket_t::subscription_at (index_, filter_out_, is_pattern_out_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    subscription_filter_t subscription_at (size_t index_)
    {
        subscription_filter_t filter;
        subscription_at (index_, filter.filter, &filter.is_pattern);
        return filter;
    }

    int subscribe (topic_message_t &out_,
                   recv_flags_t flags_ = recv_flags_t::none)
    {
        return base_socket_t::subscribe (out_, flags_);
    }

    int subscribe_part (std::optional<routing_id_t> &source_rid_out_,
                        std::string &topic_out_,
                        message_t &part_out_,
                        bool &has_more_out_,
                        recv_flags_t flags_ = recv_flags_t::none)
    {
        return base_socket_t::subscribe_part (
          source_rid_out_, topic_out_, part_out_, has_more_out_, flags_);
    }

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    sub_socket_options_t options ()
    {
        return sub_socket_options_t (handle ());
    }

  private:
    using subscriber_socket_t::set_subscription;
    using subscriber_socket_t::subscription_at;
    using subscriber_socket_t::unset_subscription;
};

} // namespace zlink

#endif
