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

    ~discovery_t () { (void) close (); }

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

        (void) close ();
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

    ZLINK_CPP_NODISCARD int connect_registry (const std::string &endpoint_)
    {
        validate_bounded_c_string (endpoint_, 255u, "endpoint");
        return zlink_discovery_connect_registry (_discovery, endpoint_.c_str ());
    }

    ZLINK_CPP_NODISCARD int set_value (int64_t value_)
    {
        return zlink_discovery_set_value (_discovery, value_);
    }

    ZLINK_CPP_NODISCARD int get_value (int64_t *value_out_) const
    {
        return zlink_discovery_get_value (_discovery, value_out_);
    }

    ZLINK_CPP_NODISCARD int
    set_metadata (const void *data_, size_t size_)
    {
        return zlink_discovery_set_metadata (_discovery, data_, size_);
    }

    ZLINK_CPP_NODISCARD int
    set_metadata (const std::vector<uint8_t> &bytes_)
    {
        return set_metadata (
          bytes_.empty () ? NULL : &bytes_[0], bytes_.size ());
    }

    ZLINK_CPP_NODISCARD int set_metadata (const std::string &text_)
    {
        return set_metadata (text_.data (), text_.size ());
    }

    ZLINK_CPP_NODISCARD int get_metadata (message_t &metadata_out_) const
    {
        zlink_msg_t native;
        const int rc = zlink_discovery_get_metadata (_discovery, &native);
        if (rc != 0)
            return rc;

        metadata_out_.adopt (&native);
        return 0;
    }

    ZLINK_CPP_NODISCARD int
    member_peers (zlink_member_peer_entry_t *entries_, size_t *count_) const
    {
        return zlink_discovery_member_peers (_discovery, entries_, count_);
    }

    ZLINK_CPP_NODISCARD int
    member_peer_metadata (service_role service_role_,
                          const std::string &endpoint_,
                          message_t &metadata_out_) const
    {
        validate_bounded_c_string (endpoint_, 255u, "endpoint");
        zlink_msg_t native;
        const int rc = zlink_discovery_member_peer_metadata (
          _discovery, static_cast<uint16_t> (service_role_),
          endpoint_.c_str (), &native);
        if (rc != 0)
            return rc;

        metadata_out_.adopt (&native);
        return 0;
    }

    ZLINK_CPP_NODISCARD service_monitor_handle_t
    monitor_open (service_monitor_event events_ = service_monitor_event::all);

    ZLINK_CPP_NODISCARD int close ()
    {
        if (!_discovery)
            return 0;

        void *tmp = _discovery;
        const int rc = zlink_discovery_destroy (&tmp);
        if (rc == 0)
            _discovery = NULL;
        return rc;
    }

    void *handle () const { return _discovery; }

  private:
    void *_discovery;
    int _last_error;
};

} // namespace service
} // namespace zlink

#endif
