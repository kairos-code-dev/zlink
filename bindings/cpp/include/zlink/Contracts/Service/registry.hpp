/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_REGISTRY_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_REGISTRY_HPP_INCLUDED

#include "../Core/context.hpp"
#include "../Messaging/message.hpp"
#include "../Core/types.hpp"

#include <cerrno>
#include <cstdio>

namespace zlink
{
namespace service
{

class registry_t;

} // namespace service

namespace detail
{
inline void *native_handle (service::registry_t &registry_) noexcept;
inline const void *native_handle (const service::registry_t &registry_) noexcept;
} // namespace detail

namespace service
{

class registry_t
{
  public:
    explicit registry_t (context_t &ctx_)
        : _registry (zlink_registry_new (detail::native_handle (ctx_))), _last_error (0)
    {
        if (!_registry)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    ~registry_t ()
    {
        try {
            close ();
        } catch (...) {
        }
    }

    registry_t (registry_t &&other) noexcept
        : _registry (other._registry), _last_error (other._last_error)
    {
        other._registry = NULL;
        other._last_error = 0;
    }

    registry_t &operator= (registry_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        try {
            close ();
        } catch (...) {
        }
        _registry = other._registry;
        _last_error = other._last_error;
        other._registry = NULL;
        other._last_error = 0;
        return *this;
    }

    registry_t (const registry_t &) = delete;
    registry_t &operator= (const registry_t &) = delete;

    bool valid () const noexcept { return _registry != NULL; }

    void bind (const std::string &pub_endpoint_, const std::string &router_endpoint_)
    {
        zlink::detail::validate_bounded_c_string (pub_endpoint_, 255u, "endpoint");
        zlink::detail::validate_bounded_c_string (router_endpoint_, 255u, "endpoint");
        detail::throw_if_failed<bind_error_t> (
          static_cast<bind_result_t> (zlink_registry_bind (
            _registry, pub_endpoint_.c_str (), router_endpoint_.c_str ())));
    }

    void set_id (uint32_t registry_id_)
    {
        set (ZLINK_REGISTRY_OPT_ID, registry_id_);
    }

    void set (zlink_registry_option_t option_, uint32_t value_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_registry_set (_registry, option_, value_)));
    }

    uint32_t get (zlink_registry_option_t option_) const
    {
        zlink_config_result_t err = ZLINK_CONFIG_OK;
        const uint32_t value = zlink_registry_get (_registry, option_, &err);
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (err));
        return value;
    }

    void add_peer (const std::string &peer_pub_endpoint_)
    {
        zlink::detail::validate_bounded_c_string (peer_pub_endpoint_, 255u, "endpoint");
        detail::throw_if_failed<connect_error_t> (
          static_cast<connect_result_t> (
            zlink_registry_add_peer (_registry, peer_pub_endpoint_.c_str ())));
    }

    void set_heartbeat (uint32_t interval_ms_, uint32_t timeout_ms_)
    {
        set (ZLINK_REGISTRY_OPT_HEARTBEAT_INTERVAL_MS, interval_ms_);
        set (ZLINK_REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS, timeout_ms_);
    }

    void set_heartbeat (std::chrono::milliseconds interval_,
                        std::chrono::milliseconds timeout_)
    {
        set_heartbeat (static_cast<uint32_t> (interval_.count ()),
                       static_cast<uint32_t> (timeout_.count ()));
    }

    void set_broadcast_interval (uint32_t interval_ms_)
    {
        set (ZLINK_REGISTRY_OPT_BROADCAST_INTERVAL_MS, interval_ms_);
    }

    void set_broadcast_interval (std::chrono::milliseconds interval_)
    {
        set_broadcast_interval (static_cast<uint32_t> (interval_.count ()));
    }

    void set_tls_server (const std::string &cert_,
                         const std::string &key_,
                         bool require_client_cert_ = false)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_set_tls_server (
              _registry, cert_.c_str (), key_.c_str (),
              require_client_cert_ ? 1 : 0)));
    }

    void set_tls_client (const std::string &ca_cert_,
                         const std::string &hostname_ = std::string (),
                         bool trust_system_ = false)
    {
        const char *ca = ca_cert_.empty () ? NULL : ca_cert_.c_str ();
        const char *hostname =
          hostname_.empty () ? NULL : hostname_.c_str ();
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_set_tls_client (
              _registry, ca, hostname, trust_system_ ? 1 : 0)));
    }

    registry_status_t status_snapshot () const
    {
        zlink_registry_status_t native;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_registry_status_snapshot (_registry, &native)));
        return registry_status_t (native);
    }

    std::vector<registry_service_summary_entry_t>
    service_summary_snapshot (
      const registry_service_summary_filter_t *filter_ = NULL) const
    {
        zlink_registry_service_summary_filter_t native_filter;
        const zlink_registry_service_summary_filter_t *filter_ptr = NULL;
        if (filter_) {
            std::memset (&native_filter, 0, sizeof (native_filter));
            if (filter_->auto_connect_type ())
                native_filter.auto_connect_type =
                  static_cast<zlink_auto_connect_type_t> (
                    *filter_->auto_connect_type ());
            if (filter_->service_role ())
                native_filter.service_role =
                  static_cast<zlink_service_role_t> (*filter_->service_role ());
            if (filter_->channel_name ())
                std::snprintf (
                  native_filter.channel_name,
                  sizeof (native_filter.channel_name), "%s",
                  filter_->channel_name ()->c_str ());
            filter_ptr = &native_filter;
        }

        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_registry_service_summary_snapshot (
              _registry, filter_ptr, NULL, &count)));
        std::vector<zlink_registry_service_summary_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_registry_service_summary_snapshot (
                  _registry, filter_ptr, native.data (), &count)));
            native.resize (count);
        }
        std::vector<registry_service_summary_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (registry_service_summary_entry_t (native[i]));
        return entries;
    }

    std::vector<registry_service_summary_entry_t>
    service_summary_snapshot (
      const registry_service_summary_filter_t &filter_) const
    {
        return service_summary_snapshot (&filter_);
    }

    std::vector<registry_topology_entry_t> topology_snapshot () const
    {
        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_registry_topology_snapshot (_registry, NULL, &count)));
        std::vector<zlink_registry_topology_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_registry_topology_snapshot (
                  _registry, native.data (), &count)));
            native.resize (count);
        }
        std::vector<registry_topology_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (registry_topology_entry_t (native[i]));
        return entries;
    }

    std::vector<registry_topology_entry_t>
    topology_query (const registry_topology_filter_t &filter_) const
    {
        zlink_registry_topology_filter_t native_filter;
        std::memset (&native_filter, 0, sizeof (native_filter));
        if (filter_.service_kind ())
            native_filter.service_kind =
              static_cast<zlink_service_kind_t> (*filter_.service_kind ());
        if (filter_.service_role ())
            native_filter.service_role =
              static_cast<zlink_service_role_t> (*filter_.service_role ());
        if (filter_.auto_connect_type ())
            native_filter.auto_connect_type =
              static_cast<zlink_auto_connect_type_t> (
                *filter_.auto_connect_type ());
        if (filter_.channel_name ())
            std::snprintf (
              native_filter.channel_name, sizeof (native_filter.channel_name),
              "%s", filter_.channel_name ()->c_str ());
        if (filter_.state ())
            native_filter.state =
              static_cast<zlink_topology_state_t> (*filter_.state ());
        if (filter_.source ())
            native_filter.source =
              static_cast<zlink_topology_source_t> (*filter_.source ());
        if (filter_.routing_id ())
            native_filter.routing_id =
              *zlink::detail::routing_id_native (*filter_.routing_id ());

        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_registry_topology_query (
              _registry, &native_filter, NULL, &count)));
        std::vector<zlink_registry_topology_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_registry_topology_query (
                  _registry, &native_filter, native.data (), &count)));
            native.resize (count);
        }
        std::vector<registry_topology_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (registry_topology_entry_t (native[i]));
        return entries;
    }

    std::vector<member_peer_entry_t>
    member_peers (const std::string &channel_name_) const
    {
        zlink::detail::validate_bounded_c_string (channel_name_, 255u, "channel_name");
        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_registry_member_peers (
              _registry, channel_name_.c_str (), NULL, &count)));
        std::vector<zlink_member_peer_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_registry_member_peers (
                  _registry, channel_name_.c_str (), native.data (), &count)));
            native.resize (count);
        }
        std::vector<member_peer_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (member_peer_entry_t (native[i]));
        return entries;
    }

    void close ()
    {
        if (!_registry)
            return;

        void *tmp = _registry;
        detail::throw_if_failed<close_error_t> (
          static_cast<close_result_t> (zlink_registry_destroy (&tmp)));
        _registry = NULL;
    }

  private:
    friend void *zlink::detail::native_handle (registry_t &registry_) noexcept;
    friend const void *
    zlink::detail::native_handle (const registry_t &registry_) noexcept;

    void *_registry;
    int _last_error;
};

} // namespace service

namespace detail
{
inline void *native_handle (service::registry_t &registry_) noexcept
{
    return registry_._registry;
}

inline const void *native_handle (const service::registry_t &registry_) noexcept
{
    return registry_._registry;
}
} // namespace detail

} // namespace zlink

#endif
