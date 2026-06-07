/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/c_api_copy_internal.hpp"
#include "services/discovery/socket_discovery_attachment.hpp"

#include "services/discovery/discovery.hpp"
#include "services/discovery/discovery_access.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/routing_id.hpp"

namespace zlink
{
void socket_discovery_attachment_t::report_topology (discovery_t *discovery_,
                                                     uint16_t local_role_,
                                                     const std::string &advertise_endpoint_,
                                                     uint16_t state_,
                                                     uint32_t desired_count_,
                                                     uint32_t ready_count_,
                                                     int error_code_,
                                                     bool flush_immediately_)
{
    if (!discovery_ || advertise_endpoint_.empty ())
        return;

    zlink_routing_id_t routing_id;
    if (!ensure_socket_routing_id (&routing_id))
        return;

    zlink_registry_topology_entry_t entry;
    memset (&entry, 0, sizeof (entry));
    entry.routing_id = routing_id;
    entry.service_kind = ZLINK_SERVICE_KIND_SOCKET;
    entry.service_role = static_cast<zlink_service_role_t> (local_role_);
    entry.auto_connect_type =
      static_cast<zlink_auto_connect_type_t> (discovery_->auto_connect_type ());
    copy_fixed_c_string_from_cstr (entry.channel_name, sizeof (entry.channel_name),
                                   discovery_->channel_name ().c_str ());
    copy_fixed_c_string_from_cstr (entry.endpoint, sizeof (entry.endpoint),
                                   advertise_endpoint_.c_str ());
    entry.source = ZLINK_TOPOLOGY_SOURCE_DISCOVERY;
    entry.state = static_cast<zlink_topology_state_t> (state_);
    entry.desired_count = desired_count_;
    entry.ready_count = ready_count_;
    entry.error_code = static_cast<uint32_t> (error_code_ > 0 ? error_code_ : 0);
    entry.last_reported_ms = zlink::clock_t ().now_ms ();
    discovery_access_t::upsert_service_summary (discovery_, entry);
    if (flush_immediately_)
        discovery_access_t::flush_topology_reports (discovery_);
}

void socket_discovery_attachment_t::refresh_peers (discovery_t *discovery_,
                                                   uint16_t local_role_,
                                                   const std::string &advertise_endpoint_,
                                                   bool shutdown_requested_)
{
    if (!discovery_ || shutdown_requested_)
        return;

    zlink_routing_id_t local_routing_id;
    memset (&local_routing_id, 0, sizeof (local_routing_id));
    if ((local_role_ == discovery_protocol::service_role_router
         || local_role_ == discovery_protocol::service_role_dealer)
        && !ensure_socket_routing_id (&local_routing_id)) {
        return;
    }

    std::vector<provider_info_t> providers;
    discovery_->snapshot_providers (discovery_->channel_name (), &providers);

    const std::string local_rid = zlink::routing_id_key (local_routing_id);
    std::set<std::string> target_endpoints;
    std::map<std::string, std::string> target_peers_by_rid;
    for (size_t i = 0; i < providers.size (); ++i) {
        const provider_info_t &provider = providers[i];
        const std::string remote_rid = zlink::routing_id_key (provider.routing_id);
        if (provider.endpoint.empty () || advertise_endpoint_ == provider.endpoint
            || (!local_rid.empty () && local_rid == remote_rid)
            || !discovery_protocol::socket_auto_connect_target_matches (
              discovery_->auto_connect_type (), local_role_, provider.service_role,
              local_routing_id, provider.routing_id, advertise_endpoint_, provider.endpoint)) {
            continue;
        }
        target_endpoints.insert (provider.endpoint);
        if (!remote_rid.empty ())
            target_peers_by_rid[remote_rid] = provider.endpoint;
    }

    std::set<std::string> active_endpoints;
    std::map<std::string, std::string> active_peers_by_rid;
    {
        scoped_lock_t lock (_sync);
        if (_discovery != discovery_ || _shutdown_requested)
            return;
        active_endpoints = _active_peer_endpoints;
        active_peers_by_rid = _active_peers_by_rid;
        _discovery_managed_peer_endpoints = target_endpoints;
        _discovery_managed_peers_by_rid = target_peers_by_rid;
        _refresh_seq = discovery_->service_update_seq (discovery_->channel_name ());
    }

    for (std::map<std::string, std::string>::const_iterator it = target_peers_by_rid.begin ();
         it != target_peers_by_rid.end (); ++it) {
        std::map<std::string, std::string>::const_iterator active =
          active_peers_by_rid.find (it->first);
        if (active != active_peers_by_rid.end () && active->second == it->second)
            continue;
        if (active != active_peers_by_rid.end ())
            (void) _socket->service_attachment_term_endpoint (active->second.c_str ());
        if (_socket->service_attachment_connect (it->second.c_str ()) == 0) {
            scoped_lock_t lock (_sync);
            if (_discovery == discovery_ && !_shutdown_requested) {
                _active_peer_endpoints.erase (
                  active == active_peers_by_rid.end () ? std::string () : active->second);
                _active_peer_endpoints.insert (it->second);
                _active_peers_by_rid[it->first] = it->second;
            }
        }
    }

    for (std::map<std::string, std::string>::const_iterator it = active_peers_by_rid.begin ();
         it != active_peers_by_rid.end (); ++it) {
        if (target_peers_by_rid.find (it->first) != target_peers_by_rid.end ())
            continue;
        if (_socket->service_attachment_term_endpoint (it->second.c_str ()) == 0) {
            scoped_lock_t lock (_sync);
            _active_peer_endpoints.erase (it->second);
            _active_peers_by_rid.erase (it->first);
        }
    }

    if (!advertise_endpoint_.empty ()) {
        size_t ready_count = 0;
        {
            scoped_lock_t lock (_sync);
            if (_discovery != discovery_)
                return;
            ready_count = _active_peer_endpoints.size ();
        }
        report_topology (discovery_, local_role_, advertise_endpoint_, ZLINK_TOPOLOGY_STATE_READY,
                         static_cast<uint32_t> (target_endpoints.size ()),
                         static_cast<uint32_t> (ready_count), 0, false);
    }
}
}
