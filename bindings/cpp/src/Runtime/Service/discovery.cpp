/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Service/discovery.hpp>

#include <Runtime/Service/actor_model_access.hpp>
#include <Runtime/Service/service_model_access.hpp>
#include <Runtime/Service/discovery_access.hpp>
#include <Runtime/Core/context_access.hpp>
#include <Runtime/Core/routing_id_access.hpp>
#include <Runtime/Native/message_access.hpp>

#include <zlink.h>

#include <cerrno>
#include <cstring>

namespace zlink::service
{

struct discovery_t::impl
{
    void *handle = nullptr;
};

} // namespace zlink::service

namespace zlink::detail
{

void *discovery_access_t::native_handle (service::discovery_t &discovery_) noexcept
{
    return discovery_._impl ? discovery_._impl->handle : nullptr;
}

const void *discovery_access_t::native_handle (const service::discovery_t &discovery_) noexcept
{
    return discovery_._impl ? discovery_._impl->handle : nullptr;
}

} // namespace zlink::detail

namespace zlink::service
{

discovery_t::discovery_t (context_t &ctx_,
                          auto_connect_type auto_connect_type_,
                          const std::string &channel_name_) :
    _impl (std::make_unique<impl> ()), _last_error (0)
{
    zlink::detail::validate_bounded_c_string (channel_name_, 255u, "channel_name");
    _impl->handle = zlink_discovery_new (
      zlink::detail::native_handle (ctx_),
      static_cast<zlink_auto_connect_type_t> (auto_connect_type_), channel_name_.c_str ());
    if (!_impl->handle)
        _last_error = errno != 0 ? errno : EFAULT;
}

discovery_t::~discovery_t ()
{
    try {
        close ();
    }
    catch (...) {
    }
}

discovery_t::discovery_t (discovery_t &&other) noexcept :
    _impl (std::move (other._impl)), _last_error (other._last_error)
{
    if (!other._impl)
        other._impl = std::make_unique<impl> ();
    other._last_error = 0;
}

discovery_t &discovery_t::operator= (discovery_t &&other) noexcept
{
    if (this == &other)
        return *this;

    try {
        close ();
    }
    catch (...) {
    }
    _impl = std::move (other._impl);
    _last_error = other._last_error;
    if (!other._impl)
        other._impl = std::make_unique<impl> ();
    other._last_error = 0;
    return *this;
}

bool discovery_t::valid () const noexcept
{
    return _impl && _impl->handle != nullptr;
}

void discovery_t::connect_registry (const std::string &endpoint_)
{
    zlink::detail::validate_bounded_c_string (endpoint_, 255u, "endpoint");
    detail::throw_if_failed<connect_error_t> (static_cast<connect_result_t> (
      zlink_discovery_connect_registry (_impl->handle, endpoint_.c_str ())));
}

void discovery_t::set_tls_client (const std::string &ca_cert_,
                                  const std::string &hostname_,
                                  bool trust_system_)
{
    const char *ca = ca_cert_.empty () ? nullptr : ca_cert_.c_str ();
    const char *hostname = hostname_.empty () ? nullptr : hostname_.c_str ();
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_set_tls_client (_impl->handle, ca, hostname, trust_system_ ? 1 : 0)));
}

void discovery_t::set_value (int64_t value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_discovery_set_value (_impl->handle, value_)));
}

void discovery_t::get_value (int64_t *value_out_) const
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_discovery_get_value (_impl->handle, value_out_)));
}

void discovery_t::set_spot_owner_sync_enabled (bool enabled_)
{
    int value = enabled_ ? 1 : 0;
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (zlink_set_option (
      _impl->handle, static_cast<zlink_option_t> (12341), &value, sizeof (value))));
}

bool discovery_t::spot_owner_sync_enabled () const
{
    int value = 0;
    size_t size = sizeof (value);
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_get_option (_impl->handle, static_cast<zlink_option_t> (12341), &value, &size)));
    return value != 0;
}

void discovery_t::set_actor_route_sync_enabled (bool enabled_)
{
    int value = enabled_ ? 1 : 0;
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (zlink_set_option (
      _impl->handle, static_cast<zlink_option_t> (12342), &value, sizeof (value))));
}

bool discovery_t::actor_route_sync_enabled () const
{
    int value = 0;
    size_t size = sizeof (value);
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_get_option (_impl->handle, static_cast<zlink_option_t> (12342), &value, &size)));
    return value != 0;
}

std::vector<member_peer_entry_t> discovery_t::member_peers () const
{
    size_t count = 0;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_discovery_member_peers (_impl->handle, nullptr, &count)));
    std::vector<zlink_member_peer_entry_t> native (count);
    if (count > 0) {
        while (true) {
            const auto result = static_cast<config_result_t> (
              zlink_discovery_member_peers (_impl->handle, native.data (), &count));
            if (result == config_result_t::ok) {
                native.resize (count);
                break;
            }
            if (result == config_result_t::internal_error && zlink_errno () == ENOBUFS) {
                native.resize (count);
                continue;
            }
            detail::throw_if_failed<config_error_t> (result);
        }
    }

    std::vector<member_peer_entry_t> entries;
    entries.reserve (native.size ());
    for (size_t i = 0; i < native.size (); ++i)
        entries.push_back (zlink::detail::service_model_access_t::from_native (native[i]));
    return entries;
}

spot_route_t discovery_t::resolve_spot (const routing_id_t &spot_rid_)
{
    zlink_spot_route_t native;
    std::memset (&native, 0, sizeof (native));
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_discovery_resolve_spot (
        _impl->handle, zlink::detail::routing_id_native (spot_rid_), &native)));
    return zlink::detail::actor_model_access_t::from_native (native);
}

actor_route_t discovery_t::resolve_actor (const std::string &actor_id_)
{
    zlink::detail::validate_bounded_c_string (actor_id_, 256 - 1u, "actor_id");
    zlink_actor_route_t native;
    std::memset (&native, 0, sizeof (native));
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_discovery_resolve_actor (_impl->handle, actor_id_.c_str (), &native)));
    return zlink::detail::actor_model_access_t::from_native (native);
}

void discovery_t::bind_route (route_kind_t kind_,
                              std::span<const std::byte> key_,
                              std::span<const std::byte> value_)
{
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_discovery_bind_route (_impl->handle, static_cast<zlink_route_kind_t> (kind_),
                                  key_.data (), key_.size (), value_.data (), value_.size ())));
}

void discovery_t::bind_route (route_kind_t kind_,
                              std::span<const uint8_t> key_,
                              std::span<const uint8_t> value_)
{
    bind_route (kind_, std::as_bytes (key_), std::as_bytes (value_));
}

void discovery_t::unbind_route (route_kind_t kind_, std::span<const std::byte> key_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_discovery_unbind_route (
        _impl->handle, static_cast<zlink_route_kind_t> (kind_), key_.data (), key_.size ())));
}

void discovery_t::unbind_route (route_kind_t kind_, std::span<const uint8_t> key_)
{
    unbind_route (kind_, std::as_bytes (key_));
}

discovery_route_t discovery_t::resolve_route (route_kind_t kind_, std::span<const std::byte> key_)
{
    zlink_routing_id_t owner;
    zlink_msg_t value;
    std::memset (&owner, 0, sizeof (owner));
    std::memset (&value, 0, sizeof (value));
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_discovery_resolve_route (_impl->handle, static_cast<zlink_route_kind_t> (kind_),
                                     key_.data (), key_.size (), &owner, &value)));

    message_t msg;
    zlink::detail::adopt_native_message (msg, &value);
    return discovery_route_t (zlink::detail::routing_id_access_t::from_native (owner),
                              std::move (msg));
}

discovery_route_t discovery_t::resolve_route (route_kind_t kind_, std::span<const uint8_t> key_)
{
    return resolve_route (kind_, std::as_bytes (key_));
}

void discovery_t::close ()
{
    if (!_impl->handle)
        return;

    void *tmp = _impl->handle;
    detail::throw_if_failed<close_error_t> (
      static_cast<close_result_t> (zlink_discovery_destroy (&tmp)));
    _impl->handle = nullptr;
}

} // namespace zlink::service
