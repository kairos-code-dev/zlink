/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"

#include "services/control/service_control_runtime.hpp"

#include <cstring>

namespace zlink
{
namespace
{
static int init_msg_from_blob_local (const std::vector<unsigned char> &blob_,
                                     zlink_msg_t *metadata_out_)
{
    if (!metadata_out_) {
        errno = EINVAL;
        return -1;
    }
    if (zlink_msg_init_size (metadata_out_, blob_.size ()) != 0)
        return -1;
    if (!blob_.empty ())
        memcpy (zlink_msg_data (metadata_out_), &blob_[0], blob_.size ());
    return 0;
}

static std::string topology_routing_key_local (const zlink_routing_id_t &rid_)
{
    if (rid_.size == 0)
        return std::string ();
    return std::string (reinterpret_cast<const char *> (rid_.data), rid_.size);
}

static zlink_service_type_t public_service_type_local (uint16_t service_type_)
{
    return service_type_ == discovery_protocol::service_type_spot_node
             ? ZLINK_SERVICE_TYPE_SPOT
             : ZLINK_SERVICE_TYPE_SOCKET;
}
}

discovery_local_state_t::discovery_local_state_t () :
    _value (0),
    _metadata_max_size (4096)
{
}

int discovery_local_state_t::set_metadata_max_size (size_t value_)
{
    if (value_ == 0) {
        errno = EINVAL;
        return -1;
    }
    _metadata_max_size = value_;
    return 0;
}

int discovery_local_state_t::get_metadata_max_size (void *optval_,
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
    *static_cast<size_t *> (optval_) = _metadata_max_size;
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

int discovery_local_state_t::set_metadata (const void *data_, size_t size_)
{
    if (size_ > _metadata_max_size) {
        errno = EMSGSIZE;
        return -1;
    }
    if (size_ != 0 && !data_) {
        errno = EINVAL;
        return -1;
    }

    _metadata.clear ();
    if (size_ != 0) {
        const unsigned char *begin =
          static_cast<const unsigned char *> (data_);
        _metadata.assign (begin, begin + size_);
    }
    return 0;
}

int discovery_local_state_t::get_metadata (zlink_msg_t *metadata_out_) const
{
    return init_msg_from_blob_local (_metadata, metadata_out_);
}

void discovery_local_state_t::snapshot_registration (
  int64_t *value_out_,
  std::vector<unsigned char> *metadata_out_) const
{
    if (value_out_)
        *value_out_ = _value;
    if (metadata_out_)
        *metadata_out_ = _metadata;
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
    if (option_ == ZLINK_OPT_DISCOVERY_METADATA_MAX_SIZE) {
        service_public_api_scope_t admission (_public_api);
        if (!admission.acquired ())
            return -1;
        if (!optval_ || optvallen_ != sizeof (size_t)) {
            errno = EINVAL;
            return -1;
        }
        const size_t value = *static_cast<const size_t *> (optval_);
        scoped_lock_t lock (_sync);
        return _local_state.set_metadata_max_size (value);
    }
    return _bootstrap_runtime->set_option (this, option_, optval_, optvallen_);
}

int discovery_t::get_option (int option_, void *optval_, size_t *optvallen_) const
{
    if (option_ != ZLINK_OPT_DISCOVERY_METADATA_MAX_SIZE) {
        errno = ENOTSUP;
        return -1;
    }

    service_public_api_scope_t admission (
      const_cast<service_public_api_guard_t &> (_public_api));
    if (!admission.acquired ())
        return -1;

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return _local_state.get_metadata_max_size (optval_, optvallen_);
}

int discovery_t::set_value (int64_t value_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    std::vector<registered_service_t> services;
    std::vector<unsigned char> metadata;
    {
        scoped_lock_t lock (_sync);
        _local_state.set_value (value_);
        _local_state.snapshot_registration (NULL, &metadata);
        for (std::map<registered_service_key_t, registered_service_t>::const_iterator
               it = _registered_services.begin ();
             it != _registered_services.end (); ++it)
            services.push_back (it->second);
    }

    for (size_t i = 0; i < services.size (); ++i) {
        if (update_service_attributes (services[i].service_type,
                                       services[i].service_name.c_str (),
                                       services[i].endpoint.c_str (), value_,
                                       &metadata, services[i].service_role)
            != 0) {
            return -1;
        }
    }
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

int discovery_t::set_metadata (const void *data_, size_t size_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    std::vector<registered_service_t> services;
    std::vector<unsigned char> metadata;
    int64_t value = 0;
    {
        scoped_lock_t lock (_sync);
        if (_local_state.set_metadata (data_, size_) != 0)
            return -1;
        _local_state.snapshot_registration (&value, &metadata);
        for (std::map<registered_service_key_t, registered_service_t>::const_iterator
               it = _registered_services.begin ();
             it != _registered_services.end (); ++it)
            services.push_back (it->second);
    }

    for (size_t i = 0; i < services.size (); ++i) {
        if (update_service_attributes (services[i].service_type,
                                       services[i].service_name.c_str (),
                                       services[i].endpoint.c_str (), value,
                                       &metadata, services[i].service_role)
            != 0) {
            return -1;
        }
    }
    return 0;
}

int discovery_t::get_metadata (zlink_msg_t *metadata_out_) const
{
    service_public_api_scope_t admission (
      const_cast<service_public_api_guard_t &> (_public_api));
    if (!admission.acquired ())
        return -1;
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    return _local_state.get_metadata (metadata_out_);
}

void discovery_t::snapshot_providers (const std::string &service_name_,
                                      std::vector<provider_info_t> *out_)
{
    if (!out_)
        return;
    out_->clear ();
    if (service_name_ != _service_name)
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
            if (it->second.service_type == _service_type
                && it->second.service_name == _service_name) {
                local_members.insert (
                  discovery_member_key_t (it->second.service_role,
                                          it->second.endpoint));
            }
        }
        _service_state.snapshot_member_peers (
          public_service_type_local (_service_type), local_members, out_);
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

int discovery_t::member_peer_metadata (uint16_t service_role_,
                                       const char *endpoint_,
                                       zlink_msg_t *metadata_out_) const
{
    service_public_api_scope_t admission (
      const_cast<service_public_api_guard_t &> (_public_api));
    if (!admission.acquired ())
        return -1;
    if (!endpoint_ || endpoint_[0] == '\0' || !metadata_out_) {
        errno = EINVAL;
        return -1;
    }

    std::vector<unsigned char> metadata;
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        std::set<discovery_member_key_t> local_members;
        for (std::map<registered_service_key_t, registered_service_t>::const_iterator
               it = _registered_services.begin ();
             it != _registered_services.end (); ++it) {
            if (it->second.service_type == _service_type
                && it->second.service_name == _service_name) {
                local_members.insert (
                  discovery_member_key_t (it->second.service_role,
                                          it->second.endpoint));
            }
        }
        if (!_service_state.copy_member_peer_metadata (
              local_members, service_role_, endpoint_, &metadata)) {
            errno = ENOENT;
            return -1;
        }
    }
    return init_msg_from_blob_local (metadata, metadata_out_);
}

uint64_t discovery_t::update_seq ()
{
    scoped_lock_t lock (_sync);
    return _service_state.update_seq ();
}

uint64_t discovery_t::service_update_seq (const std::string &service_name_)
{
    if (service_name_ != _service_name)
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
    if (entry_.routing_id.size == 0 || entry_.service_name[0] == '\0')
        return;

    topology_key_t key;
    key.service_kind = entry_.service_kind;
    key.service_role = entry_.service_role;
    key.routing_id_key = topology_routing_key_local (entry_.routing_id);
    key.service_name = entry_.service_name;

    {
        scoped_lock_t lock (_sync);
        topology_summary_t &summary = _summary_store[key];
        summary.entry = entry_;
        summary.dirty = true;
        summary.tombstone = entry_.state == ZLINK_TOPOLOGY_STATE_STOPPED;
    }

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _task_id != 0)
        runtime->wakeup_task (_task_id);
}

void discovery_t::erase_service_summary (uint16_t service_kind_,
                                         const zlink_routing_id_t &routing_id_,
                                         const std::string &service_name_,
                                         bool stopped_)
{
    if (routing_id_.size == 0 || service_name_.empty ())
        return;

    topology_key_t key;
    key.service_kind = service_kind_;
    key.service_role = ZLINK_SERVICE_ROLE_INVALID;
    key.routing_id_key = topology_routing_key_local (routing_id_);
    key.service_name = service_name_;

    {
        scoped_lock_t lock (_sync);
        if (!stopped_) {
            _summary_store.erase (key);
        } else {
            topology_summary_t &summary = _summary_store[key];
            memset (&summary.entry, 0, sizeof (summary.entry));
            summary.entry.service_kind =
              static_cast<zlink_service_kind_t> (service_kind_);
            summary.entry.service_role = ZLINK_SERVICE_ROLE_INVALID;
            summary.entry.routing_id = routing_id_;
            summary.entry.state = ZLINK_TOPOLOGY_STATE_STOPPED;
            summary.entry.source = ZLINK_TOPOLOGY_SOURCE_DISCOVERY;
            strncpy (summary.entry.service_name, service_name_.c_str (),
                     sizeof (summary.entry.service_name) - 1);
            summary.dirty = true;
            summary.tombstone = true;
        }
    }

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _task_id != 0)
        runtime->wakeup_task (_task_id);
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
    zlink_service_event_t terminal;
    memset (&terminal, 0, sizeof (terminal));
    terminal.service_kind = ZLINK_SERVICE_KIND_DISCOVERY;
    terminal.event_type = ZLINK_MONITOR_EVENT_CLOSED;
    terminal.detail_flags = ZLINK_EVENT_DETAIL_SUBJECT_RID;
    terminal.routing_id = _bootstrap_runtime->routing_id_value ();
    _monitor.close_all (&terminal);
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
