/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKET_TYPES_HPP_INCLUDED
#define ZLINK_CPP_SOCKET_TYPES_HPP_INCLUDED

#include "message_socket.hpp"
#include "publisher_socket.hpp"
#include "subscriber_socket.hpp"

namespace zlink
{

namespace detail
{

class pub_option_socket_base_t : public publisher_socket_t
{
  public:
    using base_socket_t::get_option;
    using base_socket_t::set_option;

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
    using base_socket_t::get_option;
    using base_socket_t::set_option;

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
    using base_socket_t::get_option;
    using base_socket_t::set_option;

    explicit dealer_socket_t (context_t &ctx_)
        : message_socket_t (ctx_, socket_type::dealer)
    {
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_option (dealer_option_key_t<T> key_,
                                        const T &value_)
    {
        return set_dealer_option (key_, value_);
    }
};

class router_socket_t : public message_socket_t
{
  public:
    using base_socket_t::get_option;
    using base_socket_t::set_option;

    explicit router_socket_t (context_t &ctx_)
        : message_socket_t (ctx_, socket_type::router)
    {
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
};

class stream_socket_t : public message_socket_t
{
  public:
    using base_socket_t::get_option;
    using base_socket_t::set_option;

    explicit stream_socket_t (context_t &ctx_)
        : message_socket_t (ctx_, socket_type::stream)
    {
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
};

class pub_socket_t : public detail::pub_option_socket_base_t
{
  public:
    explicit pub_socket_t (context_t &ctx_)
        : detail::pub_option_socket_base_t (ctx_, socket_type::pub)
    {
    }
};

class xpub_socket_t : public detail::pub_option_socket_base_t
{
  public:
    explicit xpub_socket_t (context_t &ctx_)
        : detail::pub_option_socket_base_t (ctx_, socket_type::xpub)
    {
    }

    ZLINK_CPP_NODISCARD int subscription_event (bool &subscribed_out_,
                                                std::string &topic_id_out_,
                                                recv_flag flags_ = recv_flag::none)
    {
        return socket_base ().subscription_event (
          subscribed_out_, topic_id_out_, flags_);
    }

    ZLINK_CPP_NODISCARD int
    subscription_event (zlink_routing_id_t &source_rid_out_,
                        bool &subscribed_out_,
                        std::string &topic_id_out_,
                        recv_flag flags_ = recv_flag::none)
    {
        return socket_base ().subscription_event (
          source_rid_out_, subscribed_out_, topic_id_out_, flags_);
    }
};

class sub_socket_t : public detail::sub_option_socket_base_t
{
  public:
    explicit sub_socket_t (context_t &ctx_)
        : detail::sub_option_socket_base_t (ctx_, socket_type::sub)
    {
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
