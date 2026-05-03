/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_DISCOVERY_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_DISCOVERY_HPP_INCLUDED

#include "../context.hpp"
#include "../message.hpp"
#include "../types.hpp"

#include <cerrno>

namespace zlink
{
namespace service
{

class discovery_t
{
  public:
    discovery_t (context_t &ctx_,
                 auto_connect_type auto_connect_type_,
                 const std::string &channel_name_)
        : _discovery (NULL),
          _last_error (0)
    {
        validate_bounded_c_string (channel_name_, 255u, "channel_name");
        _discovery = zlink_discovery_new (
          ctx_.handle (),
          static_cast<zlink_auto_connect_type_t> (auto_connect_type_),
          channel_name_.c_str ());
        if (!_discovery)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    ~discovery_t ()
    {
        try {
            close ();
        } catch (...) {
        }
    }

    discovery_t (discovery_t &&other) noexcept
        : _discovery (other._discovery), _last_error (other._last_error)
    {
        other._discovery = NULL;
        other._last_error = 0;
    }

    discovery_t &operator= (discovery_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        try {
            close ();
        } catch (...) {
        }
        _discovery = other._discovery;
        _last_error = other._last_error;
        other._discovery = NULL;
        other._last_error = 0;
        return *this;
    }

    discovery_t (const discovery_t &) = delete;
    discovery_t &operator= (const discovery_t &) = delete;

    bool valid () const noexcept { return _discovery != NULL; }

    int last_error () const noexcept { return _last_error; }

    void connect_registry (const std::string &endpoint_)
    {
        validate_bounded_c_string (endpoint_, 255u, "endpoint");
        detail::throw_if_failed<connect_error_t> (
          static_cast<connect_result_t> (
            zlink_discovery_connect_registry (_discovery, endpoint_.c_str ())));
    }

    void set_value (int64_t value_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_discovery_set_value (_discovery, value_)));
    }

    void get_value (int64_t *value_out_) const
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_discovery_get_value (_discovery, value_out_)));
    }

    void set_spot_owner_sync_enabled (bool enabled_)
    {
        int value = enabled_ ? 1 : 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_set_option (_discovery,
                              ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC, &value,
                              sizeof (value))));
    }

    bool spot_owner_sync_enabled () const
    {
        int value = 0;
        size_t size = sizeof (value);
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_get_option (_discovery,
                              ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC, &value,
                              &size)));
        return value != 0;
    }

    std::vector<member_peer_entry_t> member_peers () const
    {
        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_discovery_member_peers (_discovery, NULL, &count)));
        std::vector<zlink_member_peer_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_discovery_member_peers (
                  _discovery, native.data (), &count)));
            native.resize (count);
        }

        std::vector<member_peer_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (member_peer_entry_t (native[i]));
        return entries;
    }

    routing_id_t resolve_spot (const routing_id_t &spot_rid_)
    {
        routing_id_t owner_node_rid;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_discovery_resolve_spot (
              _discovery, routing_id_native (spot_rid_),
              routing_id_native (owner_node_rid))));
        return owner_node_rid;
    }

    void bind_route (zlink_route_kind_t kind_,
                     const void *key_,
                     size_t key_size_,
                     const void *value_,
                     size_t value_size_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_discovery_bind_route (_discovery, kind_, key_, key_size_,
                                        value_, value_size_)));
    }

    void bind_route (zlink_route_kind_t kind_,
                     const std::string &key_,
                     const std::vector<uint8_t> &value_)
    {
        bind_route (kind_, key_.data (), key_.size (),
                    value_.empty () ? NULL : &value_[0], value_.size ());
    }

    void bind_route (zlink_route_kind_t kind_,
                     const std::string &key_,
                     const std::string &value_)
    {
        bind_route (kind_, key_.data (), key_.size (), value_.data (),
                    value_.size ());
    }

    void unbind_route (zlink_route_kind_t kind_,
                       const void *key_,
                       size_t key_size_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_discovery_unbind_route (_discovery, kind_, key_,
                                          key_size_)));
    }

    void unbind_route (zlink_route_kind_t kind_, const std::string &key_)
    {
        unbind_route (kind_, key_.data (), key_.size ());
    }

    routing_id_t resolve_route (zlink_route_kind_t kind_,
                                const void *key_,
                                size_t key_size_,
                                message_t &value_out_) const
    {
        routing_id_t owner_node_rid;
        zlink_msg_t native;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_discovery_resolve_route (
              _discovery, kind_, key_, key_size_,
              routing_id_native (owner_node_rid), &native)));
        value_out_.adopt (&native);
        return owner_node_rid;
    }

    routing_id_t resolve_route (zlink_route_kind_t kind_,
                                const std::string &key_,
                                message_t &value_out_) const
    {
        return resolve_route (kind_, key_.data (), key_.size (), value_out_);
    }

    void close ()
    {
        if (!_discovery)
            return;

        void *tmp = _discovery;
        detail::throw_if_failed<close_error_t> (
          static_cast<close_result_t> (zlink_discovery_destroy (&tmp)));
        _discovery = NULL;
    }

    void *handle () const { return _discovery; }

  private:
    void *_discovery;
    int _last_error;
};

} // namespace service
} // namespace zlink

#endif
