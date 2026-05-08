/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_QUERY_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_QUERY_HPP_INCLUDED

#include "../context.hpp"
#include "../types.hpp"

#include <cerrno>
#include <cstdio>

namespace zlink
{
namespace service
{

class registry_query_client_t;

} // namespace service

namespace detail
{
inline void *
native_handle (service::registry_query_client_t &client_) noexcept;
inline const void *
native_handle (const service::registry_query_client_t &client_) noexcept;
} // namespace detail

namespace service
{

class registry_query_client_t
{
  public:
    explicit registry_query_client_t (context_t &ctx_)
        : _client (zlink_registry_query_client_new (detail::native_handle (ctx_))),
          _last_error (0)
    {
        if (!_client)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    ~registry_query_client_t ()
    {
        try {
            close ();
        } catch (...) {
        }
    }

    registry_query_client_t (registry_query_client_t &&other) noexcept
        : _client (other._client), _last_error (other._last_error)
    {
        other._client = NULL;
        other._last_error = 0;
    }

    registry_query_client_t &operator= (registry_query_client_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        try {
            close ();
        } catch (...) {
        }
        _client = other._client;
        _last_error = other._last_error;
        other._client = NULL;
        other._last_error = 0;
        return *this;
    }

    registry_query_client_t (const registry_query_client_t &) = delete;
    registry_query_client_t &
    operator= (const registry_query_client_t &) = delete;

    bool valid () const noexcept { return _client != NULL; }

    void connect (const std::string &endpoint_)
    {
        zlink::detail::validate_bounded_c_string (endpoint_, 255u, "endpoint");
        detail::throw_if_failed<connect_error_t> (
          static_cast<connect_result_t> (
            zlink_registry_query_client_connect (_client, endpoint_.c_str ())));
    }

    std::vector<registry_topology_entry_t>
    snapshot (const registry_topology_filter_t *filter_ = NULL) const
    {
        zlink_registry_topology_filter_t native_filter;
        const zlink_registry_topology_filter_t *filter_ptr = NULL;
        if (filter_) {
            std::memset (&native_filter, 0, sizeof (native_filter));
            if (filter_->auto_connect_type ())
                native_filter.auto_connect_type =
                  static_cast<zlink_auto_connect_type_t> (
                    *filter_->auto_connect_type ());
            if (filter_->service_kind ())
                native_filter.service_kind =
                  static_cast<zlink_service_kind_t> (*filter_->service_kind ());
            if (filter_->service_role ())
                native_filter.service_role =
                  static_cast<zlink_service_role_t> (*filter_->service_role ());
            if (filter_->channel_name ())
                std::snprintf (
                  native_filter.channel_name,
                  sizeof (native_filter.channel_name), "%s",
                  filter_->channel_name ()->c_str ());
            if (filter_->state ())
                native_filter.state =
                  static_cast<zlink_topology_state_t> (*filter_->state ());
            if (filter_->source ())
                native_filter.source =
                  static_cast<zlink_topology_source_t> (*filter_->source ());
            if (filter_->routing_id ())
                native_filter.routing_id =
                  *zlink::detail::routing_id_native (*filter_->routing_id ());
            filter_ptr = &native_filter;
        }

        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_registry_query_snapshot (_client, filter_ptr, NULL, &count)));
        std::vector<zlink_registry_topology_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_registry_query_snapshot (
                  _client, filter_ptr, native.data (), &count)));
            native.resize (count);
        }
        std::vector<registry_topology_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (registry_topology_entry_t (native[i]));
        return entries;
    }

    void close ()
    {
        if (!_client)
            return;

        void *tmp = _client;
        detail::throw_if_failed<close_error_t> (
          static_cast<close_result_t> (zlink_registry_query_destroy (&tmp)));
        _client = NULL;
    }

  private:
    friend void *
    zlink::detail::native_handle (registry_query_client_t &client_) noexcept;
    friend const void *
    zlink::detail::native_handle (const registry_query_client_t &client_) noexcept;

    void *_client;
    int _last_error;
};

} // namespace service

namespace detail
{
inline void *
native_handle (service::registry_query_client_t &client_) noexcept
{
    return client_._client;
}

inline const void *
native_handle (const service::registry_query_client_t &client_) noexcept
{
    return client_._client;
}
} // namespace detail

} // namespace zlink

#endif
