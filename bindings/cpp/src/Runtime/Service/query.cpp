/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Service/registry.hpp>
#include <Runtime/Core/context_access.hpp>
#include <Runtime/Service/registry_access.hpp>
#include <Runtime/Service/service_model_access.hpp>

#include <zlink.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace zlink::service
{

struct registry_query_client_t::impl
{
    void *handle = nullptr;
};

} // namespace zlink::service

namespace zlink::detail
{

void *registry_access_t::native_handle (service::registry_query_client_t &client_) noexcept
{
    return client_._impl ? client_._impl->handle : nullptr;
}

const void *registry_access_t::native_handle (const service::registry_query_client_t &client_) noexcept
{
    return client_._impl ? client_._impl->handle : nullptr;
}

} // namespace zlink::detail

namespace zlink::service
{

registry_query_client_t::registry_query_client_t (context_t &ctx_) : _impl (std::make_unique<impl> ()), _last_error (0)
{
    _impl->handle = zlink_registry_query_client_new (zlink::detail::native_handle (ctx_));
    if (!_impl->handle)
        _last_error = errno != 0 ? errno : EFAULT;
}

registry_query_client_t::~registry_query_client_t ()
{
    try {
        close ();
    }
    catch (...) {
    }
}

registry_query_client_t::registry_query_client_t (registry_query_client_t &&other) noexcept :
    _impl (std::move (other._impl)), _last_error (other._last_error)
{
    if (!other._impl)
        other._impl = std::make_unique<impl> ();
    other._last_error = 0;
}

registry_query_client_t &registry_query_client_t::operator= (registry_query_client_t &&other) noexcept
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

bool registry_query_client_t::valid () const noexcept
{
    return _impl && _impl->handle != nullptr;
}

void registry_query_client_t::connect (const std::string &endpoint_)
{
    zlink::detail::validate_bounded_c_string (endpoint_, 255u, "endpoint");
    detail::throw_if_failed<connect_error_t> (
      static_cast<connect_result_t> (zlink_registry_query_client_connect (_impl->handle, endpoint_.c_str ())));
}

std::vector<registry_topology_entry_t>
registry_query_client_t::topology (const registry_topology_filter_t *filter_) const
{
    zlink_registry_topology_filter_t native_filter;
    const zlink_registry_topology_filter_t *filter_ptr = nullptr;
    if (filter_) {
        std::memset (&native_filter, 0, sizeof (native_filter));
        if (filter_->auto_connect_type ())
            native_filter.auto_connect_type = static_cast<zlink_auto_connect_type_t> (*filter_->auto_connect_type ());
        if (filter_->service_kind ())
            native_filter.service_kind = static_cast<zlink_service_kind_t> (*filter_->service_kind ());
        if (filter_->service_role ())
            native_filter.service_role = static_cast<zlink_service_role_t> (*filter_->service_role ());
        if (filter_->channel_name ())
            std::snprintf (native_filter.channel_name, sizeof (native_filter.channel_name), "%s",
                           filter_->channel_name ()->c_str ());
        if (filter_->state ())
            native_filter.state = static_cast<zlink_topology_state_t> (*filter_->state ());
        if (filter_->source ())
            native_filter.source = static_cast<zlink_topology_source_t> (*filter_->source ());
        if (filter_->routing_id ())
            native_filter.routing_id = *zlink::detail::routing_id_native (*filter_->routing_id ());
        filter_ptr = &native_filter;
    }

    size_t count = 0;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_registry_query_client_topology (_impl->handle, filter_ptr, nullptr, &count)));
    std::vector<zlink_registry_topology_entry_t> native (count);
    if (count > 0) {
        detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
          zlink_registry_query_client_topology (_impl->handle, filter_ptr, native.data (), &count)));
        native.resize (count);
    }
    std::vector<registry_topology_entry_t> entries;
    entries.reserve (native.size ());
    for (size_t i = 0; i < native.size (); ++i)
        entries.push_back (zlink::detail::service_model_access_t::from_native (native[i]));
    return entries;
}

void registry_query_client_t::close ()
{
    if (!_impl->handle)
        return;

    void *tmp = _impl->handle;
    detail::throw_if_failed<close_error_t> (static_cast<close_result_t> (zlink_registry_query_client_destroy (&tmp)));
    _impl->handle = nullptr;
}

} // namespace zlink::service
