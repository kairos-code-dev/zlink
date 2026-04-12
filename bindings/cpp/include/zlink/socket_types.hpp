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

inline send_flag to_legacy_send_flag (send_flags_t flags_) noexcept
{
    return flags_ == send_flags_t::dontwait ? send_flag::dontwait
                                            : send_flag::none;
}

inline recv_flag to_legacy_recv_flag (recv_flags_t flags_) noexcept
{
    return flags_ == recv_flags_t::dontwait ? recv_flag::dontwait
                                            : recv_flag::none;
}

template<typename ErrorT, typename ResultT>
inline void throw_if_failed_result (ResultT result_)
{
    detail::throw_if_failed<ErrorT> (result_);
}

} // namespace detail

class pair_socket_t : public message_socket_t
{
  public:
    explicit pair_socket_t (context_t &ctx_) : message_socket_t (ctx_, socket_type::pair) {}

    void send (message_t &part_, send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (part_, detail::to_legacy_send_flag (flags_)));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    void send (std::vector<message_t> &parts_, send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (parts_, detail::to_legacy_send_flag (flags_)));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    received_t recv (recv_flags_t flags_ = recv_flags_t::none)
    {
        received_t received;
        const recv_result_t rc = static_cast<recv_result_t> (
          base_socket_t::receive (received, detail::to_legacy_recv_flag (flags_)));
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return received;
    }

    void on_receive (zlink_socket_msg_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_receive (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

    void on_send_ready (zlink_send_ready_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_send_ready (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

  private:
    using message_socket_t::recv;
    using message_socket_t::send;
};

class dealer_socket_t : public message_socket_t
{
  public:
    explicit dealer_socket_t (context_t &ctx_)
        : message_socket_t (ctx_, socket_type::dealer)
    {
    }

    void send (message_t &part_, send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (part_, detail::to_legacy_send_flag (flags_)));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    void send (std::vector<message_t> &parts_, send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (parts_, detail::to_legacy_send_flag (flags_)));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    received_t recv (recv_flags_t flags_ = recv_flags_t::none)
    {
        received_t received;
        const recv_result_t rc = static_cast<recv_result_t> (
          base_socket_t::receive (received, detail::to_legacy_recv_flag (flags_)));
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return received;
    }

    void on_receive (zlink_socket_msg_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_receive (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

    void on_send_ready (zlink_send_ready_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_send_ready (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

    void set_routing_id (const routing_id_t &routing_id_)
    {
        if (base_socket_t::set_routing_id_raw (routing_id_.to_string ()) != 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    void get_routing_id (routing_id_t &routing_id_) const
    {
        if (base_socket_t::get_routing_id_raw (routing_id_) != 0)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    }

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    }

  private:
    using message_socket_t::recv;
    using message_socket_t::send;
};

class router_socket_t : public routed_message_socket_t
{
  public:
    using base_socket_t::get_option;
    using base_socket_t::set_option;

    explicit router_socket_t (context_t &ctx_)
        : routed_message_socket_t (ctx_, socket_type::router)
    {
    }

    void send (const routing_id_t &target_rid_, message_t &part_,
               send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (target_rid_, part_,
                                detail::to_legacy_send_flag (flags_)));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    void send (const routing_id_t &target_rid_, std::vector<message_t> &parts_,
               send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (target_rid_, parts_,
                                detail::to_legacy_send_flag (flags_)));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    received_t recv (recv_flags_t flags_ = recv_flags_t::none)
    {
        received_t received;
        const recv_result_t rc = static_cast<recv_result_t> (
          base_socket_t::receive (received, detail::to_legacy_recv_flag (flags_)));
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return received;
    }

    void on_receive (zlink_socket_msg_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_receive (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

    void on_send_ready (zlink_send_ready_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_send_ready (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

    void set_routing_id (const routing_id_t &routing_id_)
    {
        if (base_socket_t::set_routing_id_raw (routing_id_.to_string ()) != 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    void get_routing_id (routing_id_t &routing_id_) const
    {
        if (base_socket_t::get_routing_id_raw (routing_id_) != 0)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    }

    template<typename T>
    void set_option (router_option_key_t<T> key_, const T &value_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_set_router_option (
              handle (), static_cast<zlink_router_option_t> (key_.option),
              &value_, sizeof (value_))));
    }

    void set_option (router_option_key_t<std::string> key_,
                     const std::string &value_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_set_router_option (
              handle (), static_cast<zlink_router_option_t> (key_.option),
              value_.data (), value_.size ())));
    }

    template<typename T>
    void get_option (router_option_key_t<T> key_, T *value_) const
    {
        if (!value_)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
        size_t size = sizeof (T);
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_get_router_option (
              const_cast<void *> (handle ()),
              static_cast<zlink_router_option_t> (key_.option), value_,
              &size)));
    }

    void get_option (router_option_key_t<std::string> key_,
                     std::string *value_) const
    {
        if (!value_)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            detail::get_string_option (
              [](void *handle_, router_option option_, void *value_,
                 size_t *size_) {
                  return zlink_get_router_option (
                    handle_, static_cast<zlink_router_option_t> (option_),
                    value_, size_);
              },
              const_cast<void *> (handle ()), key_.option, 256u, *value_)));
    }

    void recv_spot (recv_flags_t flags_ = recv_flags_t::none)
    {
        (void) flags_;
        throw recv_error_t (recv_result_t::not_supported, zlink_errno ());
    }

    void on_spot_receive (zlink_router_spot_handler_fn handler_, void *userdata_ = NULL)
    {
        (void) handler_;
        (void) userdata_;
        throw handler_error_t (handler_result_t::not_supported, zlink_errno ());
    }

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    }

  private:
    using routed_message_socket_t::recv;
    using routed_message_socket_t::send;
};

class stream_socket_t : public routed_message_socket_t
{
  public:
    using base_socket_t::get_option;
    using base_socket_t::set_option;

    explicit stream_socket_t (context_t &ctx_)
        : routed_message_socket_t (ctx_, socket_type::stream)
    {
    }

    void connect (const std::string &) = delete;

    void send (const routing_id_t &target_rid_, message_t &part_,
               send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (target_rid_, part_,
                                detail::to_legacy_send_flag (flags_)));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    void send (const routing_id_t &target_rid_, std::vector<message_t> &parts_,
               send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::send (target_rid_, parts_,
                                detail::to_legacy_send_flag (flags_)));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    received_t recv (recv_flags_t flags_ = recv_flags_t::none)
    {
        received_t received;
        const recv_result_t rc = static_cast<recv_result_t> (
          base_socket_t::receive (received, detail::to_legacy_recv_flag (flags_)));
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return received;
    }

    void on_receive (zlink_socket_msg_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_receive (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

    void on_send_ready (zlink_send_ready_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_send_ready (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

    void set_routing_id (const routing_id_t &routing_id_)
    {
        if (base_socket_t::set_routing_id_raw (routing_id_.to_string ()) != 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    void get_routing_id (routing_id_t &routing_id_) const
    {
        if (base_socket_t::get_routing_id_raw (routing_id_) != 0)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    }

    template<typename T>
    void set_option (stream_option_key_t<T> key_, const T &value_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_set_stream_option (
              handle (), static_cast<zlink_stream_option_t> (key_.option),
              &value_, sizeof (value_))));
    }

    void set_option (stream_option_key_t<std::string> key_,
                     const std::string &value_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_set_stream_option (
              handle (), static_cast<zlink_stream_option_t> (key_.option),
              value_.data (), value_.size ())));
    }

    template<typename T>
    void get_option (stream_option_key_t<T> key_, T *value_) const
    {
        if (!value_)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
        size_t size = sizeof (T);
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_get_stream_option (
              const_cast<void *> (handle ()),
              static_cast<zlink_stream_option_t> (key_.option), value_,
              &size)));
    }

    void get_option (stream_option_key_t<std::string> key_,
                     std::string *value_) const
    {
        if (!value_)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            detail::get_string_option (
              [](void *handle_, stream_option option_, void *value_,
                 size_t *size_) {
                  return zlink_get_stream_option (
                    handle_, static_cast<zlink_stream_option_t> (option_),
                    value_, size_);
              },
              const_cast<void *> (handle ()), key_.option, 256u, *value_)));
    }

  private:
    using routed_message_socket_t::recv;
    using routed_message_socket_t::send;
    using base_socket_t::connect;
};

class pub_socket_t : public publisher_socket_t
{
  public:
    explicit pub_socket_t (context_t &ctx_)
        : publisher_socket_t (ctx_, socket_type::pub)
    {
    }

    void publish (const std::string &topic_id_, message_t &part_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::publish (topic_id_, part_,
                                  detail::to_legacy_send_flag (flags_)));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    void publish (const std::string &topic_id_, std::vector<message_t> &parts_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::publish (topic_id_, parts_,
                                  detail::to_legacy_send_flag (flags_)));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    void on_send_ready (zlink_send_ready_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_send_ready (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    }

  private:
    using publisher_socket_t::on_send_ready;
    using publisher_socket_t::publish;
};

class xpub_socket_t : public publisher_socket_t
{
  public:
    explicit xpub_socket_t (context_t &ctx_)
        : publisher_socket_t (ctx_, socket_type::xpub)
    {
    }

    void publish (const std::string &topic_id_, message_t &part_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::publish (topic_id_, part_,
                                  detail::to_legacy_send_flag (flags_)));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    void publish (const std::string &topic_id_, std::vector<message_t> &parts_,
                  send_flags_t flags_ = send_flags_t::none)
    {
        const submit_result_t rc = static_cast<submit_result_t> (
          base_socket_t::publish (topic_id_, parts_,
                                  detail::to_legacy_send_flag (flags_)));
        if (rc != submit_result_t::ok)
            throw submit_error_t (rc, zlink_errno ());
    }

    void on_send_ready (zlink_send_ready_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_send_ready (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

    subscription_event_t receive_subscription_event (
      recv_flags_t flags_ = recv_flags_t::none)
    {
        subscription_event_t event;
        const recv_result_t rc = static_cast<recv_result_t> (
          base_socket_t::subscription_event (event, detail::to_legacy_recv_flag (flags_)));
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return event;
    }

  private:
    using publisher_socket_t::on_send_ready;
    using publisher_socket_t::publish;
};

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
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    void unset_subscription (const std::string &filter_)
    {
        if (base_socket_t::unset_subscription (filter_) != 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    void subscription_at (size_t index_, std::string &filter_out_,
                          bool *is_pattern_out_ = NULL)
    {
        if (base_socket_t::subscription_at (index_, filter_out_, is_pattern_out_) != 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    subscribed_t subscribe (recv_flags_t flags_ = recv_flags_t::none)
    {
        subscribed_t subscribed;
        const recv_result_t rc = static_cast<recv_result_t> (
          base_socket_t::subscribe (subscribed, detail::to_legacy_recv_flag (flags_)));
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return subscribed;
    }

    void on_subscribe (zlink_subscribe_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_subscribe (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    }

  private:
    using subscriber_socket_t::on_subscribe;
    using subscriber_socket_t::set_subscription;
    using subscriber_socket_t::subscribe;
    using subscriber_socket_t::subscription_at;
    using subscriber_socket_t::unset_subscription;
};

class xsub_socket_t : public subscriber_socket_t
{
  public:
    explicit xsub_socket_t (context_t &ctx_)
        : subscriber_socket_t (ctx_, socket_type::xsub)
    {
    }

    void set_subscription (const std::string &filter_)
    {
        if (base_socket_t::set_subscription (filter_) != 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    void unset_subscription (const std::string &filter_)
    {
        if (base_socket_t::unset_subscription (filter_) != 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    void subscription_at (size_t index_, std::string &filter_out_,
                          bool *is_pattern_out_ = NULL)
    {
        if (base_socket_t::subscription_at (index_, filter_out_, is_pattern_out_) != 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
    }

    subscribed_t subscribe (recv_flags_t flags_ = recv_flags_t::none)
    {
        subscribed_t subscribed;
        const recv_result_t rc = static_cast<recv_result_t> (
          base_socket_t::subscribe (subscribed, detail::to_legacy_recv_flag (flags_)));
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        return subscribed;
    }

    void on_subscribe (zlink_subscribe_handler_fn handler_, void *userdata_ = NULL)
    {
        if (base_socket_t::on_subscribe (handler_, userdata_) != 0)
            throw handler_error_t (handler_result_t::invalid_handle, zlink_errno ());
    }

  private:
    using subscriber_socket_t::on_subscribe;
    using subscriber_socket_t::set_subscription;
    using subscriber_socket_t::subscribe;
    using subscriber_socket_t::subscription_at;
    using subscriber_socket_t::unset_subscription;
};

} // namespace zlink

#endif
