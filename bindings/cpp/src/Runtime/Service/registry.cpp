/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Service/registry.hpp>
#include <Runtime/Core/context_access.hpp>
#include <Runtime/Core/duration_conversion.hpp>
#include <Runtime/Service/registry_access.hpp>
#include <Runtime/Service/service_model_access.hpp>

#include <zlink.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace zlink::service
{

struct registry_t::impl
{
    void *handle = nullptr;
};

} // namespace zlink::service

namespace zlink::detail
{

void *registry_access_t::native_handle (service::registry_t &registry_) noexcept
{
    return registry_._impl ? registry_._impl->handle : nullptr;
}

const void *registry_access_t::native_handle (
  const service::registry_t &registry_) noexcept
{
    return registry_._impl ? registry_._impl->handle : nullptr;
}

} // namespace zlink::detail

namespace zlink::service
{

registry_t::registry_t (context_t &ctx_)
    : _impl (std::make_unique<impl> ()), _last_error (0)
{
    _impl->handle = zlink_registry_new (zlink::detail::native_handle (ctx_));
    if (!_impl->handle)
        _last_error = errno != 0 ? errno : EFAULT;
}

registry_t::~registry_t ()
{
    try {
        close ();
    } catch (...) {
    }
}

registry_t::registry_t (registry_t &&other) noexcept
    : _impl (std::move (other._impl)), _last_error (other._last_error)
{
    if (!other._impl)
        other._impl = std::make_unique<impl> ();
    other._last_error = 0;
}

registry_t &registry_t::operator= (registry_t &&other) noexcept
{
    if (this == &other)
        return *this;

    try {
        close ();
    } catch (...) {
    }
    _impl = std::move (other._impl);
    _last_error = other._last_error;
    if (!other._impl)
        other._impl = std::make_unique<impl> ();
    other._last_error = 0;
    return *this;
}

bool registry_t::valid () const noexcept
{
    return _impl && _impl->handle != nullptr;
}

void registry_t::bind (
  const std::string &pub_endpoint_, const std::string &router_endpoint_)
{
    zlink::detail::validate_bounded_c_string (
      pub_endpoint_, 255u, "endpoint");
    zlink::detail::validate_bounded_c_string (
      router_endpoint_, 255u, "endpoint");
    detail::throw_if_failed<bind_error_t> (
      static_cast<bind_result_t> (zlink_registry_bind (
        _impl->handle, pub_endpoint_.c_str (), router_endpoint_.c_str ())));
}

void registry_t::set (int option_, uint32_t value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_registry_set (
          _impl->handle, static_cast<zlink_registry_option_t> (option_),
          value_)));
}

uint32_t registry_t::get (int option_) const
{
    zlink_config_result_t err = static_cast<zlink_config_result_t> (0);
    const uint32_t value = zlink_registry_get (
      _impl->handle, static_cast<zlink_registry_option_t> (option_), &err);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (err));
    return value;
}

void registry_t::set_heartbeat (std::chrono::milliseconds interval_,
                                std::chrono::milliseconds timeout_)
{
    set_heartbeat (zlink::detail::native_timeout_ms (interval_),
                   zlink::detail::native_timeout_ms (timeout_));
}

void registry_t::set_broadcast_interval (std::chrono::milliseconds interval_)
{
    set_broadcast_interval (zlink::detail::native_timeout_ms (interval_));
}

void registry_t::add_peer (const std::string &peer_pub_endpoint_)
{
    zlink::detail::validate_bounded_c_string (
      peer_pub_endpoint_, 255u, "endpoint");
    detail::throw_if_failed<connect_error_t> (
      static_cast<connect_result_t> (
        zlink_registry_add_peer (_impl->handle, peer_pub_endpoint_.c_str ())));
}

void registry_t::set_tls_server (
  const std::string &cert_, const std::string &key_,
  bool require_client_cert_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_set_tls_server (
          _impl->handle, cert_.c_str (), key_.c_str (),
          require_client_cert_ ? 1 : 0)));
}

void registry_t::set_tls_client (
  const std::string &ca_cert_, const std::string &hostname_,
  bool trust_system_)
{
    const char *ca = ca_cert_.empty () ? nullptr : ca_cert_.c_str ();
    const char *hostname = hostname_.empty () ? nullptr : hostname_.c_str ();
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_set_tls_client (
          _impl->handle, ca, hostname, trust_system_ ? 1 : 0)));
}

registry_status_t registry_t::status () const
{
    zlink_registry_status_t native;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_registry_status (_impl->handle, &native)));
    return zlink::detail::service_model_access_t::from_native (native);
}

std::vector<registry_service_summary_entry_t>
registry_t::service_summary (
  const registry_service_summary_filter_t *filter_) const
{
    zlink_registry_service_summary_filter_t native_filter;
    const zlink_registry_service_summary_filter_t *filter_ptr = nullptr;
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
              native_filter.channel_name, sizeof (native_filter.channel_name),
              "%s", filter_->channel_name ()->c_str ());
        filter_ptr = &native_filter;
    }

    size_t count = 0;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_registry_service_summary (
          _impl->handle, filter_ptr, nullptr, &count)));
    std::vector<zlink_registry_service_summary_entry_t> native (count);
    if (count > 0) {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_registry_service_summary (
              _impl->handle, filter_ptr, native.data (), &count)));
        native.resize (count);
    }
    std::vector<registry_service_summary_entry_t> entries;
    entries.reserve (native.size ());
    for (size_t i = 0; i < native.size (); ++i)
        entries.push_back (
          zlink::detail::service_model_access_t::from_native (native[i]));
    return entries;
}

std::vector<registry_topology_entry_t> registry_t::topology () const
{
    size_t count = 0;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_registry_topology (_impl->handle, nullptr, nullptr, &count)));
    std::vector<zlink_registry_topology_entry_t> native (count);
    if (count > 0) {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_registry_topology (
              _impl->handle, nullptr, native.data (), &count)));
        native.resize (count);
    }
    std::vector<registry_topology_entry_t> entries;
    entries.reserve (native.size ());
    for (size_t i = 0; i < native.size (); ++i)
        entries.push_back (
          zlink::detail::service_model_access_t::from_native (native[i]));
    return entries;
}

std::vector<registry_topology_entry_t>
registry_t::topology (const registry_topology_filter_t &filter_) const
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
        zlink_registry_topology (_impl->handle, &native_filter, nullptr, &count)));
    std::vector<zlink_registry_topology_entry_t> native (count);
    if (count > 0) {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_registry_topology (
              _impl->handle, &native_filter, native.data (), &count)));
        native.resize (count);
    }
    std::vector<registry_topology_entry_t> entries;
    entries.reserve (native.size ());
    for (size_t i = 0; i < native.size (); ++i)
        entries.push_back (
          zlink::detail::service_model_access_t::from_native (native[i]));
    return entries;
}

std::vector<member_peer_entry_t>
registry_t::member_peers (const std::string &channel_name_) const
{
    zlink::detail::validate_bounded_c_string (
      channel_name_, 255u, "channel_name");
    size_t count = 0;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_registry_member_peers (
          _impl->handle, channel_name_.c_str (), nullptr, &count)));
    std::vector<zlink_member_peer_entry_t> native (count);
    if (count > 0) {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_registry_member_peers (
              _impl->handle, channel_name_.c_str (), native.data (), &count)));
        native.resize (count);
    }
    std::vector<member_peer_entry_t> entries;
    entries.reserve (native.size ());
    for (size_t i = 0; i < native.size (); ++i)
        entries.push_back (
          zlink::detail::service_model_access_t::from_native (native[i]));
    return entries;
}

void registry_t::close ()
{
    if (!_impl->handle)
        return;

    void *tmp = _impl->handle;
    detail::throw_if_failed<close_error_t> (
      static_cast<close_result_t> (zlink_registry_destroy (&tmp)));
    _impl->handle = nullptr;
}

} // namespace zlink::service
