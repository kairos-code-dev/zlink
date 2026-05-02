/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"

#include "services/control/service_control_runtime.hpp"

#include <cstring>

namespace zlink
{
discovery_local_state_t::discovery_local_state_t () :
    _value (0),
    _route_value_max_size (ZLINK_ROUTE_VALUE_MAX)
{
}

int discovery_local_state_t::set_route_value_max_size (size_t value_)
{
    if (value_ == 0 || value_ > ZLINK_ROUTE_VALUE_MAX) {
        errno = EINVAL;
        return -1;
    }
    _route_value_max_size = value_;
    return 0;
}

int discovery_local_state_t::get_route_value_max_size (void *optval_,
                                                       size_t *optvallen_) const
{
    if (!optvallen_) {
        errno = EINVAL;
        return -1;
    }
    if (!optval_) {
        *optvallen_ = sizeof (size_t);
        return 0;
    }
    if (*optvallen_ < sizeof (size_t)) {
        *optvallen_ = sizeof (size_t);
        errno = ENOBUFS;
        return -1;
    }
    *static_cast<size_t *> (optval_) = _route_value_max_size;
    *optvallen_ = sizeof (size_t);
    return 0;
}

void discovery_local_state_t::set_value (int64_t value_)
{
    _value = value_;
}

int discovery_local_state_t::get_value (int64_t *value_out_) const
{
    if (!value_out_) {
        errno = EINVAL;
        return -1;
    }
    *value_out_ = _value;
    return 0;
}

void discovery_local_state_t::snapshot_registration (
  int64_t *value_out_) const
{
    if (value_out_)
        *value_out_ = _value;
}

int discovery_t::add_observer (discovery_observer_t *observer_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    scoped_lock_t lock (_sync);
    return _service_state.add_observer (observer_);
}

int discovery_t::set_option (int option_,
                             const void *optval_,
                             size_t optvallen_)
{
    if (option_ == ZLINK_OPT_ROUTE_VALUE_MAX_SIZE) {
        service_public_api_scope_t admission (_public_api);
        if (!admission.acquired ())
            return -1;
        if (!optval_ || optvallen_ != sizeof (size_t)) {
            errno = EINVAL;
            return -1;
        }
        const size_t value = *static_cast<const size_t *> (optval_);
        scoped_lock_t lock (_sync);
        return _local_state.set_route_value_max_size (value);
    }
    return _bootstrap_runtime->set_option (this, option_, optval_, optvallen_);
}

int discovery_t::get_option (int option_, void *optval_, size_t *optvallen_) const
{
    if (option_ != ZLINK_OPT_ROUTE_VALUE_MAX_SIZE) {
        errno = ENOTSUP;
        return -1;
    }

    service_public_api_scope_t admission (
      const_cast<service_public_api_guard_t &> (_public_api));
    if (!admission.acquired ())
        return -1;

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return _local_state.get_route_value_max_size (optval_, optvallen_);
}

int discovery_t::set_value (int64_t value_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    std::vector<registered_service_t> services;
    {
        scoped_lock_t lock (_sync);
        _local_state.set_value (value_);
    }
    snapshot_registered_service_updates (&services, NULL);
    if (propagate_registered_service_updates (services, value_) != 0)
        return -1;
    return 0;
}

int discovery_t::get_value (int64_t *value_out_) const
{
    service_public_api_scope_t admission (
      const_cast<service_public_api_guard_t &> (_public_api));
    if (!admission.acquired ())
        return -1;
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return _local_state.get_value (value_out_);
}

void discovery_t::snapshot_providers (const std::string &channel_name_,
                                      std::vector<provider_info_t> *out_)
{
    if (!out_)
        return;
    out_->clear ();
    if (channel_name_ != _channel_name)
        return;
    scoped_lock_t lock (_sync);
    _service_state.snapshot_providers (out_);
}

void discovery_t::snapshot_member_peers (
  std::vector<zlink_member_peer_entry_t> *out_) const
{
    if (!out_)
        return;
    out_->clear ();

    std::vector<provider_info_t> providers;
    std::set<discovery_member_key_t> local_members;
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        for (std::map<registered_service_key_t, registered_service_t>::const_iterator
               it = _registered_services.begin ();
             it != _registered_services.end (); ++it) {
            if (it->second.channel_name == _channel_name) {
                local_members.insert (
                  discovery_member_key_t (it->second.service_role,
                                          it->second.endpoint));
            }
        }
        _service_state.snapshot_member_peers (
          static_cast<zlink_auto_connect_type_t> (_auto_connect_type),
          local_members, out_);
    }
}

int discovery_t::member_peers (zlink_member_peer_entry_t *entries_,
                               size_t *count_) const
{
    service_public_api_scope_t admission (
      const_cast<service_public_api_guard_t &> (_public_api));
    if (!admission.acquired ())
        return -1;
    if (!count_) {
        errno = EINVAL;
        return -1;
    }

    std::vector<zlink_member_peer_entry_t> remote;
    snapshot_member_peers (&remote);
    if (!entries_) {
        *count_ = remote.size ();
        return 0;
    }
    if (*count_ < remote.size ()) {
        *count_ = remote.size ();
        errno = ENOBUFS;
        return -1;
    }
    for (size_t i = 0; i < remote.size (); ++i)
        entries_[i] = remote[i];
    *count_ = remote.size ();
    return 0;
}

uint64_t discovery_t::update_seq ()
{
    scoped_lock_t lock (_sync);
    return _service_state.update_seq ();
}

uint64_t discovery_t::service_update_seq (const std::string &channel_name_)
{
    if (channel_name_ != _channel_name)
        return 0;
    scoped_lock_t lock (_sync);
    return _service_state.service_update_seq ();
}

int discovery_t::remove_observer (discovery_observer_t *observer_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    scoped_lock_t lock (_sync);
    return _service_state.remove_observer (observer_);
}

void discovery_t::upsert_service_summary (
  const zlink_registry_topology_entry_t &entry_)
{
    if (entry_.routing_id.size == 0 || entry_.channel_name[0] == '\0')
        return;

    const topology_key_t key = make_summary_key (
      entry_.service_kind, entry_.service_role, entry_.routing_id,
      entry_.channel_name);

    {
        scoped_lock_t lock (_sync);
        store_summary_entry_locked (
          key, entry_, true, entry_.state == ZLINK_TOPOLOGY_STATE_STOPPED,
          _service_state.service_update_seq ());
    }

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _task_id != 0)
        runtime->wakeup_task (_task_id);
}

discovery_t::topology_key_t discovery_t::make_summary_key (
  uint16_t service_kind_,
  uint16_t service_role_,
  const zlink_routing_id_t &routing_id_,
  const std::string &channel_name_) const
{
    topology_key_t key;
    key.service_kind = service_kind_;
    key.service_role = service_role_;
    if (routing_id_.size > 0) {
        key.routing_id_key.assign (
          reinterpret_cast<const char *> (routing_id_.data), routing_id_.size);
    }
    key.channel_name = channel_name_;
    return key;
}

void discovery_t::store_summary_entry_locked (
  const topology_key_t &key_,
  const zlink_registry_topology_entry_t &entry_,
  bool dirty_,
  bool tombstone_,
  uint64_t validated_service_seq_)
{
    topology_summary_t &summary = _summary_store[key_];
    summary.entry = entry_;
    summary.dirty = dirty_;
    summary.tombstone = tombstone_;
    summary.validated_service_seq = validated_service_seq_;
}

int discovery_t::destroy ()
{
    if (!_public_api.begin_close_or_fail_busy ())
        return -1;
    {
        scoped_lock_t lock (_sync);
        if (_service_state.has_inflight_observer_callbacks ()) {
            _public_api.cancel_close ();
            errno = EBUSY;
            return -1;
        }
    }
    _stop.set (1);
    emit_ready_changed (0);
    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _task_id != 0)
        runtime->remove_task (_task_id);
    _task_id = 0;
    void *sub_socket = NULL;
    std::set<std::string> connected_endpoints;
    std::vector<std::pair<std::string, socket_base_t *> > bootstrap_dealers;
    std::vector<std::pair<std::string, socket_base_t *> > report_dealers;
    std::vector<std::pair<std::string, socket_base_t *> > control_dealers;
    std::vector<discovery_observer_t *> observers;
    {
        scoped_lock_t lock (_sync);
        sub_socket = _sub_socket;
        connected_endpoints = _connected_endpoints;
        _sub_socket = NULL;
        _connected_endpoints.clear ();
        _service_state.take_shutdown_observers (&observers);
        _registered_services.clear ();
        _summary_store.clear ();
    }
    for (size_t i = 0; i < observers.size (); ++i) {
        if (observers[i])
            observers[i]->on_discovery_shutdown_requested (this);
    }
    _bootstrap_runtime->take_shutdown_state (this, &bootstrap_dealers);
    _uplink_runtime->take_shutdown_state (this, &report_dealers,
                                          &control_dealers);

    if (sub_socket) {
        for (std::set<std::string>::const_iterator it =
               connected_endpoints.begin ();
             it != connected_endpoints.end (); ++it)
            zlink_disconnect (sub_socket, it->c_str ());
        socket_base_t *sub = static_cast<socket_base_t *> (sub_socket);
        sub->set_all_pipes_nodelay ();
        (void) _lifecycle.close_socket_and_wait (sub, 1000);
    }

    for (size_t i = 0; i < bootstrap_dealers.size (); ++i) {
        if (!bootstrap_dealers[i].second)
            continue;
        if (!bootstrap_dealers[i].first.empty ()) {
            zlink_disconnect (bootstrap_dealers[i].second,
                              bootstrap_dealers[i].first.c_str ());
        }
        bootstrap_dealers[i].second->set_all_pipes_nodelay ();
        (void) _lifecycle.close_socket_and_wait (bootstrap_dealers[i].second,
                                                 1000);
    }
    for (size_t i = 0; i < report_dealers.size (); ++i) {
        if (!report_dealers[i].second)
            continue;
        if (!report_dealers[i].first.empty ())
            zlink_disconnect (report_dealers[i].second,
                              report_dealers[i].first.c_str ());
        report_dealers[i].second->set_all_pipes_nodelay ();
        (void) _lifecycle.close_socket_and_wait (report_dealers[i].second, 1000);
    }
    for (size_t i = 0; i < control_dealers.size (); ++i) {
        if (!control_dealers[i].second)
            continue;
        if (!control_dealers[i].first.empty ())
            zlink_disconnect (control_dealers[i].second,
                              control_dealers[i].first.c_str ());
        control_dealers[i].second->set_all_pipes_nodelay ();
        (void) _lifecycle.close_socket_and_wait (control_dealers[i].second,
                                                 1000);
    }
    (void) _lifecycle.wait_drained (10000);

    for (size_t i = 0; i < observers.size (); ++i) {
        if (observers[i])
            observers[i]->on_discovery_destroyed (this);
    }
    return 0;
}

void discovery_t::control_task (void *arg_)
{
    discovery_t *self = static_cast<discovery_t *> (arg_);
    self->tick ();
}

int discovery_t::ensure_sub_socket ()
{
    scoped_lock_t lock (_sync);
    if (_sub_socket)
        return 0;

    void *sub = static_cast<void *> (_ctx->create_socket (ZLINK_CORE_SOCKET_SUB));
    if (!sub)
        return -1;
    _lifecycle.register_socket (static_cast<socket_base_t *> (sub));

    _bootstrap_runtime->apply_socket_options (
      static_cast<socket_base_t *> (sub));
    if (!_bootstrap_runtime->ensure_socket_routing_id (
          static_cast<socket_base_t *> (sub))) {
        socket_base_t *sub_socket = static_cast<socket_base_t *> (sub);
        (void) _lifecycle.close_socket (sub_socket);
        return -1;
    }
    static_cast<socket_base_t *> (sub)->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE,
                                                    "", 0);
    _sub_socket = sub;
    _connected_endpoints.clear ();
    return 0;
}

void discovery_t::close_sub_socket ()
{
    void *sub_socket = NULL;
    std::set<std::string> connected_endpoints;
    {
        scoped_lock_t lock (_sync);
        sub_socket = _sub_socket;
        connected_endpoints = _connected_endpoints;
        _sub_socket = NULL;
        _connected_endpoints.clear ();
    }

    if (!sub_socket)
        return;

    for (std::set<std::string>::const_iterator it = connected_endpoints.begin ();
         it != connected_endpoints.end (); ++it)
        zlink_disconnect (sub_socket, it->c_str ());
    socket_base_t *sub = static_cast<socket_base_t *> (sub_socket);
    (void) _lifecycle.close_socket (sub);
}
}
