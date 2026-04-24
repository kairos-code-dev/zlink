/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_node.hpp"

#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_runtime.hpp"

namespace zlink
{
void spot_node_t::refresh_discovery_peers ()
{
    discovery_t *discovery = NULL;
    std::string service;
    uint64_t seq = 0;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery;
        service = _discovery_service;
        if (!discovery || service.empty ())
            return;
        seq = discovery->service_update_seq (service);
        if (_pending_service_updates.empty () && seq == _discovery_seq)
            return;
        _pending_service_updates.clear ();
        _discovery_seq = seq;
    }

    std::vector<provider_info_t> providers;
    discovery->snapshot_providers (service, &providers);

    std::set<std::string> new_endpoints;
    std::map<std::string, zlink_admission_state_t> new_admission_by_endpoint;
    std::map<std::string, zlink_admission_state_t> new_admission_by_rid;
    std::string self_endpoint;
    {
        scoped_lock_t lock (_sync);
        self_endpoint = _advertise_endpoint;
    }
    for (size_t i = 0; i < providers.size (); ++i) {
        if (!providers[i].endpoint.empty ()
            && self_endpoint != providers[i].endpoint) {
            new_endpoints.insert (providers[i].endpoint);
            new_admission_by_endpoint[providers[i].endpoint] =
              providers[i].admission_state;
            if (providers[i].routing_id.size > 0) {
                new_admission_by_rid[std::string (
                  reinterpret_cast<const char *> (providers[i].routing_id.data),
                  providers[i].routing_id.size)] = providers[i].admission_state;
            }
        }
    }

    std::vector<std::string> to_connect;
    std::vector<std::string> to_disconnect;
    size_t old_active_count = 0;
    {
        scoped_lock_t lock (_sync);
        old_active_count = _peer_state.active_endpoints.size ();
        to_connect.reserve (new_endpoints.size ());
        to_disconnect.reserve (_peer_state.discovery_endpoints.size ());
        for (std::set<std::string>::const_iterator it = new_endpoints.begin ();
             it != new_endpoints.end (); ++it) {
            if (_peer_state.discovery_endpoints.count (*it) == 0
                && _peer_state.active_endpoints.count (*it) == 0)
                to_connect.push_back (*it);
        }

        for (std::set<std::string>::const_iterator it =
               _peer_state.discovery_endpoints.begin ();
             it != _peer_state.discovery_endpoints.end (); ++it) {
            if (new_endpoints.count (*it) == 0
                && _peer_state.manual_endpoints.count (*it) == 0)
                to_disconnect.push_back (*it);
        }
    }

    for (size_t i = 0; i < to_connect.size (); ++i) {
        if (send_data_plane_command ("connect_peer_pub", to_connect[i].c_str ())
            == 0) {
            scoped_lock_t lock (_sync);
            if (_peer_state.active_endpoints.insert (to_connect[i]).second)
                _active_peer_count.fetch_add (1, std::memory_order_acq_rel);
        }
    }

    for (size_t i = 0; i < to_disconnect.size (); ++i) {
        if (send_data_plane_command ("disconnect_peer_pub",
                                     to_disconnect[i].c_str ())
            == 0) {
            scoped_lock_t lock (_sync);
            if (_peer_state.active_endpoints.erase (to_disconnect[i]) != 0)
                _active_peer_count.fetch_sub (1, std::memory_order_acq_rel);
        }
    }

    size_t new_active_count = 0;
    {
        scoped_lock_t lock (_sync);
        _peer_state.discovery_endpoints.swap (new_endpoints);
        _peer_state.peer_admission_by_endpoint.swap (new_admission_by_endpoint);
        _peer_state.peer_admission_by_rid.swap (new_admission_by_rid);
        new_active_count = _peer_state.active_endpoints.size ();
    }

    if (old_active_count == 0 && new_active_count > 0) {
        if (has_local_filtered_subs ()) {
            queue_all_subscription_ready_filters ();
            schedule_subscription_replay ();
            if (replay_subscriptions_if_active_peers () != 0) {
                debug_mark_fault (errno);
                return;
            }
        }
        refresh_sub_peer_summaries (true, false);
    } else if (!to_connect.empty () && new_active_count > 0) {
        if (has_local_filtered_subs ()) {
            queue_all_subscription_ready_filters ();
            schedule_subscription_replay ();
            if (replay_subscriptions_if_active_peers () != 0) {
                debug_mark_fault (errno);
                return;
            }
        }
    } else if (old_active_count > 0 && new_active_count == 0)
        refresh_sub_peer_summaries (false, true);
}

void spot_node_t::refresh_connected_peer_endpoints ()
{
    if (is_shutting_down ())
        return;

    std::set<std::string> connected;
    spot_runtime_t *runtime = NULL;
    uint64_t connected_peer_version = 0;
    size_t previous_connected_count = 0;
    {
        scoped_lock_t lock (_sync);
        runtime = _runtime;
        if (!runtime)
            return;
        connected_peer_version =
          mesh_peer_version (&runtime->execution.mesh_peer_state);
    }
    if (!runtime->note_connected_peer_version (connected_peer_version))
        return;
    snapshot_connected_mesh_peer_endpoints (&runtime->execution.mesh_peer_state,
                                            &connected);

    bool changed = false;
    std::vector<spot_sub_t *> subs;
    bool became_empty = false;
    {
        scoped_lock_t lock (_sync);
        if (connected == _peer_state.connected_endpoints)
            return;
        const uint64_t now_ms = zlink::clock_t ().now_ms ();
        for (std::set<std::string>::const_iterator it = connected.begin ();
             it != connected.end (); ++it) {
            if (_peer_state.connected_endpoints.count (*it) == 0) {
                spot_peer_observation_t &obs = _peer_state.observations[*it];
                obs.last_changed_ms = now_ms;
                if (obs.connected_since_ms == 0)
                    obs.connected_since_ms = now_ms;
            }
        }
        for (std::set<std::string>::const_iterator it =
               _peer_state.connected_endpoints.begin ();
             it != _peer_state.connected_endpoints.end (); ++it) {
            if (connected.count (*it) == 0) {
                spot_peer_observation_t &obs = _peer_state.observations[*it];
                obs.last_changed_ms = now_ms;
                obs.connected_since_ms = 0;
            }
        }
        previous_connected_count = _peer_state.connected_endpoints.size ();
        _peer_state.connected_endpoints.swap (connected);
        changed = true;
        _summary_last_changed_ms = now_ms;
        if (_peer_state.connected_endpoints.empty ()) {
            subs.reserve (_subs.size ());
            subs.assign (_subs.begin (), _subs.end ());
            clear_peer_readiness_locked (NULL);
            became_empty = true;
        } else {
            subs.reserve (_subs.size ());
            subs.assign (_subs.begin (), _subs.end ());
        }
    }

    if (became_empty) {
        for (size_t i = 0; i < subs.size (); ++i)
            subs[i]->mark_all_subjects_lost (NULL);
        return;
    }

    bool has_filters = false;
    for (size_t i = 0; i < subs.size (); ++i) {
        if (subs[i]->has_filters ()) {
            has_filters = true;
            break;
        }
    }

    if (changed && has_filters) {
        if (send_data_plane_command ("replay_subscriptions") != 0) {
            debug_mark_fault (errno);
            return;
        }
        queue_all_subscription_ready_filters ();
    }

    LIBZLINK_UNUSED (previous_connected_count);
}
}
