/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/gateway/gateway.hpp"

#include "services/common/socket_monitor_bridge.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/discovery/discovery_owned_service.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "services/gateway/gateway_runtime.hpp"
#include "utils/sleep.hpp"

#include <cstdio>
#include <cstdlib>

namespace zlink
{
namespace
{
static void gateway_diag_log_local (const char *stage_)
{
    if (!getenv ("ZLINK_GATEWAY_DIAG_LOG"))
        return;

    const uint64_t now_ms = zlink::clock_t ().now_ms ();
    FILE *fp = fopen ("/tmp/zlink_gateway_diag.log", "a");
    if (!fp)
        return;

    fprintf (fp,
             "ts=%llu pid=%ld stage=%s\n",
             static_cast<unsigned long long> (now_ms),
             static_cast<long> (getpid ()),
             stage_ ? stage_ : "?");
    fclose (fp);
}

static int unregister_gateway_service_with_retry_local (discovery_t *discovery_,
                                                        const char *endpoint_,
                                                        zlink::clock_t *clock_,
                                                        uint64_t timeout_ms_)
{
    if (!discovery_ || !endpoint_ || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    zlink::clock_t fallback_clock;
    zlink::clock_t *clock = clock_ ? clock_ : &fallback_clock;
    const uint64_t deadline_ms = clock->now_ms () + timeout_ms_;

    while (true) {
        if (discovery_owned_service::unregister_endpoint (
              discovery_, discovery_protocol::service_type_gateway_receiver,
              endpoint_)
            == 0)
            return 0;

        const int err = errno;
        if (err != EAGAIN || clock->now_ms () >= deadline_ms) {
            errno = err;
            return -1;
        }

        zlink::sleep_ms (1);
    }
}
}

int gateway_t::attach_discovery (discovery_t *discovery_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!discovery_
        || discovery_->service_type ()
             != discovery_protocol::service_type_gateway_receiver) {
        errno = EINVAL;
        return -1;
    }

    bool should_register = false;
    bool should_emit_ready = false;
    std::string bind_endpoint;
    uint32_t server_weight = 0;
    {
        scoped_lock_t lock (_sync);
        if (ensure_facade_mode () != 0)
            return -1;
        if (_discovery == discovery_)
            return 0;
        if (_discovery) {
            errno = EBUSY;
            return -1;
        }
        if (!_runtime->manual_routes.empty () || !_runtime->ready_endpoints.empty ()
            || !_runtime->inflight_endpoints.empty ()
            || !_runtime->down_endpoints.empty ()) {
            errno = EBUSY;
            return -1;
        }

        _discovery = discovery_;
        _service_name = discovery_->service_name ();
        _summary_last_changed_ms = _runtime->clock.now_ms ();
        if (_discovery->add_observer (this) != 0) {
            _discovery = NULL;
            _service_name.clear ();
            return -1;
        }
        _runtime->force_refresh_all = true;
        _runtime->pending_updates.insert (_service_name);
        if (!_bind_endpoint.empty ()) {
            should_register = true;
            should_emit_ready = !_service_ready_emitted;
            bind_endpoint = _bind_endpoint;
            server_weight = _server_weight;
        }
    }

    if (should_register && register_service (bind_endpoint.c_str (), server_weight) != 0) {
        scoped_lock_t lock (_sync);
        if (_discovery == discovery_) {
            (void) _discovery->remove_observer (this);
            _discovery = NULL;
            _service_name.clear ();
        }
        return -1;
    }

    if (should_register && should_emit_ready) {
        {
            scoped_lock_t lock (_sync);
            _service_ready_emitted = true;
        }
        emit_event (ZLINK_GATEWAY_MONITOR_EVENT_READY_CHANGED, _service_name,
                    bind_endpoint, NULL, 1, 0);
    }

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    {
        scoped_lock_t lock (_sync);
        if (ensure_refresh_task_running () != 0)
            return -1;
    }
    if (runtime && _runtime->refresh_task_id != 0)
        runtime->wakeup_task (_runtime->refresh_task_id);
    return 0;
}

int gateway_t::bind (const char *endpoint_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!endpoint_ || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    socket_base_t *router_socket = NULL;
    bool should_register = false;
    uint32_t server_weight = 0;
    bool needs_bind = false;
    bool should_emit_ready = false;
    {
        scoped_lock_t lock (_sync);
        if (ensure_facade_mode () != 0)
            return -1;
        lock_routing_id ();
        if (ensure_router_socket () != 0 || !_runtime->router_socket) {
            errno = ENOTSUP;
            return -1;
        }
        router_socket = _runtime->router_socket;

        if (!_tls_server_cert.empty ()) {
            if (router_socket->setsockopt (ZLINK_INTERNAL_OPT_TLS_CERT,
                                           _tls_server_cert.data (),
                                           _tls_server_cert.size ())
                  != 0
                || router_socket->setsockopt (ZLINK_INTERNAL_OPT_TLS_KEY,
                                              _tls_server_key.data (),
                                              _tls_server_key.size ())
                     != 0)
                return -1;
        }

        const bool already_bound_same_endpoint =
          !_bind_endpoint.empty () && _bind_endpoint == endpoint_;

        if (!already_bound_same_endpoint) {
            _bind_endpoint = endpoint_;
            needs_bind = true;
            _summary_last_changed_ms = _runtime->clock.now_ms ();
        }

        should_register = _discovery != NULL;
        server_weight = _server_weight;
        should_emit_ready = !_service_ready_emitted;
    }

    if (needs_bind && router_socket->bind (endpoint_) != 0) {
        scoped_lock_t lock (_sync);
        if (_bind_endpoint == endpoint_)
            _bind_endpoint.clear ();
        _last_summary_error = errno != 0 ? errno : EIO;
        _summary_last_changed_ms = _runtime->clock.now_ms ();
        return -1;
    }

    if (should_register && register_service (endpoint_, server_weight) != 0)
        return -1;

    if (should_emit_ready) {
        std::string ready_endpoint;
        {
            scoped_lock_t lock (_sync);
            _service_ready_emitted = true;
            _summary_last_changed_ms = _runtime->clock.now_ms ();
            ready_endpoint =
              !_advertise_endpoint.empty () ? _advertise_endpoint : _bind_endpoint;
        }
        emit_event (ZLINK_GATEWAY_MONITOR_EVENT_READY_CHANGED, _service_name,
                    ready_endpoint, NULL, 1, 0);
    }

    return 0;
}

int gateway_t::register_service (const char *advertise_endpoint_,
                                 uint32_t weight_)
{
    discovery_t *discovery = NULL;
    socket_base_t *router_socket = NULL;
    zlink_routing_id_t routing_id;
    memset (&routing_id, 0, sizeof (routing_id));
    std::string advertise_endpoint;
    {
        scoped_lock_t lock (_sync);
        if (_service_name.empty ()) {
            errno = EINVAL;
            return -1;
        }
        if (ensure_facade_mode () != 0)
            return -1;
        if (!_discovery) {
            errno = ENOTSUP;
            return -1;
        }
        if (ensure_router_socket () != 0 || !_runtime->router_socket) {
            errno = ENOTSUP;
            return -1;
        }

        router_socket = _runtime->router_socket;
        if (_routing_id.size == 0) {
            size_t size = sizeof (_routing_id.data);
            if (router_socket->getsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID,
                                           _routing_id.data, &size)
                != 0)
                return -1;
            _routing_id.size = static_cast<uint8_t> (size);
        }

        discovery = _discovery;
        routing_id = _routing_id;
        advertise_endpoint = resolve_advertise (advertise_endpoint_);
        if (advertise_endpoint.empty ()) {
            errno = EINVAL;
            return -1;
        }
    }

    std::string resolved;
    // Discovery attach can race with registry bootstrap and transient dealer
    // readiness during back-to-back integration runs. Keep bind semantics
    // stable by waiting long enough for the registry path to become usable.
    const uint64_t deadline_ms = _runtime->clock.now_ms () + 15000;
    while (true) {
        if (discovery_owned_service::register_endpoint (
              discovery, discovery_protocol::service_type_gateway_receiver,
              advertise_endpoint.c_str (), weight_, &resolved, &routing_id)
            == 0)
            break;

        const int err = errno;
        {
            scoped_lock_t lock (_sync);
            _last_register_error = strerror (err);
        }
        if (err != EAGAIN || _runtime->clock.now_ms () >= deadline_ms) {
            errno = err;
            return -1;
        }
        zlink::sleep_ms (1);
    }

    {
        scoped_lock_t lock (_sync);
        _advertise_endpoint =
          !resolved.empty () ? resolved : advertise_endpoint;
        _server_weight = weight_;
        _routing_id = routing_id;
        _last_register_error.clear ();
    }
    return 0;
}

int gateway_t::update_weight (uint32_t weight_)
{
    if (_service_name.empty ()) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (ensure_facade_mode () != 0)
        return -1;
    if (!_discovery) {
        errno = ENOTSUP;
        return -1;
    }
    if (_advertise_endpoint.empty ()) {
        errno = EFSM;
        return -1;
    }

    const uint32_t value = weight_;
    if (discovery_owned_service::update_weight (
          _discovery, discovery_protocol::service_type_gateway_receiver,
          _advertise_endpoint.c_str (), value)
        != 0)
        return -1;
    _server_weight = value;
    return 0;
}

int gateway_t::unregister_service ()
{
    if (_service_name.empty ()) {
        errno = EINVAL;
        return -1;
    }

    discovery_t *discovery = NULL;
    std::string service_name;
    std::string endpoint;
    {
        scoped_lock_t lock (_sync);
        if (ensure_facade_mode () != 0)
            return -1;
        if (!_discovery) {
            errno = ENOTSUP;
            return -1;
        }
        if (_advertise_endpoint.empty ()) {
            errno = EINVAL;
            return -1;
        }

        discovery = _discovery;
        service_name = _service_name;
        endpoint = _advertise_endpoint;
    }

    if (unregister_gateway_service_with_retry_local (
          discovery, endpoint.c_str (), &_runtime->clock, 15000)
        != 0)
        return -1;

    {
        scoped_lock_t lock (_sync);
        _advertise_endpoint.clear ();
        _last_register_error.clear ();
        _service_ready_emitted = false;
    }

    emit_event (ZLINK_GATEWAY_MONITOR_EVENT_READY_CHANGED, service_name,
                endpoint, NULL, 0, 0);
    report_topology (service_name, endpoint, ZLINK_TOPOLOGY_STATE_STOPPED, 0,
                     0);
    return 0;
}

int gateway_t::set_tls_server (const char *cert_, const char *key_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!cert_ || !key_) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (ensure_facade_mode () != 0)
        return -1;
    if (cert_[0] == '\0' || key_[0] == '\0') {
        _tls_server_cert.clear ();
        _tls_server_key.clear ();
        return 0;
    }

    _tls_server_cert = cert_;
    _tls_server_key = key_;
    if (_runtime->router_socket) {
        if (_runtime->router_socket->setsockopt (
              ZLINK_INTERNAL_OPT_TLS_CERT, _tls_server_cert.data (),
              _tls_server_cert.size ())
              != 0
            || _runtime->router_socket->setsockopt (
                 ZLINK_INTERNAL_OPT_TLS_KEY, _tls_server_key.data (),
                 _tls_server_key.size ())
                 != 0)
            return -1;
    }
    return 0;
}

int gateway_t::destroy ()
{
    gateway_diag_log_local ("destroy.begin");
    std::set<std::string> endpoints_to_term;

    _runtime->stop.set (1);
    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _runtime->refresh_task_id != 0)
        runtime->remove_task (_runtime->refresh_task_id);
    gateway_diag_log_local ("destroy.after-remove-task");
    _runtime->refresh_task_id = 0;
    const auto destroy_detach_phase = [&]() {
        discovery_t *discovery = _discovery;
        const std::string service_name (_service_name);
        const std::string endpoint (_advertise_endpoint);

        if (discovery)
            discovery->remove_observer (this);
        if (discovery && !service_name.empty () && !endpoint.empty ()) {
            (void) unregister_gateway_service_with_retry_local (
              discovery, endpoint.c_str (), &_runtime->clock, 15000);
            report_topology (service_name, endpoint,
                             ZLINK_TOPOLOGY_STATE_STOPPED, 0, 0);
        }
        if (!_bind_endpoint.empty ())
            endpoints_to_term.insert (_bind_endpoint);
        endpoints_to_term.insert (_runtime->ready_endpoints.begin (),
                                  _runtime->ready_endpoints.end ());
        endpoints_to_term.insert (_runtime->inflight_endpoints.begin (),
                                  _runtime->inflight_endpoints.end ());
        endpoints_to_term.insert (_runtime->down_endpoints.begin (),
                                  _runtime->down_endpoints.end ());
        for (std::map<std::string,
                      gateway_runtime_t::gateway_peer_report_t>::const_iterator
               it = _runtime->ready_peer_reports.begin ();
             it != _runtime->ready_peer_reports.end (); ++it) {
            report_gateway_peer (it->second.service_name,
                                 it->second.peer_endpoint,
                                 it->second.peer_routing_id, it->second.weight,
                                 ZLINK_TOPOLOGY_STATE_STOPPED,
                                 it->second.connected_since_ms);
        }
        _runtime->pools.clear ();
        _runtime->primary_pool = NULL;
        _runtime->endpoint_to_service.clear ();
        _runtime->routing_id_to_service.clear ();
        _runtime->ready_endpoints.clear ();
        _runtime->inflight_endpoints.clear ();
        _runtime->inflight_rid_by_endpoint.clear ();
        _runtime->rid_connect_not_before_ms.clear ();
        _runtime->down_endpoints.clear ();
        _runtime->down_until_ms.clear ();
        _runtime->ready_peer_reports.clear ();
        _runtime->next_gateway_peer_report_ms = 0;
        _runtime->force_refresh_all = false;
        _runtime->pending_updates.clear ();
    };

    const auto destroy_monitor_phase = [&]() {
        zlink_service_event_t terminal;
        memset (&terminal, 0, sizeof (terminal));
        terminal.service_kind = ZLINK_SERVICE_KIND_GATEWAY;
        terminal.event_type = ZLINK_MONITOR_EVENT_CLOSED;
        terminal.detail_flags = ZLINK_EVENT_DETAIL_SUBJECT_RID;
        terminal.routing_id = _routing_id;
        _monitor.close_all (&terminal);
    };

    const auto destroy_socket_phase = [&]() {
        if (_runtime->monitor_socket) {
            socket_base_t *monitor_socket =
              static_cast<socket_base_t *> (_runtime->monitor_socket);
            close_socket_monitor_bridge (_runtime->router_socket, monitor_socket);
            (void) _runtime->lifecycle.close_socket_and_wait (monitor_socket,
                                                              2000);
            _runtime->monitor_socket = NULL;
        }
        if (_runtime->router_socket) {
            for (std::set<std::string>::const_iterator it =
                   endpoints_to_term.begin (),
                   end = endpoints_to_term.end ();
                 it != end; ++it) {
                (void) _runtime->router_socket->term_endpoint (it->c_str ());
            }
            (void) _runtime->lifecycle.close_socket_and_wait (
              _runtime->router_socket, 2000);
        }
    };

    destroy_detach_phase ();
    destroy_monitor_phase ();
    gateway_diag_log_local ("destroy.after-monitor-close-all");
    destroy_socket_phase ();
    gateway_diag_log_local ("destroy.after-monitor-socket-close");
    gateway_diag_log_local ("destroy.before-wait-drained");
    (void) _runtime->lifecycle.wait_drained (10000);
    gateway_diag_log_local ("destroy.after-wait-drained");
    return 0;
}
}
