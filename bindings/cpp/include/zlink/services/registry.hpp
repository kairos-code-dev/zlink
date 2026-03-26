/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_REGISTRY_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_REGISTRY_HPP_INCLUDED

#include "../context.hpp"
#include "../message.hpp"
#include "../types.hpp"

#include <cerrno>

namespace zlink
{
namespace service
{

class registry_t
{
  public:
    explicit registry_t (context_t &ctx_)
        : _registry (zlink_registry_new (ctx_.handle ())), _last_error (0)
    {
        if (!_registry)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    ~registry_t () { (void) destroy (); }

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

        (void) destroy ();
        _registry = other._registry;
        _last_error = other._last_error;
        other._registry = NULL;
        other._last_error = 0;
        return *this;
    }

    registry_t (const registry_t &) = delete;
    registry_t &operator= (const registry_t &) = delete;

    bool valid () const noexcept { return _registry != NULL; }

    int last_error () const noexcept { return _last_error; }

    ZLINK_CPP_NODISCARD int
    bind (const std::string &pub_endpoint_, const std::string &router_endpoint_)
    {
        return zlink_registry_bind (
          _registry, pub_endpoint_.c_str (), router_endpoint_.c_str ());
    }

    ZLINK_CPP_NODISCARD int set_id (uint32_t registry_id_)
    {
        return zlink_registry_set_id (_registry, registry_id_);
    }

    ZLINK_CPP_NODISCARD int add_peer (const std::string &peer_pub_endpoint_)
    {
        return zlink_registry_add_peer (_registry, peer_pub_endpoint_.c_str ());
    }

    ZLINK_CPP_NODISCARD int
    set_heartbeat (uint32_t interval_ms_, uint32_t timeout_ms_)
    {
        return zlink_registry_set_heartbeat (
          _registry, interval_ms_, timeout_ms_);
    }

    ZLINK_CPP_NODISCARD int set_broadcast_interval (uint32_t interval_ms_)
    {
        return zlink_registry_set_broadcast_interval (_registry, interval_ms_);
    }

    ZLINK_CPP_NODISCARD int
    status_snapshot (zlink_registry_status_t &out_) const
    {
        return zlink_registry_status_snapshot (_registry, &out_);
    }

    ZLINK_CPP_NODISCARD int
    service_summary_snapshot (zlink_registry_service_summary_entry_t *entries_,
                              size_t *count_,
                              const zlink_registry_service_summary_filter_t
                                *filter_ = NULL) const
    {
        return zlink_registry_service_summary_snapshot (
          _registry, filter_, entries_, count_);
    }

    ZLINK_CPP_NODISCARD int
    topology_snapshot (zlink_registry_topology_entry_t *entries_,
                       size_t *count_) const
    {
        return zlink_registry_topology_snapshot (_registry, entries_, count_);
    }

    ZLINK_CPP_NODISCARD int
    topology_query (zlink_registry_topology_entry_t *entries_,
                    size_t *count_,
                    const zlink_registry_topology_filter_t *filter_) const
    {
        return zlink_registry_topology_query (
          _registry, filter_, entries_, count_);
    }

    ZLINK_CPP_NODISCARD int
    member_peers (service_type service_type_,
                  const std::string &service_name_,
                  zlink_member_peer_entry_t *entries_,
                  size_t *count_) const
    {
        return zlink_registry_member_peers (
          _registry, static_cast<zlink_service_type_t> (service_type_),
          service_name_.c_str (), entries_, count_);
    }

    ZLINK_CPP_NODISCARD int
    member_peer_metadata (service_type service_type_,
                          const std::string &service_name_,
                          service_role service_role_,
                          const std::string &endpoint_,
                          message_t &metadata_out_) const
    {
        zlink_msg_t native;
        const int rc = zlink_registry_member_peer_metadata (
          _registry, static_cast<zlink_service_type_t> (service_type_),
          service_name_.c_str (), static_cast<uint16_t> (service_role_),
          endpoint_.c_str (), &native);
        if (rc != 0)
            return rc;

        if (metadata_out_.adopt (&native) == 0)
            return 0;

        zlink_msg_close (&native);
        return -1;
    }

    ZLINK_CPP_NODISCARD int destroy ()
    {
        if (!_registry)
            return 0;

        void *tmp = _registry;
        const int rc = zlink_registry_destroy (&tmp);
        if (rc == 0)
            _registry = NULL;
        return rc;
    }

    void *handle () const { return _registry; }

  private:
    void *_registry;
    int _last_error;
};

} // namespace service
} // namespace zlink

#endif
