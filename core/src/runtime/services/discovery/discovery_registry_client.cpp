/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/c_api_copy_internal.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_debug.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/discovery_registry_rpc.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"

#include <cstring>

namespace zlink
{
namespace
{
static uint16_t resolve_registered_service_role_local (uint16_t service_role_)
{
    return service_role_;
}

static bool topology_state_is_resolvable_local (zlink_topology_state_t state_)
{
    return state_ == ZLINK_TOPOLOGY_STATE_READY;
}

static bool spot_kind_is_resolvable_local (zlink_spot_kind_t kind_)
{
    return kind_ == ZLINK_SPOT_KIND_ENTRY || kind_ == ZLINK_SPOT_KIND_USER;
}

static const uint64_t resolve_spot_cache_ttl_ms = 250;

static bool valid_route_request_local (zlink_route_kind_t kind_,
                                       const void *key_,
                                       size_t key_size_,
                                       const void *value_,
                                       size_t value_size_,
                                       bool has_value_)
{
    if (kind_ == ZLINK_ROUTE_KIND_INVALID || !key_ || key_size_ == 0) {
        errno = EINVAL;
        return false;
    }
    if (key_size_ > ZLINK_ROUTE_KEY_MAX) {
        errno = EMSGSIZE;
        return false;
    }
    if (has_value_) {
        if (value_size_ > ZLINK_ROUTE_VALUE_MAX) {
            errno = EMSGSIZE;
            return false;
        }
        if (!value_ && value_size_ > 0) {
            errno = EINVAL;
            return false;
        }
    }
    return true;
}
}

int discovery_t::resolve_spot (const zlink_routing_id_t *spot_rid_,
                               zlink_spot_route_t *route_out_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!spot_rid_ || spot_rid_->size == 0 || !route_out_) {
        errno = EINVAL;
        return -1;
    }
    if (_auto_connect_type != ZLINK_AUTO_CONNECT_SPOT_MESH) {
        errno = ENOTSUP;
        return -1;
    }

    memset (route_out_, 0, sizeof (*route_out_));

    const topology_key_t key = make_spot_topology_key (*spot_rid_);

    const uint64_t now_ms = zlink::clock_t ().now_ms ();
    {
        scoped_lock_t lock (_sync);
        if (try_resolve_spot_from_cache_locked (key, now_ms, route_out_)) {
            return 0;
        }
    }

    flush_topology_reports ();

    const uint64_t flushed_now_ms = zlink::clock_t ().now_ms ();
    {
        scoped_lock_t lock (_sync);
        if (try_resolve_spot_from_cache_locked (key, flushed_now_ms,
                                                route_out_)) {
            return 0;
        }
    }

    std::vector<zlink_registry_topology_entry_t> entries;
    if (query_spot_owner_entries_from_registry (spot_rid_, &entries) != 0)
        return -1;

    {
        scoped_lock_t lock (_sync);
        refresh_spot_owner_cache_locked (key, entries);
        const uint64_t refreshed_now_ms = zlink::clock_t ().now_ms ();
        if (try_resolve_spot_from_cache_locked (key, refreshed_now_ms,
                                                route_out_)) {
            return 0;
        }
    }

    errno = ENOENT;
    return -1;
}

discovery_t::topology_key_t discovery_t::make_spot_topology_key (
  const zlink_routing_id_t &spot_rid_) const
{
    return make_summary_key (ZLINK_SERVICE_KIND_SPOT_PUB, ZLINK_SERVICE_ROLE_SPOT,
                             spot_rid_, _channel_name);
}

bool discovery_t::resolve_owner_node_from_endpoint_locked (
  const char *endpoint_, zlink_routing_id_t *owner_node_rid_out_) const
{
    if (!endpoint_ || endpoint_[0] == '\0' || !owner_node_rid_out_)
        return false;

    for (std::map<registered_service_key_t, registered_service_t>::const_iterator
           it = _registered_services.begin ();
         it != _registered_services.end (); ++it) {
        if (it->second.service_role != discovery_protocol::service_role_spot
            || it->second.endpoint != endpoint_
            || it->second.routing_id.size == 0) {
            continue;
        }
        *owner_node_rid_out_ = it->second.routing_id;
        return true;
    }

    std::vector<provider_info_t> providers;
    _service_state.snapshot_providers (&providers);
    for (size_t i = 0; i < providers.size (); ++i) {
        if (providers[i].service_role != discovery_protocol::service_role_spot
            || providers[i].endpoint != endpoint_
            || providers[i].routing_id.size == 0) {
            continue;
        }
        *owner_node_rid_out_ = providers[i].routing_id;
        return true;
    }
    return false;
}

bool discovery_t::try_resolve_spot_from_cache_locked (
  const topology_key_t &key_,
  uint64_t now_ms_,
  zlink_spot_route_t *route_out_) const
{
    if (!route_out_)
        return false;
    std::map<topology_key_t, topology_summary_t>::const_iterator it =
      _summary_store.find (key_);
    if (it == _summary_store.end ()
        || !topology_state_is_resolvable_local (it->second.entry.state)
        || it->second.entry.endpoint[0] == '\0'
        || !spot_kind_is_resolvable_local (it->second.entry.spot_kind)) {
        return false;
    }

    const uint64_t current_service_seq = _service_state.service_update_seq ();
    const bool validated_recently =
      it->second.validated_at_ms > 0
      && (it->second.validated_at_ms >= now_ms_
          || now_ms_ - it->second.validated_at_ms
               <= resolve_spot_cache_ttl_ms);
    const bool cache_is_fresh =
      it->second.validated_service_seq == current_service_seq
      && validated_recently;
    if (!cache_is_fresh) {
        // Provider membership changed or the cache aged out, so the owner may
        // have moved even if the old endpoint still exists.
        return false;
    }

    zlink_routing_id_t owner_node_rid;
    memset (&owner_node_rid, 0, sizeof (owner_node_rid));
    if (!resolve_owner_node_from_endpoint_locked (it->second.entry.endpoint,
                                                  &owner_node_rid)
        || owner_node_rid.size == 0) {
        return false;
    }

    memset (route_out_, 0, sizeof (*route_out_));
    route_out_->spot_rid = it->second.entry.routing_id;
    route_out_->owner_node_rid = owner_node_rid;
    route_out_->spot_kind = it->second.entry.spot_kind;
    return true;
}

int discovery_t::query_spot_owner_entries_from_registry (
  const zlink_routing_id_t *spot_rid_,
  std::vector<zlink_registry_topology_entry_t> *entries_out_)
{
    if (!spot_rid_ || !entries_out_) {
        errno = EINVAL;
        return -1;
    }

    std::string uplink;
    if (!_uplink_runtime->latest_registry_uplink (this, &uplink)) {
        errno = EAGAIN;
        return -1;
    }

    socket_base_t *dealer = NULL;
    if (discovery_registry_rpc::prepare_transient_dealer (_ctx, _bootstrap_runtime, uplink, NULL,
                                        &dealer)
        != 0) {
        return -1;
    }

    zlink_registry_topology_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    filter.service_kind = ZLINK_SERVICE_KIND_SPOT_PUB;
    filter.service_role = ZLINK_SERVICE_ROLE_SPOT;
    filter.routing_id = *spot_rid_;
    filter.auto_connect_type =
      static_cast<zlink_auto_connect_type_t> (_auto_connect_type);
    copy_fixed_c_string_from_cstr (filter.channel_name,
                                   sizeof (filter.channel_name),
                                   _channel_name.c_str ());

    int rc = 0;
    if (discovery_protocol::send_u16 (static_cast<void *> (dealer),
                                      discovery_protocol::msg_topology_query,
                                      ZLINK_SNDMORE)
          < 0
        || discovery_protocol::send_frame (static_cast<void *> (dealer), &filter,
                                           sizeof (filter), 0)
             < 0
        || discovery_registry_rpc::recv_topology_reply_entries (dealer, entries_out_) != 0) {
        rc = -1;
    }

    const int saved_errno = errno;
    (void) discovery_registry_rpc::close_dealer (_ctx, dealer);
    if (rc != 0) {
        errno = saved_errno;
        return -1;
    }
    return 0;
}

void discovery_t::refresh_spot_owner_cache_locked (
  const topology_key_t &key_,
  const std::vector<zlink_registry_topology_entry_t> &entries_)
{
    _summary_store.erase (key_);
    const uint64_t validated_service_seq = _service_state.service_update_seq ();
    for (size_t i = 0; i < entries_.size (); ++i) {
        const zlink_registry_topology_entry_t &entry = entries_[i];
        if (entry.routing_id.size == 0)
            continue;

        const topology_key_t entry_key = make_summary_key (
          entry.service_kind, entry.service_role, entry.routing_id,
          entry.channel_name);
        store_summary_entry_locked (
          entry_key, entry, false, entry.state == ZLINK_TOPOLOGY_STATE_STOPPED,
          validated_service_seq);
    }
}

bool discovery_t::select_route_owner_locked (registered_service_t *owner_out_) const
{
    if (!owner_out_)
        return false;
    bool found = false;
    for (std::map<registered_service_key_t, registered_service_t>::const_iterator
           it = _registered_services.begin ();
         it != _registered_services.end (); ++it) {
        if (it->second.channel_name != _channel_name)
            continue;
        if (found) {
            errno = EINVAL;
            return false;
        }
        *owner_out_ = it->second;
        found = true;
    }
    if (!found)
        errno = ENOENT;
    return found;
}

int discovery_t::bind_route (zlink_route_kind_t kind_,
                             const void *key_,
                             size_t key_size_,
                             const void *value_,
                             size_t value_size_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!valid_route_request_local (kind_, key_, key_size_, value_,
                                    value_size_, true))
        return -1;

    registered_service_t owner;
    {
        scoped_lock_t lock (_sync);
        if (value_size_ > _local_state.route_value_max_size ()) {
            errno = EMSGSIZE;
            return -1;
        }
        if (!select_route_owner_locked (&owner))
            return -1;
    }
    if (owner.registration_id == 0) {
        errno = EAGAIN;
        return -1;
    }

    socket_base_t *dealer = NULL;
    if (discovery_registry_rpc::prepare_transient_dealer (_ctx, _bootstrap_runtime,
                                        owner.uplink_endpoint, NULL, &dealer)
        != 0) {
        return -1;
    }

    const uint32_t raw_kind = kind_;
    const int send_rc =
      discovery_protocol::send_u16 (dealer, discovery_protocol::msg_bind_route,
                                    ZLINK_SNDMORE)
        < 0
        || discovery_protocol::send_u32 (dealer, raw_kind, ZLINK_SNDMORE) < 0
        || discovery_protocol::send_frame (dealer, key_, key_size_,
                                           ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_frame (dealer, value_, value_size_,
                                           ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, _channel_name,
                                            ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_u16 (dealer, owner.service_role,
                                         ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, owner.endpoint,
                                            ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_u64 (dealer, owner.registration_id, 0) < 0;
    if (send_rc) {
        (void) discovery_registry_rpc::close_dealer (_ctx, dealer);
        return -1;
    }

    const int rc = discovery_registry_rpc::recv_route_reply (dealer, NULL, NULL);
    const int saved_errno = errno;
    (void) discovery_registry_rpc::close_dealer (_ctx, dealer);
    if (rc != 0)
        errno = saved_errno;
    return rc;
}

int discovery_t::unbind_route (zlink_route_kind_t kind_,
                               const void *key_,
                               size_t key_size_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!valid_route_request_local (kind_, key_, key_size_, NULL, 0, false))
        return -1;

    registered_service_t owner;
    {
        scoped_lock_t lock (_sync);
        if (!select_route_owner_locked (&owner))
            return -1;
    }
    if (owner.registration_id == 0) {
        errno = EAGAIN;
        return -1;
    }

    socket_base_t *dealer = NULL;
    if (discovery_registry_rpc::prepare_transient_dealer (_ctx, _bootstrap_runtime,
                                        owner.uplink_endpoint, NULL, &dealer)
        != 0) {
        return -1;
    }

    const uint32_t raw_kind = kind_;
    const int send_rc =
      discovery_protocol::send_u16 (
        dealer, discovery_protocol::msg_unbind_route, ZLINK_SNDMORE)
        < 0
        || discovery_protocol::send_u32 (dealer, raw_kind, ZLINK_SNDMORE) < 0
        || discovery_protocol::send_frame (dealer, key_, key_size_,
                                           ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, _channel_name,
                                            ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_u16 (dealer, owner.service_role,
                                         ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, owner.endpoint,
                                            ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_u64 (dealer, owner.registration_id, 0) < 0;
    if (send_rc) {
        (void) discovery_registry_rpc::close_dealer (_ctx, dealer);
        return -1;
    }

    const int rc = discovery_registry_rpc::recv_route_reply (dealer, NULL, NULL);
    const int saved_errno = errno;
    (void) discovery_registry_rpc::close_dealer (_ctx, dealer);
    if (rc != 0)
        errno = saved_errno;
    return rc;
}

int discovery_t::resolve_route (zlink_route_kind_t kind_,
                                const void *key_,
                                size_t key_size_,
                                zlink_routing_id_t *owner_rid_out_,
                                zlink_msg_t *value_out_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!owner_rid_out_
        || !valid_route_request_local (kind_, key_, key_size_, NULL, 0,
                                       false))
        return -1;

    std::string uplink;
    if (!_uplink_runtime->latest_registry_uplink (this, &uplink)) {
        errno = EAGAIN;
        return -1;
    }

    socket_base_t *dealer = NULL;
    if (discovery_registry_rpc::prepare_transient_dealer (_ctx, _bootstrap_runtime, uplink, NULL,
                                        &dealer)
        != 0) {
        return -1;
    }

    const uint32_t raw_kind = kind_;
    const int send_rc =
      discovery_protocol::send_u16 (
        dealer, discovery_protocol::msg_resolve_route, ZLINK_SNDMORE)
        < 0
        || discovery_protocol::send_u32 (dealer, raw_kind, ZLINK_SNDMORE) < 0
        || discovery_protocol::send_frame (dealer, key_, key_size_,
                                           ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, _channel_name, 0) < 0;
    if (send_rc) {
        (void) discovery_registry_rpc::close_dealer (_ctx, dealer);
        return -1;
    }

    const int rc =
      discovery_registry_rpc::recv_route_reply (dealer, owner_rid_out_, value_out_);
    const int saved_errno = errno;
    (void) discovery_registry_rpc::close_dealer (_ctx, dealer);
    if (rc != 0)
        errno = saved_errno;
    return rc;
}

int discovery_t::register_service (const char *endpoint_,
                                   uint32_t weight_,
                                   int64_t value_,
                                   const std::vector<unsigned char> *metadata_,
                                   std::string *resolved_endpoint_out_,
                                   const zlink_routing_id_t *routing_id_,
                                   uint16_t service_role_)
{
    if (_channel_name.empty () || !endpoint_ || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    const uint16_t service_role =
      resolve_registered_service_role_local (service_role_);
    if (!discovery_protocol::auto_connect_type_allows_role (
          _auto_connect_type, service_role)) {
        errno = ENOTSUP;
        return -1;
    }

    std::string uplink;
    if (!_uplink_runtime->latest_registry_uplink (this, &uplink)) {
        errno = EAGAIN;
        return -1;
    }

    socket_base_t *dealer = NULL;
    if (discovery_registry_rpc::prepare_transient_dealer (_ctx, _bootstrap_runtime, uplink,
                                        routing_id_, &dealer)
        != 0) {
        discovery_debugf ("register_service pollout timeout uplink=%s",
                                uplink.c_str ());
        return -1;
    }

    if (discovery_protocol::send_u16 (
          dealer, discovery_protocol::msg_register, ZLINK_SNDMORE)
          < 0
        || discovery_protocol::send_u16 (dealer, _auto_connect_type,
                                         ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_u16 (dealer, service_role, ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, _channel_name,
                                            ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, endpoint_, ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_u16 (
             dealer, static_cast<uint16_t> (weight_), ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_i64 (dealer, value_, ZLINK_SNDMORE) < 0
        || discovery_protocol::send_frame (
             dealer, metadata_ && !metadata_->empty () ? &(*metadata_)[0] : NULL,
             metadata_ ? metadata_->size () : 0, 0)
             < 0) {
        (void) discovery_registry_rpc::close_dealer (_ctx, dealer);
        return -1;
    }

    int status = -1;
    uint32_t source_registry = 0;
    uint64_t registration_id = 0;
    std::string resolved;
    std::string error;
    if (discovery_registry_rpc::recv_status_ack (dealer, discovery_protocol::msg_register_ack,
                               &status, &resolved, &source_registry,
                               &registration_id, &error)
        != 0) {
        discovery_debugf ("register_service ack recv failed errno=%d",
                                errno);
        (void) discovery_registry_rpc::close_dealer (_ctx, dealer);
        return -1;
    }
    (void) discovery_registry_rpc::close_dealer (_ctx, dealer);
    if (status != discovery_protocol::status_ok) {
        if (status == -1) {
            discovery_debugf ("register_service ack timeout uplink=%s",
                                    uplink.c_str ());
            errno = EAGAIN;
            return -1;
        }
        discovery_debugf ("register_service rejected status=%d error=%s",
                                status, error.c_str ());
        errno = discovery_protocol::register_status_errno (
          static_cast<uint8_t> (status));
        return -1;
    }

    registered_service_key_t key;
    key.service_role = service_role;
    key.channel_name = _channel_name;
    key.endpoint = resolved.empty () ? endpoint_ : resolved;

    {
        scoped_lock_t lock (_sync);
        registered_service_t &service = _registered_services[key];
        service.service_role = service_role;
        service.channel_name = _channel_name;
        service.endpoint = key.endpoint;
        service.uplink_endpoint = uplink;
        service.weight = weight_;
        service.value = value_;
        service.metadata = metadata_ ? *metadata_ : std::vector<unsigned char> ();
        service.routing_id = routing_id_ ? *routing_id_
                                         : _bootstrap_runtime->routing_id_value ();
        service.source_registry = source_registry;
        service.registration_id = registration_id;
        service.last_heartbeat_ms = clock_t ().now_ms ();
    }

    if (resolved_endpoint_out_)
        *resolved_endpoint_out_ = key.endpoint;
    return 0;
}

int discovery_t::update_service_attributes (const char *endpoint_,
                                            uint32_t weight_,
                                            int64_t value_,
                                            const std::vector<unsigned char> *metadata_,
                                            uint16_t service_role_)
{
    if (_channel_name.empty () || !endpoint_ || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    const uint16_t service_role =
      resolve_registered_service_role_local (service_role_);
    if (!discovery_protocol::auto_connect_type_allows_role (
          _auto_connect_type, service_role)) {
        errno = ENOTSUP;
        return -1;
    }

    std::string uplink;
    {
        scoped_lock_t lock (_sync);
        registered_service_key_t key;
        key.service_role = service_role;
        key.channel_name = _channel_name;
        key.endpoint = endpoint_;
        std::map<registered_service_key_t, registered_service_t>::const_iterator
          it = _registered_services.find (key);
        if (it != _registered_services.end ())
            uplink = it->second.uplink_endpoint;
    }
    if (uplink.empty ())
        (void) _uplink_runtime->latest_registry_uplink (this, &uplink);
    if (uplink.empty ()) {
        errno = EAGAIN;
        return -1;
    }

    socket_base_t *dealer = NULL;
    if (discovery_registry_rpc::prepare_transient_dealer (_ctx, _bootstrap_runtime, uplink, NULL,
                                        &dealer)
        != 0) {
        discovery_debugf (
          "update_service_attributes pollout timeout uplink=%s",
          uplink.c_str ());
        return -1;
    }

    if (discovery_protocol::send_u16 (
          dealer, discovery_protocol::msg_update_attributes, ZLINK_SNDMORE)
          < 0
        || discovery_protocol::send_u16 (dealer, _auto_connect_type,
                                         ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_u16 (dealer, service_role, ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, _channel_name,
                                            ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, endpoint_, ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_u16 (
             dealer, static_cast<uint16_t> (weight_), ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_i64 (dealer, value_, ZLINK_SNDMORE) < 0
        || discovery_protocol::send_frame (
             dealer, metadata_ && !metadata_->empty () ? &(*metadata_)[0] : NULL,
             metadata_ ? metadata_->size () : 0, 0)
             < 0) {
        (void) discovery_registry_rpc::close_dealer (_ctx, dealer);
        return -1;
    }

    int status = -1;
    uint32_t source_registry = 0;
    uint64_t registration_id = 0;
    std::string resolved;
    std::string error;
    if (discovery_registry_rpc::recv_status_ack (dealer, discovery_protocol::msg_register_ack,
                               &status, &resolved, &source_registry,
                               &registration_id, &error)
        != 0) {
        discovery_debugf (
          "update_service_attributes ack recv failed errno=%d", errno);
        (void) discovery_registry_rpc::close_dealer (_ctx, dealer);
        return -1;
    }
    (void) discovery_registry_rpc::close_dealer (_ctx, dealer);
    if (status != discovery_protocol::status_ok) {
        if (status == -1) {
            discovery_debugf (
              "update_service_attributes ack timeout uplink=%s",
              uplink.c_str ());
            errno = EAGAIN;
            return -1;
        }
        discovery_debugf (
          "update_service_attributes rejected status=%d error=%s", status,
          error.c_str ());
        errno = discovery_protocol::register_status_errno (
          static_cast<uint8_t> (status));
        return -1;
    }

    scoped_lock_t lock (_sync);
    registered_service_key_t key;
    key.service_role = service_role;
    key.channel_name = _channel_name;
    key.endpoint = endpoint_;
    std::map<registered_service_key_t, registered_service_t>::iterator it =
      _registered_services.find (key);
    if (it != _registered_services.end ()) {
        it->second.weight = weight_;
        it->second.value = value_;
        it->second.metadata =
          metadata_ ? *metadata_ : std::vector<unsigned char> ();
        if (source_registry != 0)
            it->second.source_registry = source_registry;
        if (registration_id != 0)
            it->second.registration_id = registration_id;
    }
    return 0;
}

int discovery_t::unregister_service (const char *endpoint_,
                                     uint16_t service_role_)
{
    if (_channel_name.empty () || !endpoint_ || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    const uint16_t service_role =
      resolve_registered_service_role_local (service_role_);
    if (!discovery_protocol::auto_connect_type_allows_role (
          _auto_connect_type, service_role)) {
        errno = ENOTSUP;
        return -1;
    }

    std::string uplink;
    {
        scoped_lock_t lock (_sync);
        registered_service_key_t key;
        key.service_role = service_role;
        key.channel_name = _channel_name;
        key.endpoint = endpoint_;
        std::map<registered_service_key_t, registered_service_t>::const_iterator
          it = _registered_services.find (key);
        if (it != _registered_services.end ())
            uplink = it->second.uplink_endpoint;
    }
    if (uplink.empty ())
        (void) _uplink_runtime->latest_registry_uplink (this, &uplink);
    if (uplink.empty ()) {
        errno = EAGAIN;
        return -1;
    }

    socket_base_t *dealer = NULL;
    if (discovery_registry_rpc::prepare_transient_dealer (_ctx, _bootstrap_runtime, uplink, NULL,
                                        &dealer)
        != 0) {
        discovery_debugf ("unregister_service pollout timeout uplink=%s",
                                uplink.c_str ());
        return -1;
    }

    if (discovery_protocol::send_u16 (
          dealer, discovery_protocol::msg_unregister, ZLINK_SNDMORE)
          < 0
        || discovery_protocol::send_u16 (dealer, _auto_connect_type,
                                         ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_u16 (dealer, service_role, ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, _channel_name,
                                            ZLINK_SNDMORE)
             < 0
        || discovery_protocol::send_string (dealer, endpoint_, 0) < 0) {
        (void) discovery_registry_rpc::close_dealer (_ctx, dealer);
        return -1;
    }

    int status = -1;
    std::string error;
    if (discovery_registry_rpc::recv_status_ack (dealer, discovery_protocol::msg_unregister_ack,
                               &status, NULL, NULL, NULL, &error)
        != 0) {
        discovery_debugf (
          "unregister_service ack recv failed errno=%d", errno);
        (void) discovery_registry_rpc::close_dealer (_ctx, dealer);
        return -1;
    }
    (void) discovery_registry_rpc::close_dealer (_ctx, dealer);
    if (status != discovery_protocol::status_ok) {
        if (status == -1) {
            discovery_debugf (
              "unregister_service ack timeout uplink=%s", uplink.c_str ());
            errno = EAGAIN;
            return -1;
        }
        discovery_debugf (
          "unregister_service rejected status=%d error=%s", status,
          error.c_str ());
        errno = discovery_protocol::route_status_errno (
          static_cast<uint8_t> (status));
        return -1;
    }

    scoped_lock_t lock (_sync);
    registered_service_key_t key;
    key.service_role = service_role;
    key.channel_name = _channel_name;
    key.endpoint = endpoint_;
    _registered_services.erase (key);
    return 0;
}
}
