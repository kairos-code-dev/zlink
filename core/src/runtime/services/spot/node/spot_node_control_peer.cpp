/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node.hpp"

#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "utils/routing_id.hpp"

namespace zlink
{
bool spot_node_t::external_route_id_for_peer_endpoint (const std::string &peer_endpoint_,
                                                       std::string *out_) const
{
    if (peer_endpoint_.empty () || !out_)
        return false;

    scoped_lock_t lock (_sync);
    for (std::map<std::string, std::set<std::string>>::const_iterator it =
           _peer_state.peer_endpoints_by_rid.begin ();
         it != _peer_state.peer_endpoints_by_rid.end (); ++it) {
        if (it->second.count (peer_endpoint_) != 0) {
            *out_ = it->first;
            return !out_->empty ();
        }
    }
    return false;
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
        connected_peer_version = mesh_peer_version (&runtime->execution.mesh_peer_state);
    }
    if (!runtime->note_connected_peer_version (connected_peer_version))
        return;
    snapshot_connected_mesh_peer_endpoints (&runtime->execution.mesh_peer_state, &connected);

    bool changed = false;
    std::vector<spot_sub_t *> subs;
    bool became_empty = false;
    {
        scoped_lock_t lock (_sync);
        if (connected == _peer_state.connected_endpoints)
            return;
        const uint64_t now_ms = zlink::clock_t ().now_ms ();
        for (std::set<std::string>::const_iterator it = connected.begin (); it != connected.end ();
             ++it) {
            if (_peer_state.connected_endpoints.count (*it) == 0) {
                spot_peer_observation_t &obs = _peer_state.observations[*it];
                obs.last_changed_ms = now_ms;
                if (obs.connected_since_ms == 0)
                    obs.connected_since_ms = now_ms;
            }
        }
        for (std::set<std::string>::const_iterator it = _peer_state.connected_endpoints.begin ();
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
        _summary_state.summary_last_changed_ms = now_ms;
        if (_peer_state.connected_endpoints.empty ()) {
            subs.reserve (_handle_state.subs.size ());
            subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
            clear_peer_readiness_locked (NULL);
            became_empty = true;
        } else {
            subs.reserve (_handle_state.subs.size ());
            subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
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
        if (send_data_plane_command (spot_control_protocol::cmd_replay_handle_state_subscriptions)
            != 0) {
            debug_mark_fault (errno);
            return;
        }
        queue_all_subscription_ready_filters ();
    }

    LIBZLINK_UNUSED (previous_connected_count);
}
}
