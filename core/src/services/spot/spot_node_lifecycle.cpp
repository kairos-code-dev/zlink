/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_node.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_sub.hpp"

#include "services/control/service_control_runtime.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "utils/clock.hpp"

namespace zlink
{
namespace
{
static void preserve_first_error_local (int rc_, int *first_error_)
{
    if (rc_ == 0 || !first_error_ || *first_error_ != 0)
        return;
    *first_error_ = errno != 0 ? errno : EIO;
}

static void spot_shutdown_logf_local (bool always_, const char *fmt_, ...)
{
    LIBZLINK_UNUSED (always_);
    LIBZLINK_UNUSED (fmt_);
}
}

int spot_node_t::bind (const char *endpoint_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!endpoint_ || endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    {
        scoped_lock_t lock (_sync);
        if (!_bound_endpoint.empty ()) {
            errno = EBUSY;
            return -1;
        }
    }

    if (send_data_plane_command ("bind_pub", endpoint_) != 0)
        return -1;

    std::vector<spot_pub_t *> pubs;
    bool should_register = false;
    {
        scoped_lock_t lock (_sync);
        _bound_endpoint = endpoint_;
        _server_tls_locked = true;
        _summary_last_changed_ms = zlink::clock_t ().now_ms ();
        pubs.assign (_pubs.begin (), _pubs.end ());
        should_register = _discovery != NULL;
    }
    if (should_register && ensure_registered () != 0)
        return -1;
    for (size_t i = 0; i < pubs.size (); ++i)
        submit_pub_summary (pubs[i], ZLINK_TOPOLOGY_STATE_READY, 0);
    return 0;
}

int spot_node_t::connect_peer_pub (const char *peer_pub_endpoint_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!peer_pub_endpoint_ || peer_pub_endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    bool need_connect = false;
    bool had_active_peers = false;
    {
        scoped_lock_t lock (_sync);
        if (_discovery) {
            errno = EBUSY;
            return -1;
        }
        had_active_peers = !_active_peer_endpoints.empty ();
        if (_manual_peer_endpoints.count (peer_pub_endpoint_) != 0)
            return 0;
        _manual_peer_endpoints.insert (peer_pub_endpoint_);
        _peer_observations[peer_pub_endpoint_].last_changed_ms =
          zlink::clock_t ().now_ms ();
        _summary_last_changed_ms = zlink::clock_t ().now_ms ();
        if (_active_peer_endpoints.count (peer_pub_endpoint_) == 0)
            need_connect = true;
    }

    if (need_connect && send_data_plane_command ("connect_peer_pub",
                                                 peer_pub_endpoint_)
                           != 0) {
        scoped_lock_t lock (_sync);
        _manual_peer_endpoints.erase (peer_pub_endpoint_);
        return -1;
    }

    bool has_active_peers = false;
    {
        scoped_lock_t lock (_sync);
        _mesh_client_tls_locked = true;
        if (_active_peer_endpoints.insert (peer_pub_endpoint_).second)
            _active_peer_count.fetch_add (1, std::memory_order_acq_rel);
        _peer_observations[peer_pub_endpoint_].last_changed_ms =
          zlink::clock_t ().now_ms ();
        _summary_last_changed_ms = zlink::clock_t ().now_ms ();
        has_active_peers = !_active_peer_endpoints.empty ();
    }
    if (has_active_peers) {
        if (has_local_filtered_subs ()) {
            queue_all_subscription_ready_filters ();
            schedule_subscription_replay ();
            if (replay_subscriptions_if_active_peers () != 0)
                return -1;
        }
        if (!had_active_peers)
            refresh_sub_peer_summaries (true, false);
    }
    return 0;
}

int spot_node_t::disconnect_peer_pub (const char *peer_pub_endpoint_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!peer_pub_endpoint_ || peer_pub_endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    bool need_disconnect = false;
    bool had_active_peers = false;
    bool disconnecting_last_active_peer = false;
    {
        scoped_lock_t lock (_sync);
        if (_discovery) {
            errno = EBUSY;
            return -1;
        }
        had_active_peers = !_active_peer_endpoints.empty ();
        _manual_peer_endpoints.erase (peer_pub_endpoint_);
        _peer_observations[peer_pub_endpoint_].last_changed_ms =
          zlink::clock_t ().now_ms ();
        _summary_last_changed_ms = zlink::clock_t ().now_ms ();
        if (_discovery_peer_endpoints.count (peer_pub_endpoint_) == 0
            && _active_peer_endpoints.count (peer_pub_endpoint_) != 0) {
            need_disconnect = true;
            disconnecting_last_active_peer = _active_peer_endpoints.size () == 1;
        }
    }

    if (need_disconnect && disconnecting_last_active_peer) {
        std::vector<spot_sub_t *> subs;
        {
            scoped_lock_t lock (_sync);
            subs.assign (_subs.begin (), _subs.end ());
        }
        for (size_t i = 0; i < subs.size (); ++i)
            subs[i]->send_ready_ack_lost_for_endpoint (peer_pub_endpoint_);
    }

    if (need_disconnect
        && send_data_plane_command ("disconnect_peer_pub", peer_pub_endpoint_)
             != 0)
        return -1;

    if (need_disconnect) {
        bool has_active_peers = false;
        {
            scoped_lock_t lock (_sync);
            if (_active_peer_endpoints.erase (peer_pub_endpoint_) != 0)
                _active_peer_count.fetch_sub (1, std::memory_order_acq_rel);
            _peer_observations[peer_pub_endpoint_].last_changed_ms =
              zlink::clock_t ().now_ms ();
            _peer_observations[peer_pub_endpoint_].connected_since_ms = 0;
            _summary_last_changed_ms = zlink::clock_t ().now_ms ();
            has_active_peers = !_active_peer_endpoints.empty ();
        }
        if (had_active_peers && !has_active_peers) {
            std::vector<spot_sub_t *> subs;
            std::vector<spot_pub_t *> pubs;
            std::vector<std::pair<std::string, uint32_t> > pub_ready_updates;
            {
                scoped_lock_t lock (_sync);
                _connected_peer_endpoints.clear ();
                _summary_last_changed_ms = zlink::clock_t ().now_ms ();
                _subscription_ready_refresh_pending = false;
                _subscription_ready_refresh_holdoff_ticks = 0;
                _pending_subscription_ready_filters.clear ();
                subs.assign (_subs.begin (), _subs.end ());
                pubs.assign (_pubs.begin (), _pubs.end ());
                for (std::map<std::string, std::set<std::string> >::iterator it =
                       _pub_delivery_ready_sources.begin ();
                     it != _pub_delivery_ready_sources.end (); ++it) {
                    pub_ready_updates.push_back (
                      std::make_pair (it->first, static_cast<uint32_t> (0)));
                }
                _pub_delivery_ready_sources.clear ();
                publish_mesh_pub_budget_hint_locked ();
            }
            refresh_sub_peer_summaries (false, true);
            for (size_t i = 0; i < subs.size (); ++i)
                subs[i]->mark_all_subjects_lost (NULL);
            for (size_t i = 0; i < pubs.size (); ++i) {
                for (size_t j = 0; j < pub_ready_updates.size (); ++j) {
                    pubs[i]->emit_delivery_ready_changed_event (
                      pub_ready_updates[j].first.c_str (), false,
                      ZLINK_SERVICE_EVENT_SUBJECT_NONE,
                      pub_ready_updates[j].second);
                    pubs[i]->emit_first_delivery_ready_changed_event (
                      pub_ready_updates[j].first.c_str (), false,
                      ZLINK_SERVICE_EVENT_SUBJECT_NONE,
                      pub_ready_updates[j].second);
                }
            }
        }
    }
    return 0;
}

int spot_node_t::ensure_registered ()
{
    if (ensure_healthy () != 0)
        return -1;

    discovery_t *discovery = NULL;
    std::string advertise;
    bool need_default_pub = false;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery;
        if (_registered)
            return 0;
        if (_bound_endpoint.empty ()) {
            errno = EFSM;
            return -1;
        }
        advertise = _advertise_endpoint.empty () ? _bound_endpoint
                                                 : _advertise_endpoint;
    }
    if (!discovery) {
        errno = EFSM;
        return -1;
    }
    if (!validate_public_endpoint (advertise)) {
        errno = EINVAL;
        return -1;
    }

    std::string resolved;
    if (discovery->register_service (discovery_protocol::service_type_spot_node,
                                     _service_name.c_str (),
                                     advertise.c_str (), 1, &resolved)
        != 0) {
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        _registered = true;
        _advertise_endpoint = resolved.empty () ? advertise : resolved;
        if (!discovery->latest_registry_uplink (&_registration_uplink_endpoint))
            _registration_uplink_endpoint.clear ();
        _registration_tls_locked = true;
        need_default_pub = _pubs.empty () && !_handle_defaults.default_pub ();
    }

    if (need_default_pub && !ensure_default_pub ())
        return -1;

    return 0;
}

int spot_node_t::unregister_registered ()
{
    if (ensure_healthy () != 0)
        return -1;

    std::string advertise;
    {
        scoped_lock_t lock (_sync);
        if (!_registered) {
            return 0;
        }
        advertise = _advertise_endpoint;
    }

    discovery_t *discovery = NULL;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery;
    }
    if (!discovery) {
        errno = EFSM;
        return -1;
    }
    if (discovery->unregister_service (discovery_protocol::service_type_spot_node,
                                       _service_name.c_str (),
                                       advertise.c_str ())
        != 0)
        return -1;

    scoped_lock_t lock (_sync);
    _registered = false;
    _advertise_endpoint.clear ();
    _registration_uplink_endpoint.clear ();
    return 0;
}

int spot_node_t::attach_discovery (discovery_t *discovery_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!discovery_ || discovery_->service_type ()
                          != discovery_protocol::service_type_spot_node) {
        errno = EINVAL;
        return -1;
    }

    bool should_register = false;
    {
        scoped_lock_t lock (_sync);
        if (_discovery == discovery_)
            return 0;
        if (_discovery || !_manual_peer_endpoints.empty ()) {
            errno = EBUSY;
            return -1;
        }
        _discovery = discovery_;
        _discovery_service = _service_name;
        _discovery_seq = 0;
        _pending_service_updates.insert (_service_name);
        _discovery_peer_endpoints.clear ();
        _summary_last_changed_ms = zlink::clock_t ().now_ms ();
        should_register = !_bound_endpoint.empty ();
    }
    discovery_->add_observer (this);
    if (should_register && ensure_registered () != 0) {
        scoped_lock_t lock (_sync);
        if (_discovery == discovery_) {
            _discovery->remove_observer (this);
            _discovery = NULL;
            _discovery_service.clear ();
        }
        return -1;
    }
    refresh_existing_summaries ();
    wake_control_task ();
    return 0;
}

void spot_node_t::on_service_update (const std::string &service_name_)
{
    bool should_wake = false;
    {
        scoped_lock_t lock (_sync);
        if (_discovery_service.empty () || service_name_ != _discovery_service)
            return;
        _pending_service_updates.insert (service_name_);
        _summary_last_changed_ms = zlink::clock_t ().now_ms ();
        should_wake = true;
    }
    if (should_wake)
        wake_control_task ();
}

void spot_node_t::on_discovery_destroyed (discovery_t *discovery_)
{
    scoped_lock_t lock (_sync);
    if (_discovery != discovery_)
        return;
    _discovery = NULL;
    _discovery_service.clear ();
    _discovery_seq = 0;
    _pending_service_updates.clear ();
    _discovery_peer_endpoints.clear ();
    _connected_peer_endpoints.clear ();
    _registered = false;
    _advertise_endpoint.clear ();
    _registration_uplink_endpoint.clear ();
    _summary_last_changed_ms = zlink::clock_t ().now_ms ();
}

int spot_node_t::set_tls_server (const char *cert_, const char *key_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!cert_ || !key_ || cert_[0] == '\0' || key_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_server_tls_locked || !_bound_endpoint.empty ()) {
        errno = EBUSY;
        return -1;
    }
    _tls_cert = cert_;
    _tls_key = key_;
    return 0;
}

int spot_node_t::set_tls_client (const char *ca_cert_,
                                 const char *hostname_,
                                 int trust_system_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (trust_system_ < 0) {
        errno = EINVAL;
        return -1;
    }

    scoped_lock_t lock (_sync);
    if (_mesh_client_tls_locked || _registration_tls_locked) {
        errno = EBUSY;
        return -1;
    }
    _tls_ca = ca_cert_ ? ca_cert_ : "";
    _tls_hostname = hostname_ ? hostname_ : "";
    _tls_trust_system = trust_system_;
    return 0;
}

int spot_node_t::set_send_ready_handler (zlink_send_ready_handler_fn handler_,
                                         void *userdata_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    spot_pub_t *pub = ensure_default_pub ();
    if (!pub)
        return -1;

    const int rc = pub->set_send_ready_handler (handler_, this, userdata_);
    if (rc == 0) {
        _send_ready_handler_userdata.store (userdata_,
                                            std::memory_order_release);
        _send_ready_handler.store (handler_, std::memory_order_release);
    }
    return rc;
}

int spot_node_t::destroy ()
{
    {
        scoped_lock_t lock (_sync);
        for (std::set<spot_pub_t *>::const_iterator it = _pubs.begin ();
             it != _pubs.end (); ++it) {
            if (*it && !(*it)->is_node_owned_default ()) {
                errno = EBUSY;
                return -1;
            }
        }
        for (std::set<spot_sub_t *>::const_iterator it = _subs.begin ();
             it != _subs.end (); ++it) {
            spot_internal_receiver_t *receiver = _handle_defaults.internal_receiver ();
            if (receiver && *it == receiver->impl ())
                continue;
            if (*it && !(*it)->is_node_owned_default ()) {
                errno = EBUSY;
                return -1;
            }
        }
    }
    _lifecycle.transition_to (service_state_stopping);
    discovery_t *discovery = NULL;
    std::vector<std::string> active_peer_endpoints;
    std::string bound_endpoint;
    int first_error = 0;
    int graceful_error = 0;
    int final_error = 0;
    bool used_abortive = false;

    spot_shutdown_logf_local (false,
                              "step=begin node=%p service=%s state=%d tracked=%zu",
                              static_cast<void *> (this),
                              _service_name.c_str (),
                              static_cast<int> (_lifecycle.state ()),
                              _lifecycle.owned_socket_count ());
    if (_discovery && _registered)
        (void) unregister_registered ();
    {
        scoped_lock_t lock (_sync);
        active_peer_endpoints.assign (_active_peer_endpoints.begin (),
                                      _active_peer_endpoints.end ());
        bound_endpoint = _bound_endpoint;
    }
    for (size_t i = 0; i < active_peer_endpoints.size (); ++i)
        (void) send_data_plane_command ("disconnect_peer_pub",
                                        active_peer_endpoints[i].c_str ());
    if (!bound_endpoint.empty ())
        (void) send_data_plane_command ("unbind_pub", bound_endpoint.c_str ());
    spot_shutdown_logf_local (false, "step=peer_disconnect node=%p",
                              static_cast<void *> (this));
    if (_runtime)
        _runtime->stop.set (1);
    submit_stopped_summaries ();
    spot_shutdown_logf_local (false, "step=summaries_stopped node=%p",
                              static_cast<void *> (this));

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (runtime && _runtime && _runtime->task_id != 0)
        runtime->remove_task (_runtime->task_id);
    if (_runtime)
        _runtime->task_id = 0;
    spot_shutdown_logf_local (false, "step=task_removed node=%p",
                              static_cast<void *> (this));

    {
        scoped_lock_t lock (_sync);
        discovery = _discovery;
        _discovery = NULL;
        _discovery_service.clear ();
        _discovery_seq = 0;
        _pending_service_updates.clear ();
        _manual_peer_endpoints.clear ();
        _active_peer_endpoints.clear ();
        _active_peer_count.store (0, std::memory_order_release);
        _connected_peer_endpoints.clear ();
        _discovery_peer_endpoints.clear ();
        _registered = false;
        _advertise_endpoint.clear ();
        _registration_uplink_endpoint.clear ();
    }

    if (discovery)
        preserve_first_error_local (discovery->remove_observer (this),
                                    &first_error);
    spot_shutdown_logf_local (false, "step=observer_removed node=%p",
                              static_cast<void *> (this));

    if (_runtime)
        preserve_first_error_local (_runtime->stop_and_join (), &first_error);
    spot_shutdown_logf_local (false,
                              "step=data_plane_stopped node=%p error=%d",
                              static_cast<void *> (this), first_error);
    preserve_first_error_local (destroy_handles (), &first_error);
    preserve_first_error_local (destroy_internal_receiver (), &first_error);
    if (_runtime)
        preserve_first_error_local (_runtime->close_control_sockets (),
                                    &first_error);
    spot_shutdown_logf_local (false,
                              "step=handles_destroyed node=%p error=%d tracked=%zu",
                              static_cast<void *> (this), first_error,
                              _lifecycle.owned_socket_count ());
    preserve_first_error_local (wait_owned_socket_removals (10000),
                                &first_error);
    graceful_error = first_error;
    final_error = graceful_error;

    if (_runtime
        && (first_error != 0 || _runtime->live_socket_slot_count () != 0
            || _runtime->attachment_count () != 0)) {
        const int abort_reason = first_error != 0 ? first_error : ETIMEDOUT;
        const size_t live_slots = _runtime->live_socket_slot_count ();
        const size_t live_attachments = _runtime->attachment_count ();
        const size_t tracked_sockets = _lifecycle.owned_socket_count ();
        size_t ctx_socket_baseline = 0;
        if (_ctx) {
            const size_t ctx_socket_count = _ctx->socket_count ();
            ctx_socket_baseline =
              ctx_socket_count > tracked_sockets ? ctx_socket_count - tracked_sockets
                                                 : 0;
        }
        used_abortive = true;
        spot_shutdown_logf_local (
          true,
          "service=spot node=%p shutdown=abortive reason=%d live_slots=%zu attachments=%zu tracked=%zu",
          static_cast<void *> (this), abort_reason, live_slots,
          live_attachments, tracked_sockets);
        _runtime->abortive_stop ();
        preserve_first_error_local (_lifecycle.force_wait_remaining (5000),
                                    &final_error);
        preserve_first_error_local (wait_owned_socket_removals (5000),
                                    &final_error);
        if (_runtime->live_socket_slot_count () == 0
            && _runtime->attachment_count () == 0) {
            if (_lifecycle.owned_socket_count () != 0)
                _lifecycle.clear_tracked_sockets ();

            if (_lifecycle.owned_socket_count () == 0) {
                final_error = 0;
            } else if (_ctx
                       && _ctx->wait_for_socket_count_at_most (
                            ctx_socket_baseline, 5000)
                            == 0) {
                _lifecycle.clear_tracked_sockets ();
                final_error = 0;
            } else if (_ctx && _ctx->socket_count () == 0) {
                _lifecycle.clear_tracked_sockets ();
                final_error = 0;
            }
        }
    }

    if (!used_abortive)
        spot_shutdown_logf_local (false,
                                  "service=spot node=%p shutdown=graceful",
                                  static_cast<void *> (this));
    _lifecycle.transition_to (service_state_stopped);
    spot_shutdown_logf_local (false,
                              "step=complete node=%p state=%d error=%d tracked=%zu",
                              static_cast<void *> (this),
                              static_cast<int> (_lifecycle.state ()),
                              final_error, _lifecycle.owned_socket_count ());
    if (final_error != 0) {
        errno = final_error;
        return -1;
    }
    return 0;
}
}
