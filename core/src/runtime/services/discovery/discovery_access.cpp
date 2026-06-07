/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/discovery_access.hpp"

#include <new>

#include "api/service/service_handle_internal.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"

namespace zlink
{
mutex_t &discovery_t::sync ()
{
    return _sync;
}

mutex_t &discovery_t::uplink_sync ()
{
    return _uplink_sync;
}

service_public_api_guard_t &discovery_t::public_api_guard ()
{
    return _public_api;
}

discovery_bootstrap_runtime_t *discovery_t::bootstrap_runtime ()
{
    return _bootstrap_runtime;
}

discovery_uplink_runtime_t *discovery_t::uplink_runtime ()
{
    return _uplink_runtime;
}

bool discovery_t::bootstrap_socket_config_locked (bool routing_id_locked_) const
{
    return routing_id_locked_ || _sub_socket || !_connected_endpoints.empty ()
           || _bootstrap_runtime->has_bootstrap_activity_locked ();
}

void discovery_t::apply_socket_option_to_sub_socket (int option_,
                                                     const void *optval_,
                                                     size_t optvallen_)
{
    if (_sub_socket)
        static_cast<socket_base_t *> (_sub_socket)->setsockopt (option_, optval_, optvallen_);
}

bool discovery_t::should_publish_summary_entry (const zlink_registry_topology_entry_t &entry_) const
{
    return should_publish_summary_entry_locked (entry_);
}

void discovery_t::collect_dirty_summary_entries (std::vector<zlink_registry_topology_entry_t> *out_)
{
    if (!out_)
        return;

    out_->clear ();
    for (std::map<topology_key_t, topology_summary_t>::iterator it = _summary_store.begin ();
         it != _summary_store.end (); ++it) {
        if (it->second.dirty && should_publish_summary_entry_locked (it->second.entry)) {
            out_->push_back (it->second.entry);
        }
    }
}

void discovery_t::mark_summary_entries_sent (
  const std::vector<zlink_registry_topology_entry_t> &entries_, const std::vector<bool> &sent_)
{
    for (size_t i = 0; i < entries_.size () && i < sent_.size (); ++i) {
        if (!sent_[i])
            continue;
        const zlink_registry_topology_entry_t &entry = entries_[i];
        const topology_key_t key = make_summary_key (entry.service_kind, entry.service_role,
                                                     entry.routing_id, entry.channel_name);
        std::map<topology_key_t, topology_summary_t>::iterator it = _summary_store.find (key);
        if (it != _summary_store.end ())
            it->second.dirty = false;
    }
}

void discovery_t::collect_registered_services_for_heartbeat (
  uint64_t now_ms_,
  uint32_t heartbeat_interval_ms_,
  std::vector<discovery_registered_service_snapshot_t> *out_) const
{
    if (!out_)
        return;

    out_->clear ();
    for (std::map<registered_service_key_t, registered_service_t>::const_iterator it =
           _registered_services.begin ();
         it != _registered_services.end (); ++it) {
        if (it->second.uplink_endpoint.empty ())
            continue;
        if (it->second.last_heartbeat_ms != 0
            && now_ms_ - it->second.last_heartbeat_ms < heartbeat_interval_ms_) {
            continue;
        }
        discovery_registered_service_snapshot_t service;
        service.service_role = it->second.service_role;
        service.channel_name = it->second.channel_name;
        service.endpoint = it->second.endpoint;
        service.uplink_endpoint = it->second.uplink_endpoint;
        out_->push_back (service);
    }
}

void discovery_t::mark_registered_service_heartbeat (
  const discovery_registered_service_snapshot_t &service_, uint64_t now_ms_)
{
    registered_service_key_t key;
    key.service_role = service_.service_role;
    key.channel_name = service_.channel_name;
    key.endpoint = service_.endpoint;

    std::map<registered_service_key_t, registered_service_t>::iterator it =
      _registered_services.find (key);
    if (it != _registered_services.end ())
        it->second.last_heartbeat_ms = now_ms_;
}

void *discovery_access_t::create (ctx_t *ctx_,
                                  zlink_auto_connect_type_t auto_connect_type_,
                                  const char *channel_name_)
{
    if (!discovery_protocol::is_valid_auto_connect_type (
          static_cast<uint16_t> (auto_connect_type_))) {
        errno = EINVAL;
        return NULL;
    }

    discovery_t *discovery = new (std::nothrow)
      discovery_t (ctx_, static_cast<uint16_t> (auto_connect_type_), channel_name_);
    if (!discovery) {
        errno = ENOMEM;
        return NULL;
    }
    return discovery;
}

discovery_t *discovery_access_t::from_handle (void *discovery_)
{
    if (!discovery_ || !is_registered_discovery_handle (discovery_)) {
        errno = EFAULT;
        return NULL;
    }

    discovery_t *discovery = static_cast<discovery_t *> (discovery_);
    if (!discovery->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return discovery;
}

int discovery_access_t::connect_registry (discovery_t *discovery_, const char *registry_endpoint_)
{
    return discovery_ ? discovery_->connect_registry (registry_endpoint_) : -1;
}

int discovery_access_t::set_tls_client (discovery_t *discovery_,
                                        const char *ca_cert_,
                                        const char *hostname_,
                                        int trust_system_)
{
    return discovery_ ? discovery_->set_tls_client (ca_cert_, hostname_, trust_system_) : -1;
}

int discovery_access_t::set_option (discovery_t *discovery_,
                                    int option_,
                                    const void *optval_,
                                    size_t optvallen_)
{
    return discovery_ ? discovery_->set_option (option_, optval_, optvallen_) : -1;
}

int discovery_access_t::get_option (discovery_t *discovery_,
                                    int option_,
                                    void *optval_,
                                    size_t *optvallen_)
{
    return discovery_ ? discovery_->get_option (option_, optval_, optvallen_) : -1;
}

int discovery_access_t::set_routing_id (discovery_t *discovery_, const void *data_, size_t size_)
{
    return discovery_ ? discovery_->set_routing_id (data_, size_) : -1;
}

int discovery_access_t::routing_id (discovery_t *discovery_, zlink_routing_id_t *out_)
{
    return discovery_ ? discovery_->routing_id (out_) : -1;
}

int discovery_access_t::set_value (discovery_t *discovery_, int64_t value_)
{
    return discovery_ ? discovery_->set_value (value_) : -1;
}

int discovery_access_t::get_value (discovery_t *discovery_, int64_t *value_out_)
{
    return discovery_ ? discovery_->get_value (value_out_) : -1;
}

int discovery_access_t::resolve_spot (discovery_t *discovery_,
                                      const zlink_routing_id_t *spot_rid_,
                                      zlink_spot_route_t *route_out_)
{
    return discovery_ ? discovery_->resolve_spot (spot_rid_, route_out_) : -1;
}

int discovery_access_t::bind_route (discovery_t *discovery_,
                                    zlink_route_kind_t kind_,
                                    const void *key_,
                                    size_t key_size_,
                                    const void *value_,
                                    size_t value_size_)
{
    return discovery_ ? discovery_->bind_route (kind_, key_, key_size_, value_, value_size_) : -1;
}

int discovery_access_t::unbind_route (discovery_t *discovery_,
                                      zlink_route_kind_t kind_,
                                      const void *key_,
                                      size_t key_size_)
{
    return discovery_ ? discovery_->unbind_route (kind_, key_, key_size_) : -1;
}

int discovery_access_t::resolve_route (discovery_t *discovery_,
                                       zlink_route_kind_t kind_,
                                       const void *key_,
                                       size_t key_size_,
                                       zlink_routing_id_t *owner_rid_out_,
                                       zlink_msg_t *value_out_)
{
    return discovery_
             ? discovery_->resolve_route (kind_, key_, key_size_, owner_rid_out_, value_out_)
             : -1;
}

int discovery_access_t::member_peers (discovery_t *discovery_,
                                      zlink_member_peer_entry_t *entries_,
                                      size_t *count_)
{
    return discovery_ ? discovery_->member_peers (entries_, count_) : -1;
}

int discovery_access_t::destroy (discovery_t *discovery_)
{
    return discovery_ ? discovery_->destroy () : -1;
}

void discovery_access_t::delete_handle (discovery_t *discovery_)
{
    erase_discovery_handle (discovery_);
    delete discovery_;
}

void discovery_access_t::set_summary_enabled (discovery_t *discovery_, bool enabled_)
{
    if (discovery_)
        discovery_->set_discovery_summary_enabled (enabled_);
}

int discovery_access_t::add_observer (discovery_t *discovery_, discovery_observer_t *observer_)
{
    return discovery_ ? discovery_->add_observer (observer_) : -1;
}

int discovery_access_t::remove_observer (discovery_t *discovery_, discovery_observer_t *observer_)
{
    return discovery_ ? discovery_->remove_observer (observer_) : 0;
}

void discovery_access_t::upsert_service_summary (discovery_t *discovery_,
                                                 const zlink_registry_topology_entry_t &entry_)
{
    if (discovery_)
        discovery_->upsert_service_summary (entry_);
}

void discovery_access_t::flush_topology_reports (discovery_t *discovery_)
{
    if (discovery_)
        discovery_->flush_topology_reports ();
}

mutex_t &discovery_access_t::sync (discovery_t *discovery_)
{
    return discovery_->sync ();
}

mutex_t &discovery_access_t::uplink_sync (discovery_t *discovery_)
{
    return discovery_->uplink_sync ();
}

service_public_api_guard_t &discovery_access_t::public_api_guard (discovery_t *discovery_)
{
    return discovery_->public_api_guard ();
}

bool discovery_access_t::bootstrap_socket_config_locked (discovery_t *discovery_,
                                                         bool routing_id_locked_)
{
    if (!discovery_)
        return true;
    return discovery_->bootstrap_socket_config_locked (routing_id_locked_);
}

void discovery_access_t::apply_socket_option_to_sub_socket (discovery_t *discovery_,
                                                            int option_,
                                                            const void *optval_,
                                                            size_t optvallen_)
{
    if (discovery_)
        discovery_->apply_socket_option_to_sub_socket (option_, optval_, optvallen_);
}

discovery_bootstrap_runtime_t *discovery_access_t::bootstrap_runtime (discovery_t *discovery_)
{
    return discovery_ ? discovery_->bootstrap_runtime () : NULL;
}

discovery_uplink_runtime_t *discovery_access_t::uplink_runtime (discovery_t *discovery_)
{
    return discovery_ ? discovery_->uplink_runtime () : NULL;
}

int discovery_access_t::ensure_control_task_active (discovery_t *discovery_)
{
    return discovery_ ? discovery_->ensure_control_task_active () : -1;
}

void discovery_access_t::emit_ready_changed (discovery_t *discovery_, uint32_t ready_count_)
{
    if (discovery_)
        discovery_->emit_ready_changed (ready_count_);
}

int discovery_access_t::ensure_topology_reporters (discovery_t *discovery_)
{
    return discovery_ ? discovery_->ensure_topology_reporters () : -1;
}

socket_base_t *discovery_access_t::create_tracked_socket (discovery_t *discovery_, int socket_type_)
{
    return discovery_ ? discovery_->create_tracked_socket (socket_type_) : NULL;
}

int discovery_access_t::close_tracked_socket (discovery_t *discovery_,
                                              socket_base_t *&socket_,
                                              int timeout_ms_)
{
    return discovery_ ? discovery_->close_tracked_socket (socket_, timeout_ms_) : -1;
}

int discovery_access_t::close_tracked_socket_and_wait (discovery_t *discovery_,
                                                       socket_base_t *&socket_,
                                                       int timeout_ms_)
{
    return discovery_ ? discovery_->close_tracked_socket_and_wait (socket_, timeout_ms_) : -1;
}

bool discovery_access_t::should_publish_summary_entry (
  discovery_t *discovery_, const zlink_registry_topology_entry_t &entry_)
{
    return discovery_ && discovery_->should_publish_summary_entry (entry_);
}

void discovery_access_t::collect_dirty_summary_entries (
  discovery_t *discovery_, std::vector<zlink_registry_topology_entry_t> *out_)
{
    if (!out_)
        return;
    out_->clear ();
    if (!discovery_)
        return;
    discovery_->collect_dirty_summary_entries (out_);
}

void discovery_access_t::mark_summary_entries_sent (
  discovery_t *discovery_,
  const std::vector<zlink_registry_topology_entry_t> &entries_,
  const std::vector<bool> &sent_)
{
    if (!discovery_)
        return;
    discovery_->mark_summary_entries_sent (entries_, sent_);
}

void discovery_access_t::collect_registered_services_for_heartbeat (
  discovery_t *discovery_,
  uint64_t now_ms_,
  uint32_t heartbeat_interval_ms_,
  std::vector<discovery_registered_service_snapshot_t> *out_)
{
    if (!out_)
        return;
    out_->clear ();
    if (!discovery_)
        return;
    discovery_->collect_registered_services_for_heartbeat (now_ms_, heartbeat_interval_ms_, out_);
}

void discovery_access_t::mark_registered_service_heartbeat (
  discovery_t *discovery_,
  const discovery_registered_service_snapshot_t &service_,
  uint64_t now_ms_)
{
    if (!discovery_)
        return;
    discovery_->mark_registered_service_heartbeat (service_, now_ms_);
}
}
