/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node.hpp"

#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/pubsub/spot_sub.hpp"

#include "utils/clock.hpp"
#include "utils/routing_id.hpp"

namespace zlink
{
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
        had_active_peers = !_peer_state.active_endpoints.empty ();
        if (_peer_state.manual_endpoints.count (peer_pub_endpoint_) != 0)
            return 0;
        _peer_state.manual_endpoints.insert (peer_pub_endpoint_);
        _peer_state.observations[peer_pub_endpoint_].last_changed_ms = zlink::clock_t ().now_ms ();
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        if (_peer_state.active_endpoints.count (peer_pub_endpoint_) == 0)
            need_connect = true;
    }

    if (need_connect
        && send_data_plane_command (spot_control_protocol::cmd_connect_peer_pub, peer_pub_endpoint_)
             != 0) {
        scoped_lock_t lock (_sync);
        _peer_state.manual_endpoints.erase (peer_pub_endpoint_);
        return -1;
    }

    bool has_active_peers = false;
    {
        scoped_lock_t lock (_sync);
        _tls_state.mesh_client_tls_locked = true;
        if (_peer_state.active_endpoints.insert (peer_pub_endpoint_).second)
            _endpoint_state.active_peer_count.fetch_add (1, std::memory_order_acq_rel);
        _peer_state.observations[peer_pub_endpoint_].last_changed_ms = zlink::clock_t ().now_ms ();
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        has_active_peers = !_peer_state.active_endpoints.empty ();
    }
    if (has_active_peers) {
        if (!had_active_peers)
            refresh_sub_peer_summaries (true, false);
    }
    lock_entry_spot_rid ();
    return 0;
}

int spot_node_t::connect_peer_pub_rid (const zlink_routing_id_t *target_node_rid_,
                                       const char *peer_pub_endpoint_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!valid_routing_id (target_node_rid_) || !peer_pub_endpoint_ || peer_pub_endpoint_[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    const std::string rid_key = zlink::routing_id_key (target_node_rid_);
    bool need_connect = false;
    bool had_active_peers = false;
    bool endpoint_was_manual = false;
    bool inserted_rid_endpoint = false;
    {
        scoped_lock_t lock (_sync);
        had_active_peers = !_peer_state.active_endpoints.empty ();
        endpoint_was_manual = _peer_state.manual_endpoints.count (peer_pub_endpoint_) != 0;
        if (!endpoint_was_manual)
            _peer_state.manual_endpoints.insert (peer_pub_endpoint_);
        _peer_state.peer_weight_by_rid[rid_key] = 100;
        inserted_rid_endpoint =
          _peer_state.peer_endpoints_by_rid[rid_key].insert (peer_pub_endpoint_).second;
        _peer_state.observations[peer_pub_endpoint_].last_changed_ms = zlink::clock_t ().now_ms ();
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        if (_peer_state.active_endpoints.count (peer_pub_endpoint_) == 0 || inserted_rid_endpoint)
            need_connect = true;
    }

    std::vector<std::string> args;
    args.push_back (peer_pub_endpoint_);
    args.push_back (rid_key);
    if (need_connect
        && send_data_plane_command (spot_control_protocol::cmd_connect_peer_pub, args) != 0) {
        scoped_lock_t lock (_sync);
        if (!endpoint_was_manual)
            _peer_state.manual_endpoints.erase (peer_pub_endpoint_);
        if (inserted_rid_endpoint) {
            _peer_state.peer_endpoints_by_rid[rid_key].erase (peer_pub_endpoint_);
            if (_peer_state.peer_endpoints_by_rid[rid_key].empty ()) {
                _peer_state.peer_endpoints_by_rid.erase (rid_key);
                _peer_state.peer_weight_by_rid.erase (rid_key);
            }
        }
        return -1;
    }

    bool has_active_peers = false;
    {
        scoped_lock_t lock (_sync);
        _tls_state.mesh_client_tls_locked = true;
        if (_peer_state.active_endpoints.insert (peer_pub_endpoint_).second)
            _endpoint_state.active_peer_count.fetch_add (1, std::memory_order_acq_rel);
        _peer_state.observations[peer_pub_endpoint_].last_changed_ms = zlink::clock_t ().now_ms ();
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        has_active_peers = !_peer_state.active_endpoints.empty ();
    }
    if (has_active_peers && !had_active_peers)
        refresh_sub_peer_summaries (true, false);
    lock_entry_spot_rid ();
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
        if (_discovery_state.discovery) {
            errno = EBUSY;
            return -1;
        }
        had_active_peers = !_peer_state.active_endpoints.empty ();
        _peer_state.manual_endpoints.erase (peer_pub_endpoint_);
        _peer_state.observations[peer_pub_endpoint_].last_changed_ms = zlink::clock_t ().now_ms ();
        _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        if (_peer_state.discovery_endpoints.count (peer_pub_endpoint_) == 0
            && _peer_state.active_endpoints.count (peer_pub_endpoint_) != 0) {
            need_disconnect = true;
            disconnecting_last_active_peer = _peer_state.active_endpoints.size () == 1;
        }
    }

    if (need_disconnect && disconnecting_last_active_peer) {
        std::vector<spot_sub_t *> subs;
        {
            scoped_lock_t lock (_sync);
            subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
        }
        for (size_t i = 0; i < subs.size (); ++i)
            subs[i]->send_ready_ack_lost_for_endpoint (peer_pub_endpoint_);
    }

    if (need_disconnect
        && send_data_plane_command (spot_control_protocol::cmd_disconnect_peer_pub,
                                    peer_pub_endpoint_)
             != 0)
        return -1;

    if (need_disconnect) {
        bool has_active_peers = false;
        {
            scoped_lock_t lock (_sync);
            if (_peer_state.active_endpoints.erase (peer_pub_endpoint_) != 0)
                _endpoint_state.active_peer_count.fetch_sub (1, std::memory_order_acq_rel);
            _peer_state.observations[peer_pub_endpoint_].last_changed_ms =
              zlink::clock_t ().now_ms ();
            _peer_state.observations[peer_pub_endpoint_].connected_since_ms = 0;
            _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
            has_active_peers = !_peer_state.active_endpoints.empty ();
        }
        if (had_active_peers && !has_active_peers) {
            std::vector<spot_sub_t *> subs;
            {
                scoped_lock_t lock (_sync);
                _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
                subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
                clear_peer_readiness_locked (NULL);
            }
            refresh_sub_peer_summaries (false, true);
            for (size_t i = 0; i < subs.size (); ++i)
                subs[i]->mark_all_subjects_lost (NULL);
        }
    }
    return 0;
}

int spot_node_t::disconnect_peer_pub_rid (const zlink_routing_id_t *target_node_rid_)
{
    service_public_api_scope_t admission (_public_api);
    if (!admission.acquired ())
        return -1;

    if (!target_node_rid_ || target_node_rid_->size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (ensure_healthy () != 0)
        return -1;

    const std::string rid_key = zlink::routing_id_key (target_node_rid_);

    std::vector<std::string> endpoints;
    {
        scoped_lock_t lock (_sync);
        std::map<std::string, std::set<std::string>>::const_iterator it =
          _peer_state.peer_endpoints_by_rid.find (rid_key);
        if (it != _peer_state.peer_endpoints_by_rid.end ())
            endpoints.assign (it->second.begin (), it->second.end ());
    }

    if (endpoints.empty ()) {
        errno = ENOENT;
        return -1;
    }

    bool any_disconnected = false;
    for (size_t i = 0; i < endpoints.size (); ++i) {
        if (send_data_plane_command (spot_control_protocol::cmd_disconnect_peer_pub,
                                     endpoints[i].c_str ())
            == 0) {
            any_disconnected = true;
            scoped_lock_t lock (_sync);
            if (_peer_state.active_endpoints.erase (endpoints[i]) != 0)
                _endpoint_state.active_peer_count.fetch_sub (1, std::memory_order_acq_rel);
            _peer_state.manual_endpoints.erase (endpoints[i]);
            _peer_state.discovery_endpoints.erase (endpoints[i]);
            _peer_state.peer_weight_by_endpoint.erase (endpoints[i]);
            _peer_state.observations[endpoints[i]].last_changed_ms = zlink::clock_t ().now_ms ();
            _peer_state.observations[endpoints[i]].connected_since_ms = 0;
            _summary_state.summary_last_changed_ms = zlink::clock_t ().now_ms ();
        }
    }

    {
        scoped_lock_t lock (_sync);
        _peer_state.peer_weight_by_rid.erase (rid_key);
        _peer_state.peer_endpoints_by_rid.erase (rid_key);
    }

    if (!any_disconnected) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}
}
