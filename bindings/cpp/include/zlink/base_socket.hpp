/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_BASE_SOCKET_HPP_INCLUDED
#define ZLINK_CPP_BASE_SOCKET_HPP_INCLUDED

#include "monitor.hpp"
#include "service_monitor.hpp"
#include "socket_handle.hpp"

namespace zlink
{

class base_socket_t : public socket_handle_t
{
  public:
    bool valid () const noexcept { return socket_handle_t::valid (); }

    ZLINK_CPP_NODISCARD int bind (const std::string &endpoint_)
    {
        return socket_base ().bind (endpoint_);
    }

    ZLINK_CPP_NODISCARD int connect (const std::string &endpoint_)
    {
        return socket_base ().connect (endpoint_);
    }

    ZLINK_CPP_NODISCARD int unbind (const std::string &endpoint_)
    {
        return socket_base ().unbind (endpoint_);
    }

    ZLINK_CPP_NODISCARD int disconnect (const std::string &endpoint_)
    {
        return socket_base ().disconnect (endpoint_);
    }

    ZLINK_CPP_NODISCARD int attach_discovery (service::discovery_t &discovery_)
    {
        return socket_base ().attach_discovery (discovery_);
    }

    monitor_handle_t
    monitor_handle (monitor_event events_ = monitor_event::all) const
    {
        zlink_socket_monitor_open_options_t options;
        options.events =
          static_cast<zlink_socket_monitor_event_mask_t> (events_);
        return monitor_handle_t (
          zlink_socket_monitor_open (const_cast<void *> (handle ()), &options));
    }

    service_monitor_handle_t
    service_monitor_handle (
      service_monitor_event events_ = service_monitor_event::all) const
    {
        return service_monitor_handle_t (const_cast<void *> (handle ()), events_);
    }

    ZLINK_CPP_NODISCARD int
    set_option (socket_option_key_t<std::string> key_,
                const std::string &value_)
    {
        return socket_base ().set_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_option (socket_option_key_t<T> key_,
                                        const T &value_)
    {
        return socket_base ().set_option (key_, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_option (socket_option_key_t<std::string> key_,
                std::string &value_) const
    {
        return socket_base ().get_option (key_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_option (socket_option_key_t<T> key_,
                                        T *value_) const
    {
        return socket_base ().get_option (key_, value_);
    }

    ZLINK_CPP_NODISCARD int set_tls_server (const std::string &cert_,
                                            const std::string &key_,
                                            bool require_client_cert_ = false)
    {
        return socket_base ().set_tls_server (cert_, key_, require_client_cert_);
    }

    ZLINK_CPP_NODISCARD int set_tls_client (const std::string &ca_cert_,
                                            const std::string &hostname_,
                                            bool trust_system_ = false)
    {
        return socket_base ().set_tls_client (ca_cert_, hostname_, trust_system_);
    }

  protected:
    base_socket_t () noexcept {}

    base_socket_t (context_t &ctx_, socket_type type_)
        : socket_handle_t (
            zlink_socket (ctx_.handle (),
                          static_cast<zlink_socket_type_t> (type_)),
            true)
    {
    }

    explicit base_socket_t (detail::socket_t &&socket_) noexcept
        : socket_handle_t (std::move (socket_))
    {
    }

    ZLINK_CPP_NODISCARD int set_routing_id (const void *data_, size_t size_)
    {
        return socket_base ().set_routing_id (data_, size_);
    }

    ZLINK_CPP_NODISCARD int set_routing_id (const std::string &routing_id_)
    {
        return socket_base ().set_routing_id (routing_id_);
    }

    ZLINK_CPP_NODISCARD int get_routing_id (zlink_routing_id_t &routing_id_) const
    {
        return socket_base ().get_routing_id (routing_id_);
    }

    ZLINK_CPP_NODISCARD int get_routing_id (std::string &routing_id_) const
    {
        return socket_base ().get_routing_id (routing_id_);
    }

    ZLINK_CPP_NODISCARD int
    set_router_option (router_option option_, const void *value_, size_t size_)
    {
        return socket_base ().set_router_option (option_, value_, size_);
    }

    ZLINK_CPP_NODISCARD int
    set_router_option (router_option_key_t<std::string> key_,
                       const std::string &value_)
    {
        return socket_base ().set_router_option (key_.option, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_router_option (router_option option_,
                                               const T &value_)
    {
        return socket_base ().set_router_option (option_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_router_option (router_option_key_t<T> key_,
                                               const T &value_)
    {
        return socket_base ().set_router_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_router_option (router_option option_, void *value_, size_t *size_) const
    {
        return socket_base ().get_router_option (option_, value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int
    get_router_option (router_option option_, T *value_) const
    {
        return socket_base ().get_router_option (option_, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_router_option (router_option option_, std::string &value_) const
    {
        return socket_base ().get_router_option (option_, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_router_option (router_option_key_t<std::string> key_,
                       std::string &value_) const
    {
        return socket_base ().get_router_option (key_.option, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int
    get_router_option (router_option_key_t<T> key_, T *value_) const
    {
        return socket_base ().get_router_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_dealer_option (dealer_option option_, const void *value_, size_t size_)
    {
        return socket_base ().set_dealer_option (option_, value_, size_);
    }

    ZLINK_CPP_NODISCARD int
    set_dealer_option (dealer_option_key_t<std::string> key_,
                       const std::string &value_)
    {
        return socket_base ().set_dealer_option (key_.option, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_dealer_option (dealer_option option_,
                                               const T &value_)
    {
        return socket_base ().set_dealer_option (option_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_dealer_option (dealer_option_key_t<T> key_,
                                               const T &value_)
    {
        return socket_base ().set_dealer_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_pub_option (pub_option option_, const void *value_, size_t size_)
    {
        return socket_base ().set_pub_option (option_, value_, size_);
    }

    ZLINK_CPP_NODISCARD int
    set_pub_option (pub_option_key_t<std::string> key_,
                    const std::string &value_)
    {
        return socket_base ().set_pub_option (key_.option, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_pub_option (pub_option option_,
                                            const T &value_)
    {
        return socket_base ().set_pub_option (option_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_pub_option (pub_option_key_t<T> key_,
                                            const T &value_)
    {
        return socket_base ().set_pub_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_pub_option (pub_option option_, void *value_, size_t *size_) const
    {
        return socket_base ().get_pub_option (option_, value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_pub_option (pub_option option_, T *value_) const
    {
        return socket_base ().get_pub_option (option_, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_pub_option (pub_option option_, std::string &value_) const
    {
        return socket_base ().get_pub_option (option_, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_pub_option (pub_option_key_t<std::string> key_,
                    std::string &value_) const
    {
        return socket_base ().get_pub_option (key_.option, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_pub_option (pub_option_key_t<T> key_,
                                            T *value_) const
    {
        return socket_base ().get_pub_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_sub_option (sub_option option_, const void *value_, size_t size_)
    {
        return socket_base ().set_sub_option (option_, value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_sub_option (sub_option option_,
                                            const T &value_)
    {
        return socket_base ().set_sub_option (option_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_sub_option (sub_option_key_t<T> key_,
                                            const T &value_)
    {
        return socket_base ().set_sub_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_sub_option (sub_option option_, void *value_, size_t *size_) const
    {
        return socket_base ().get_sub_option (option_, value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_sub_option (sub_option option_, T *value_) const
    {
        return socket_base ().get_sub_option (option_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int get_sub_option (sub_option_key_t<T> key_,
                                            T *value_) const
    {
        return socket_base ().get_sub_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    set_stream_option (stream_option option_, const void *value_, size_t size_)
    {
        return socket_base ().set_stream_option (option_, value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_stream_option (stream_option option_,
                                               const T &value_)
    {
        return socket_base ().set_stream_option (option_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int set_stream_option (stream_option_key_t<T> key_,
                                               const T &value_)
    {
        return socket_base ().set_stream_option (key_.option, value_);
    }

    ZLINK_CPP_NODISCARD int
    get_stream_option (stream_option option_, void *value_, size_t *size_) const
    {
        return socket_base ().get_stream_option (option_, value_, size_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int
    get_stream_option (stream_option option_, T *value_) const
    {
        return socket_base ().get_stream_option (option_, value_);
    }

    template<typename T>
    ZLINK_CPP_NODISCARD int
    get_stream_option (stream_option_key_t<T> key_, T *value_) const
    {
        return socket_base ().get_stream_option (key_.option, value_);
    }
};

} // namespace zlink

#endif
