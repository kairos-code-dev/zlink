/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKET_TYPES_HPP_INCLUDED
#define ZLINK_CPP_SOCKET_TYPES_HPP_INCLUDED

#include "message_socket.hpp"
#include "publisher_socket.hpp"
#include "subscriber_socket.hpp"
#include "error.hpp"

namespace zlink
{

namespace detail
{

class pub_option_socket_base_t : public publisher_socket_t
{
  public:
    ZLINK_CPP_NODISCARD int
    set_option (socket_option_key_t<std::string> key_,
                const std::string &value_)
    {
        return base_socket_t::set_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_option (socket_option_key_t<T> key_,
                                        const T &value_)
    {
        return base_socket_t::set_option (key_, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_option (socket_option_key_t<std::string> key_, std::string &value_) const
    {
        return base_socket_t::get_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_option (socket_option_key_t<T> key_,
                                        T *value_) const
    {
        return base_socket_t::get_option (key_, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_option (pub_option_key_t<std::string> key_, const std::string &value_)
    {
        return set_pub_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_option (pub_option_key_t<T> key_,
                                        const T &value_)
    {
        return set_pub_option (key_, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_option (pub_option_key_t<std::string> key_, std::string &value_) const
    {
        return get_pub_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_option (pub_option_key_t<T> key_,
                                        T *value_) const
    {
        return get_pub_option (key_, value_);
    }

  protected:
    pub_option_socket_base_t (context_t &ctx_, socket_type type_)
        : publisher_socket_t (ctx_, type_)
    {
    }
};

class sub_option_socket_base_t : public subscriber_socket_t
{
  public:
    ZLINK_CPP_NODISCARD int
    set_option (socket_option_key_t<std::string> key_,
                const std::string &value_)
    {
        return base_socket_t::set_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_option (socket_option_key_t<T> key_,
                                        const T &value_)
    {
        return base_socket_t::set_option (key_, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_option (socket_option_key_t<std::string> key_, std::string &value_) const
    {
        return base_socket_t::get_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_option (socket_option_key_t<T> key_,
                                        T *value_) const
    {
        return base_socket_t::get_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_option (sub_option_key_t<T> key_,
                                        const T &value_)
    {
        return set_sub_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_option (sub_option_key_t<T> key_,
                                        T *value_) const
    {
        return get_sub_option (key_, value_);
    }

  protected:
    sub_option_socket_base_t (context_t &ctx_, socket_type type_)
        : subscriber_socket_t (ctx_, type_)
    {
    }
};

} // namespace detail

class pair_socket_t : public message_socket_t
{
  public:
    explicit pair_socket_t (context_t &ctx_) : message_socket_t (ctx_, socket_type::pair) {}
};

class dealer_socket_t : public message_socket_t
{
  public:
    explicit dealer_socket_t (context_t &ctx_)
        : message_socket_t (ctx_, socket_type::dealer)
    {
    }

    ZLINK_CPP_NODISCARD int
    set_option (socket_option_key_t<std::string> key_,
                const std::string &value_)
    {
        return base_socket_t::set_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_option (socket_option_key_t<T> key_,
                                        const T &value_)
    {
        return base_socket_t::set_option (key_, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_option (socket_option_key_t<std::string> key_, std::string &value_) const
    {
        return base_socket_t::get_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_option (socket_option_key_t<T> key_,
                                        T *value_) const
    {
        return base_socket_t::get_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_option (dealer_option_key_t<T> key_,
                                        const T &value_)
    {
        return set_dealer_option (key_, value_);
    }

    template<typename DiscoveryT>
    ZLINK_CPP_NODISCARD int attach_discovery (DiscoveryT &discovery_)
    {
        return base_socket_t::attach_discovery (discovery_);
    }

    ZLINK_CPP_NODISCARD int set_routing_id (const routing_id_t &routing_id_)
    {
        return base_socket_t::set_routing_id_raw (
          routing_id_.to_string ());
    }

    ZLINK_CPP_NODISCARD int get_routing_id (routing_id_t &routing_id_) const
    {
        return base_socket_t::get_routing_id_raw (routing_id_);
    }
};

class router_socket_t : public routed_message_socket_t
{
  public:
    explicit router_socket_t (context_t &ctx_)
        : routed_message_socket_t (ctx_, socket_type::router)
    {
    }

    ZLINK_CPP_NODISCARD int
    set_option (socket_option_key_t<std::string> key_,
                const std::string &value_)
    {
        return base_socket_t::set_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_option (socket_option_key_t<T> key_,
                                        const T &value_)
    {
        return base_socket_t::set_option (key_, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_option (socket_option_key_t<std::string> key_, std::string &value_) const
    {
        return base_socket_t::get_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_option (socket_option_key_t<T> key_,
                                        T *value_) const
    {
        return base_socket_t::get_option (key_, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_option (router_option_key_t<std::string> key_, const std::string &value_)
    {
        return set_router_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_option (router_option_key_t<T> key_,
                                        const T &value_)
    {
        return set_router_option (key_, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_option (router_option_key_t<std::string> key_, std::string &value_) const
    {
        return get_router_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_option (router_option_key_t<T> key_,
                                        T *value_) const
    {
        return get_router_option (key_, value_);
    }

    template<typename DiscoveryT>
    ZLINK_CPP_NODISCARD int attach_discovery (DiscoveryT &discovery_)
    {
        return base_socket_t::attach_discovery (discovery_);
    }

    ZLINK_CPP_NODISCARD int set_routing_id (const routing_id_t &routing_id_)
    {
        return base_socket_t::set_routing_id_raw (
          routing_id_.to_string ());
    }

    ZLINK_CPP_NODISCARD int get_routing_id (routing_id_t &routing_id_) const
    {
        return base_socket_t::get_routing_id_raw (routing_id_);
    }
};

class stream_socket_t : public routed_message_socket_t
{
  public:
    explicit stream_socket_t (context_t &ctx_)
        : routed_message_socket_t (ctx_, socket_type::stream)
    {
    }

    int connect (const std::string &) = delete;

    ZLINK_CPP_NODISCARD int
    set_option (socket_option_key_t<std::string> key_,
                const std::string &value_)
    {
        return base_socket_t::set_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_option (socket_option_key_t<T> key_,
                                        const T &value_)
    {
        return base_socket_t::set_option (key_, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_option (socket_option_key_t<std::string> key_, std::string &value_) const
    {
        return base_socket_t::get_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_option (socket_option_key_t<T> key_,
                                        T *value_) const
    {
        return base_socket_t::get_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_option (stream_option_key_t<T> key_,
                                        const T &value_)
    {
        return set_stream_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_option (stream_option_key_t<T> key_,
                                        T *value_) const
    {
        return get_stream_option (key_, value_);
    }

    ZLINK_CPP_NODISCARD int set_routing_id (const routing_id_t &routing_id_)
    {
        return base_socket_t::set_routing_id_raw (
          routing_id_.to_string ());
    }

    ZLINK_CPP_NODISCARD int get_routing_id (routing_id_t &routing_id_) const
    {
        return base_socket_t::get_routing_id_raw (routing_id_);
    }
};

class pub_socket_t : public detail::pub_option_socket_base_t
{
  public:
    explicit pub_socket_t (context_t &ctx_)
        : detail::pub_option_socket_base_t (ctx_, socket_type::pub)
    {
    }

    template<typename DiscoveryT>
    ZLINK_CPP_NODISCARD int attach_discovery (DiscoveryT &discovery_)
    {
        return base_socket_t::attach_discovery (discovery_);
    }
};

class xpub_socket_t : public detail::pub_option_socket_base_t
{
  public:
    explicit xpub_socket_t (context_t &ctx_)
        : detail::pub_option_socket_base_t (ctx_, socket_type::xpub)
    {
    }

    ZLINK_CPP_NODISCARD subscription_event_t receive_subscription_event ()
    {
        subscription_event_t event;
        const int rc = base_socket_t::subscription_event (event);
        throw_on_error (rc);
        return event;
    }

    ZLINK_CPP_NODISCARD maybe_t<subscription_event_t>
    try_receive_subscription_event ()
    {
        subscription_event_t event;
        const int rc =
          base_socket_t::subscription_event (event, recv_flag::dontwait);
        if (rc == 0)
            return maybe_t<subscription_event_t> (std::move (event));
        if (errno == EAGAIN)
            return maybe_t<subscription_event_t> ();
        throw_on_error (rc);
        return maybe_t<subscription_event_t> ();
    }
};

class sub_socket_t : public detail::sub_option_socket_base_t
{
  public:
    explicit sub_socket_t (context_t &ctx_)
        : detail::sub_option_socket_base_t (ctx_, socket_type::sub)
    {
    }

    template<typename DiscoveryT>
    ZLINK_CPP_NODISCARD int attach_discovery (DiscoveryT &discovery_)
    {
        return base_socket_t::attach_discovery (discovery_);
    }
};

class xsub_socket_t : public detail::sub_option_socket_base_t
{
  public:
    explicit xsub_socket_t (context_t &ctx_)
        : detail::sub_option_socket_base_t (ctx_, socket_type::xsub)
    {
    }
};

} // namespace zlink

#endif
