/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/gateway/gateway.hpp"

#include "services/common/monitor_decode.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/gateway/gateway_runtime.hpp"

#include <cerrno>
#include <cstring>

namespace zlink
{
namespace
{
static bool routing_id_equals_local (const zlink_routing_id_t &a_,
                                     const zlink_routing_id_t &b_)
{
    if (a_.size != b_.size || a_.size == 0)
        return false;
    return memcmp (a_.data, b_.data, a_.size) == 0;
}

static uint32_t count_ready_for_service_local (
  const std::string &service_name_,
  const std::map<std::string, std::string> &endpoint_to_service_,
  const std::set<std::string> &ready_endpoints_)
{
    uint32_t count = 0;
    for (std::set<std::string>::const_iterator it = ready_endpoints_.begin ();
         it != ready_endpoints_.end (); ++it) {
        std::map<std::string, std::string>::const_iterator fit =
          endpoint_to_service_.find (*it);
        if (fit != endpoint_to_service_.end () && fit->second == service_name_)
            ++count;
    }
    return count;
}

static uint64_t gateway_peer_report_interval_ms_local ()
{
    return 1000;
}

static std::string gateway_peer_key_local (const std::string &service_name_,
                                           const zlink_routing_id_t &rid_)
{
    if (service_name_.empty () || rid_.size == 0)
        return std::string ();

    std::string key = service_name_;
    key.push_back ('\0');
    key.append (reinterpret_cast<const char *> (rid_.data), rid_.size);
    return key;
}
}

void gateway_t::report_gateway_peer (const std::string &service_name_,
                                     const std::string &peer_endpoint_,
                                     const zlink_routing_id_t &peer_routing_id_,
                                     uint32_t weight_,
                                     uint16_t state_,
                                     uint64_t connected_since_ms)
{
    if (!_discovery || service_name_.empty () || _routing_id.size == 0
        || peer_routing_id_.size == 0) {
        return;
    }

    zlink_registry_gateway_peer_entry_t entry;
    memset (&entry, 0, sizeof (entry));
    entry.gateway_routing_id = _routing_id;
    entry.peer_routing_id = peer_routing_id_;
    entry.state = static_cast<zlink_topology_state_t> (state_);
    entry.weight = weight_;
    entry.connected_since_ms = connected_since_ms;
    entry.last_reported_ms = _runtime->clock.now_ms ();
    strncpy (entry.service_name, service_name_.c_str (),
             sizeof (entry.service_name) - 1);
    if (!_bind_endpoint.empty ())
        strncpy (entry.gateway_endpoint, _bind_endpoint.c_str (),
                 sizeof (entry.gateway_endpoint) - 1);
    if (!peer_endpoint_.empty ())
        strncpy (entry.peer_endpoint, peer_endpoint_.c_str (),
                 sizeof (entry.peer_endpoint) - 1);
    _discovery->upsert_gateway_peer_summary (entry);
}

void gateway_t::sync_gateway_peer_reports (uint64_t now_ms_)
{
    if (!_discovery || _runtime->ready_peer_reports.empty ())
        return;
    if (_runtime->next_gateway_peer_report_ms != 0
        && now_ms_ < _runtime->next_gateway_peer_report_ms) {
        return;
    }

    for (std::map<std::string, gateway_runtime_t::gateway_peer_report_t>::const_iterator
           it = _runtime->ready_peer_reports.begin ();
         it != _runtime->ready_peer_reports.end (); ++it) {
        report_gateway_peer (it->second.service_name, it->second.peer_endpoint,
                             it->second.peer_routing_id, it->second.weight,
                             ZLINK_TOPOLOGY_STATE_READY,
                             it->second.connected_since_ms);
    }
    _runtime->next_gateway_peer_report_ms =
      now_ms_ + gateway_peer_report_interval_ms_local ();
}

void *gateway_t::monitor_open (int events_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return NULL;

    {
        scoped_lock_t lock (_sync);
        if (ensure_refresh_task_running () != 0)
            return NULL;
    }
    return _monitor.open (events_);
}

void gateway_t::emit_event (uint32_t event_type_,
                            const std::string &service_name_,
                            const std::string &endpoint_,
                            const zlink_routing_id_t *routing_id_,
                            uint32_t value_,
                            int32_t error_code_)
{
    zlink_service_event_t ev;
    fill_event (&ev, event_type_, service_name_, endpoint_, routing_id_, value_,
                error_code_);
    emit_events (&ev, 1);
}

void gateway_t::fill_event (zlink_service_event_t *out_,
                            uint32_t event_type_,
                            const std::string &service_name_,
                            const std::string &endpoint_,
                            const zlink_routing_id_t *routing_id_,
                            uint32_t value_,
                            int32_t error_code_) const
{
    if (!out_)
        return;

    memset (out_, 0, sizeof (*out_));
    out_->service_kind = ZLINK_SERVICE_KIND_GATEWAY;
    out_->event_type = event_type_;
    out_->error_code = error_code_;
    out_->value = value_;
    if (!service_name_.empty ()) {
        out_->detail_flags |= ZLINK_EVENT_DETAIL_SERVICE_NAME;
        strncpy (out_->service_name, service_name_.c_str (),
                 sizeof (out_->service_name) - 1);
    }
    if (!endpoint_.empty ()) {
        out_->detail_flags |= ZLINK_EVENT_DETAIL_ENDPOINT;
        strncpy (out_->endpoint, endpoint_.c_str (), sizeof (out_->endpoint) - 1);
    }
    if (routing_id_ && routing_id_->size > 0) {
        out_->detail_flags |= ZLINK_EVENT_DETAIL_PEER_RID;
        out_->routing_id = *routing_id_;
    } else if (_routing_id.size > 0) {
        out_->detail_flags |= ZLINK_EVENT_DETAIL_SUBJECT_RID;
        out_->routing_id = _routing_id;
    }
}

void gateway_t::emit_events (const zlink_service_event_t *events_, size_t count_)
{
    if (!events_ || count_ == 0)
        return;

    _monitor.emit_batch (events_, count_);
    for (size_t i = 0; i < count_; ++i) {
        const zlink_service_event_t &ev = events_[i];
        uint16_t state = 0;
        switch (ev.event_type) {
            case ZLINK_GATEWAY_MONITOR_EVENT_READY_CHANGED:
                state = ev.value > 0 ? ZLINK_TOPOLOGY_STATE_READY
                                     : ZLINK_TOPOLOGY_STATE_LOST;
                break;
            case ZLINK_GATEWAY_ROUTE_UP:
            case ZLINK_GATEWAY_ROUTE_DOWN:
                state = ev.value > 0 ? ZLINK_TOPOLOGY_STATE_READY
                                     : ZLINK_TOPOLOGY_STATE_CONNECTING;
                break;
            default:
                break;
        }
        if (state != 0)
            report_topology (ev.service_name, ev.endpoint, state, ev.value,
                             ev.error_code);
    }
}

void gateway_t::report_topology (const std::string &service_name_,
                                 const std::string &endpoint_,
                                 uint16_t state_,
                                 uint32_t ready_count_,
                                 int32_t error_code_)
{
    if (!_discovery || service_name_.empty () || _routing_id.size == 0)
        return;

    std::vector<provider_info_t> providers;
    _discovery->snapshot_providers (service_name_, &providers);

    zlink_registry_topology_entry_t entry;
    memset (&entry, 0, sizeof (entry));
    entry.routing_id = _routing_id;
    entry.service_kind = ZLINK_SERVICE_KIND_GATEWAY;
    entry.service_role = ZLINK_SERVICE_ROLE_GATEWAY;
    strncpy (entry.service_name, service_name_.c_str (),
             sizeof (entry.service_name) - 1);
    if (!endpoint_.empty ())
        strncpy (entry.endpoint, endpoint_.c_str (),
                 sizeof (entry.endpoint) - 1);
    entry.source = ZLINK_TOPOLOGY_SOURCE_DISCOVERY;
    entry.state = static_cast<zlink_topology_state_t> (state_);
    entry.desired_count = static_cast<uint32_t> (providers.size ());
    entry.ready_count = ready_count_;
    entry.error_code = static_cast<uint32_t> (error_code_);
    entry.last_reported_ms = _runtime->clock.now_ms ();
    _discovery->upsert_service_summary (entry);
}

void gateway_t::on_service_update (const std::string &service_name_)
{
    if (_runtime->stop.get () != 0)
        return;
    if (!_service_name.empty () && service_name_ != _service_name)
        return;
    scoped_lock_t lock (_sync);
    if (!service_name_.empty ()) {
        _runtime->pending_updates.insert (service_name_);
        std::map<std::string, gateway_service_pool_t>::iterator pit =
          _runtime->pools.find (service_name_);
        if (pit != _runtime->pools.end ())
            pit->second.dirty = true;
    }
    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _runtime->refresh_task_id != 0)
        runtime->wakeup_task (_runtime->refresh_task_id);
}

void gateway_t::on_discovery_destroyed (discovery_t *discovery_)
{
    scoped_lock_t lock (_sync);
    if (_discovery != discovery_)
        return;
    _discovery = NULL;
    _runtime->pending_updates.clear ();
    _runtime->force_refresh_all = false;
    _advertise_endpoint.clear ();
    _last_register_error.clear ();
    _summary_last_changed_ms = _runtime ? _runtime->clock.now_ms () : 0;
}

void gateway_t::on_discovery_shutdown_requested (discovery_t *discovery_)
{
    if (_discovery != discovery_)
        return;
    _public_api.mark_closing ();
    (void) destroy ();
}

int gateway_t::fill_monitor_snapshot (zlink_monitor_snapshot_t *out_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    memset (out_, 0, sizeof (*out_));
    out_->source_kind = ZLINK_MONITOR_SOURCE_GATEWAY;
    out_->detail_flags = ZLINK_MONITOR_SNAPSHOT_DETAIL_READY_COUNT;
    socket_base_t *router_socket = NULL;
    bool is_bound_ready = false;
    std::shared_ptr<const gateway_service_pool_t::send_snapshot_t> snapshot;

    {
        scoped_lock_t lock (_sync);
        if (ensure_facade_mode () != 0)
            return -1;
        lock_routing_id ();
        process_monitor_events ();
        is_bound_ready = !_bind_endpoint.empty ();
        gateway_service_pool_t *pool = get_or_create_pool_cached ();
        {
            scoped_lock_t send_lock (_send_sync);
            if (pool)
                snapshot = pool->send_snapshot;
        }
        router_socket = _runtime->router_socket;
    }

    out_->state_flags = is_bound_ready ? ZLINK_MONITOR_STATE_BOUND_READY : 0;
    out_->ready_count =
      snapshot ? static_cast<uint32_t> (snapshot->endpoints.size ()) : 0;
    if (out_->ready_count > 0)
        out_->state_flags |=
          ZLINK_MONITOR_STATE_READY | ZLINK_MONITOR_STATE_SEND_READY;
    if (router_socket) {
        zlink_monitor_snapshot_t router_snapshot;
        if (router_socket->monitor_snapshot (&router_snapshot) == 0) {
            out_->snd_pending_msgs = router_snapshot.snd_pending_msgs;
            out_->rcv_pending_msgs = router_snapshot.rcv_pending_msgs;
            out_->detail_flags |= ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS
                                  | ZLINK_MONITOR_SNAPSHOT_DETAIL_RCV_PENDING_MSGS;
        }
    }
    return 0;
}

int gateway_t::snapshot_status (zlink_gateway_status_t *out_) const
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    memset (out_, 0, sizeof (*out_));

    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (const_cast<gateway_t *> (this)->ensure_facade_mode () != 0)
        return -1;

    if (!_service_name.empty ()) {
        strncpy (out_->service_name, _service_name.c_str (),
                 sizeof (out_->service_name) - 1);
    }
    const std::string bind_endpoint =
      !_advertise_endpoint.empty () ? _advertise_endpoint : _bind_endpoint;
    if (!bind_endpoint.empty ()) {
        strncpy (out_->bind_endpoint, bind_endpoint.c_str (),
                 sizeof (out_->bind_endpoint) - 1);
    }
    out_->gateway_routing_id = _routing_id;

    const gateway_service_pool_t *pool = NULL;
    if (_runtime->primary_pool)
        pool = _runtime->primary_pool;
    else {
        std::map<std::string, gateway_service_pool_t>::const_iterator it =
          _runtime->pools.find (_service_name);
        if (it != _runtime->pools.end ())
            pool = &it->second;
    }

    const gateway_service_pool_t::control_snapshot_t *control =
      pool && pool->control_snapshot ? pool->control_snapshot.get () : NULL;
    out_->observed_provider_count =
      control ? static_cast<uint32_t> (control->routes_by_endpoint.size ()) : 0;

    {
        scoped_lock_t send_lock (const_cast<mutex_t &> (_send_sync));
        const gateway_service_pool_t::send_snapshot_t *send =
          pool && pool->send_snapshot ? pool->send_snapshot.get () : NULL;
        out_->ready_provider_count =
          send ? static_cast<uint32_t> (send->endpoints.size ()) : 0;
    }

    out_->active_route_count = out_->ready_provider_count;
    out_->send_ready = out_->ready_provider_count > 0 ? 1U : 0U;
    out_->last_error = _last_summary_error;
    out_->last_changed_ms = _summary_last_changed_ms;

    if (out_->last_error != 0)
        out_->state = ZLINK_GATEWAY_STATE_ERROR;
    else if (out_->bind_endpoint[0] == '\0' && out_->observed_provider_count == 0)
        out_->state = ZLINK_GATEWAY_STATE_IDLE;
    else if (out_->observed_provider_count > 0 && out_->ready_provider_count == 0)
        out_->state = ZLINK_GATEWAY_STATE_CONNECTING;
    else if (out_->ready_provider_count > 0
             && out_->ready_provider_count < out_->observed_provider_count) {
        out_->state = ZLINK_GATEWAY_STATE_PARTIAL_READY;
    } else if (out_->ready_provider_count > 0
               && out_->ready_provider_count == out_->observed_provider_count) {
        out_->state = ZLINK_GATEWAY_STATE_READY;
    } else
        out_->state = ZLINK_GATEWAY_STATE_IDLE;

    return 0;
}

void gateway_t::emit_route_deltas (
  const std::vector<gateway_route_delta_t> &deltas_)
{
    if (deltas_.empty ())
        return;

    std::vector<zlink_service_event_t> events (deltas_.size ());
    for (size_t i = 0; i < deltas_.size (); ++i) {
        fill_event (&events[i], deltas_[i].event_type, deltas_[i].service_name,
                    deltas_[i].endpoint,
                    deltas_[i].routing_id.size != 0 ? &deltas_[i].routing_id
                                                    : NULL,
                    deltas_[i].ready_count, deltas_[i].error_code);
    }
    emit_events (&events[0], events.size ());
}

void gateway_t::process_monitor_events ()
{
    if (!_runtime->monitor_socket)
        return;
    const size_t max_events_per_cycle = 64;
    size_t processed = 0;
    while (processed < max_events_per_cycle) {
        zlink_monitor_event_t event;
        const int rc =
          recv_socket_monitor_event (_runtime->monitor_socket, &event, ZLINK_DONTWAIT);
        if (rc != 0) {
            if (errno == EAGAIN)
                return;
            return;
        }
        ++processed;
        const std::string endpoint = event.remote_addr;
        if (endpoint.empty ())
            continue;
        std::string service_name;
        if (event.event == ZLINK_EVENT_CONNECTION_READY_CHANGED) {
            _runtime->down_endpoints.erase (endpoint);
            _runtime->down_until_ms.erase (endpoint);
            _runtime->ready_endpoints.insert (endpoint);
            _runtime->inflight_endpoints.erase (endpoint);
            _runtime->inflight_rid_by_endpoint.erase (endpoint);
            _summary_last_changed_ms = _runtime->clock.now_ms ();
        } else if (event.event == ZLINK_EVENT_DISCONNECTED
                   || event.event == ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
                   || event.event == ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
                   || event.event == ZLINK_EVENT_HANDSHAKE_FAILED_AUTH) {
            _runtime->inflight_endpoints.erase (endpoint);
            _runtime->inflight_rid_by_endpoint.erase (endpoint);
            _runtime->ready_endpoints.erase (endpoint);
            _runtime->down_endpoints.insert (endpoint);
            _runtime->down_until_ms[endpoint] = _runtime->clock.now_ms () + 500;
            _last_summary_error = static_cast<int32_t> (event.event);
            _summary_last_changed_ms = _runtime->clock.now_ms ();
        }
        std::map<std::string, std::string>::iterator it =
          _runtime->endpoint_to_service.find (endpoint);
        if (it != _runtime->endpoint_to_service.end ()) {
            service_name = it->second;
            std::map<std::string, gateway_service_pool_t>::iterator pit =
              _runtime->pools.find (it->second);
            if (pit != _runtime->pools.end ()) {
                pit->second.dirty = true;
                _runtime->pending_updates.insert (it->second);
            }
        } else {
            _runtime->force_refresh_all = true;
        }

        if (!service_name.empty ()) {
            const uint32_t ready_count =
              count_ready_for_service_local (service_name,
                                             _runtime->endpoint_to_service,
                                             _runtime->ready_endpoints);
            std::vector<gateway_route_delta_t> deltas;
            if (event.event == ZLINK_EVENT_CONNECTION_READY_CHANGED) {
                const std::string peer_key =
                  gateway_peer_key_local (service_name, event.routing_id);
                if (!peer_key.empty ()) {
                    gateway_runtime_t::gateway_peer_report_t &report =
                      _runtime->ready_peer_reports[peer_key];
                    if (report.connected_since_ms == 0)
                        report.connected_since_ms = _runtime->clock.now_ms ();
                    report.service_name = service_name;
                    report.peer_endpoint = endpoint;
                    report.peer_routing_id = event.routing_id;
                    report.weight = 0;
                    std::map<std::string, gateway_service_pool_t>::iterator pit =
                      _runtime->pools.find (service_name);
                    if (pit != _runtime->pools.end ()) {
                        scoped_lock_t send_lock (_send_sync);
                        const gateway_service_pool_t::send_snapshot_t *snapshot =
                          pit->second.send_snapshot.get ();
                        if (!snapshot)
                            continue;
                        for (size_t i = 0; i < snapshot->routing_ids.size ();
                             ++i) {
                            if (routing_id_equals_local (
                                  snapshot->routing_ids[i], event.routing_id)) {
                                if (i < snapshot->weights.size ())
                                    report.weight = snapshot->weights[i];
                                if (i < snapshot->endpoints.size ())
                                    report.peer_endpoint =
                                      snapshot->endpoints[i];
                                break;
                            }
                        }
                    }
                    report_gateway_peer (service_name, report.peer_endpoint,
                                         report.peer_routing_id, report.weight,
                                         ZLINK_TOPOLOGY_STATE_READY,
                                         report.connected_since_ms);
                    _runtime->next_gateway_peer_report_ms = 0;
                }
                gateway_route_delta_t up_delta;
                up_delta.event_type = ZLINK_GATEWAY_ROUTE_UP;
                up_delta.service_name = service_name;
                up_delta.endpoint = endpoint;
                up_delta.routing_id = event.routing_id;
                up_delta.ready_count = ready_count;
                deltas.push_back (up_delta);
                gateway_route_delta_t ready_delta;
                ready_delta.event_type =
                  ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED;
                ready_delta.service_name = service_name;
                ready_delta.endpoint = endpoint;
                ready_delta.ready_count = ready_count;
                deltas.push_back (ready_delta);
            } else if (event.event == ZLINK_EVENT_DISCONNECTED
                       || event.event == ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
                       || event.event == ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
                       || event.event == ZLINK_EVENT_HANDSHAKE_FAILED_AUTH) {
                const std::string peer_key =
                  gateway_peer_key_local (service_name, event.routing_id);
                std::map<std::string,
                         gateway_runtime_t::gateway_peer_report_t>::iterator pit =
                  _runtime->ready_peer_reports.find (peer_key);
                if (pit != _runtime->ready_peer_reports.end ()) {
                    report_gateway_peer (pit->second.service_name,
                                         pit->second.peer_endpoint,
                                         pit->second.peer_routing_id,
                                         pit->second.weight,
                                         ZLINK_TOPOLOGY_STATE_LOST,
                                         pit->second.connected_since_ms);
                    _runtime->ready_peer_reports.erase (pit);
                    _runtime->next_gateway_peer_report_ms = 0;
                }
                gateway_route_delta_t down_delta;
                down_delta.event_type = ZLINK_GATEWAY_ROUTE_DOWN;
                down_delta.service_name = service_name;
                down_delta.endpoint = endpoint;
                down_delta.routing_id = event.routing_id;
                down_delta.ready_count = ready_count;
                down_delta.error_code = static_cast<int32_t> (event.event);
                deltas.push_back (down_delta);
                gateway_route_delta_t ready_delta;
                ready_delta.event_type =
                  ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED;
                ready_delta.service_name = service_name;
                ready_delta.endpoint = endpoint;
                ready_delta.ready_count = ready_count;
                ready_delta.error_code = static_cast<int32_t> (event.event);
                deltas.push_back (ready_delta);
            }
            emit_route_deltas (deltas);
        }
    }
}
}
