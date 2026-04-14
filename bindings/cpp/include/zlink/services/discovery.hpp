/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_DISCOVERY_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_DISCOVERY_HPP_INCLUDED

#include "../context.hpp"
#include "../message.hpp"
#include "../types.hpp"

#include <cerrno>

namespace zlink
{
class service_monitor_handle_t;

namespace service
{

class discovery_t
{
  public:
    discovery_t (context_t &ctx_,
                 service_type service_type_,
                 const std::string &service_name_)
        : _discovery (NULL),
          _last_error (0)
    {
        validate_bounded_c_string (service_name_, 255u, "service_name");
        _discovery = zlink_discovery_new (
          ctx_.handle (), static_cast<zlink_service_type_t> (service_type_),
          service_name_.c_str ());
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

    void set_metadata (const void *data_, size_t size_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_discovery_set_metadata (_discovery, data_, size_)));
    }

    void set_metadata (const std::vector<uint8_t> &bytes_)
    {
        set_metadata (
          bytes_.empty () ? NULL : &bytes_[0], bytes_.size ());
    }

    void set_metadata (const std::string &text_)
    {
        set_metadata (text_.data (), text_.size ());
    }

    void get_metadata (message_t &metadata_out_) const
    {
        zlink_msg_t native;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_discovery_get_metadata (_discovery, &native)));
        metadata_out_.adopt (&native);
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

    void member_peer_metadata (service_role service_role_,
                               const std::string &endpoint_,
                               message_t &metadata_out_) const
    {
        validate_bounded_c_string (endpoint_, 255u, "endpoint");
        zlink_msg_t native;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_discovery_member_peer_metadata (
              _discovery, static_cast<uint16_t> (service_role_),
              endpoint_.c_str (), &native)));
        metadata_out_.adopt (&native);
    }

    ZLINK_CPP_NODISCARD service_monitor_handle_t
    monitor_open (service_monitor_event events_ = service_monitor_event::all);

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

    void set_dealer_peer_mode (discovery_dealer_peer_mode_t mode_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_discovery_set_dealer_peer_mode (
              _discovery,
              static_cast<zlink_discovery_dealer_peer_mode_t> (mode_))));
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
