/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"

#include "services/control/service_control_runtime.hpp"

#include <cstring>

namespace zlink
{
namespace
{
static std::string topology_routing_key_local (const zlink_routing_id_t &rid_)
{
    if (rid_.size == 0)
        return std::string ();
    return std::string (reinterpret_cast<const char *> (rid_.data), rid_.size);
}
}

void discovery_t::add_observer (discovery_observer_t *observer_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return;
    if (!observer_)
        return;
    scoped_lock_t lock (_sync);
    _observers.insert (observer_);
}

int discovery_t::remove_observer (discovery_observer_t *observer_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;
    if (!observer_)
        return 0;
    scoped_lock_t lock (_sync);
    if (_observer_callbacks_inflight > 0) {
        errno = EBUSY;
        return -1;
    }
    _observers.erase (observer_);
    return 0;
}

void discovery_t::upsert_service_summary (
  const zlink_registry_topology_entry_t &entry_)
{
    if (entry_.routing_id.size == 0 || entry_.service_name[0] == '\0')
        return;

    topology_key_t key;
    key.service_kind = entry_.service_kind;
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

void discovery_t::upsert_gateway_peer_summary (
  const zlink_registry_gateway_peer_entry_t &entry_)
{
    if (entry_.gateway_routing_id.size == 0 || entry_.peer_routing_id.size == 0
        || entry_.service_name[0] == '\0') {
        return;
    }

    gateway_peer_key_t key;
    key.gateway_routing_id_key =
      topology_routing_key_local (entry_.gateway_routing_id);
    key.service_name = entry_.service_name;
    key.peer_routing_id_key = topology_routing_key_local (entry_.peer_routing_id);

    {
        scoped_lock_t lock (_sync);
        gateway_peer_summary_t &summary = _gateway_peer_summary_store[key];
        summary.entry = entry_;
        summary.dirty = true;
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
        if (_observer_callbacks_inflight > 0) {
            _public_api.cancel_close ();
            errno = EBUSY;
            return -1;
        }
        _destroying = true;
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
        observers.assign (_observers.begin (), _observers.end ());
        _observers.clear ();
        _observer_callbacks_inflight = 0;
        _registered_services.clear ();
        _summary_store.clear ();
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
