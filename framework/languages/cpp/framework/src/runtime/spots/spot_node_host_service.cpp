/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/spots/spot_node_host_service.hpp"

#include "runtime/configuration/endpoint_connections.hpp"

#include <zlink.hpp>

#include "runtime/locations/location_value_codec.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace zlink::framework::runtime
{

namespace
{

void trace_spot_node_stop (std::string_view stage)
{
    const char *value = std::getenv ("ZLINK_CPP_HOST_STOP_TRACE");
    if (value == nullptr || std::string_view (value) == "0" || std::string_view (value) == "") {
        return;
    }
    std::cerr << "zlink-cpp-host-stop stage=spot-node-" << stage << std::endl;
}

zlink::routing_id_t derive_routing_id (const zlink::routing_id_t &base, std::string_view suffix)
{
    auto bytes = base.to_bytes ();
    if (bytes.size () + 1u + suffix.size () > 255u) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "derived routing id must be at most 255 bytes");
    }
    bytes.push_back (0);
    bytes.insert (bytes.end (), suffix.begin (), suffix.end ());
    return zlink::routing_id_t::from (bytes);
}

} // namespace

struct spot_node_host_service_t::native_node_t
{
    zlink::context_t context;
    std::shared_ptr<zlink::service::spot_node_t> node;
    detail::spot_node_runtime_t runtime;
    std::optional<peer_location_t> local_peer;
    std::int64_t local_generation = 0;
    bool local_published = false;
    bool node_close_failed = false;
    std::map<std::string, peer_location_t> active_peers;

    native_node_t (detail::spot_node_runtime_t runtime_, zlink::spot_node_mode_t mode_) :
        context (),
        node (std::make_shared<zlink::service::spot_node_t> (context, mode_)),
        runtime (std::move (runtime_))
    {
    }
};

zlink::spot_node_mode_t native_spot_node_mode (const spot_node_snapshot_t &snapshot)
{
    if (snapshot.router_bind_endpoint && snapshot.pub_bind_endpoint) {
        return zlink::spot_node_mode_t::all;
    }
    if (snapshot.router_bind_endpoint) {
        return zlink::spot_node_mode_t::routed;
    }
    return zlink::spot_node_mode_t::pubsub;
}

peer_location_key_t key_of (const peer_location_t &peer)
{
    return peer_location_key_t{peer.auto_connect_type, peer.mesh_name, peer.role, peer.node_rid,
                               peer.endpoint};
}

std::string spot_target_key (const peer_location_t &peer)
{
    const auto identity = peer.node_rid ? peer.node_rid->to_hex () : peer.endpoint;
    return location_value_codec_t::to_canonical_string (peer.role) + "|" + identity;
}

bool is_local_spot_peer (const peer_location_t &local, const peer_location_t &peer)
{
    if (local.node_rid && peer.node_rid && local.node_rid->to_hex () == peer.node_rid->to_hex ()) {
        return true;
    }
    return !local.endpoint.empty () && peer.endpoint == local.endpoint;
}

bool local_spot_is_initiator (const peer_location_t &local, const peer_location_t &peer)
{
    if (local.endpoint.empty ()) {
        return true;
    }
    if (local.node_rid && peer.node_rid) {
        const auto by_rid = local.node_rid->to_hex ().compare (peer.node_rid->to_hex ());
        if (by_rid != 0) {
            return by_rid < 0;
        }
    }
    return local.endpoint < peer.endpoint;
}

bool is_manual_spot_endpoint (const spot_node_snapshot_t &snapshot, const std::string &endpoint)
{
    return std::find (snapshot.router_manual_connections.begin (),
                      snapshot.router_manual_connections.end (),
                      endpoint) != snapshot.router_manual_connections.end ()
           || std::find_if (snapshot.router_manual_rid_connections.begin (),
                            snapshot.router_manual_rid_connections.end (),
                            [&] (const auto &connection) {
                                return connection.second == endpoint;
                            })
                != snapshot.router_manual_rid_connections.end ()
           || std::find (snapshot.pub_sub_manual_connections.begin (),
                         snapshot.pub_sub_manual_connections.end (),
                         endpoint) != snapshot.pub_sub_manual_connections.end ();
}

bool trace_enabled ()
{
    const char *value = std::getenv ("ZLINK_CPP_AUTO_CONNECT_TRACE");
    return value != nullptr && *value != '\0';
}

std::string write_status_name (location_write_status_t status)
{
    switch (status) {
        case location_write_status_t::stored:
            return "stored";
        case location_write_status_t::ignored_stale:
            return "ignored-stale";
        case location_write_status_t::rejected_conflict:
            return "rejected-conflict";
    }
    return "unknown";
}

void trace_spot_publish (const peer_location_t &row, location_write_status_t status)
{
    if (!trace_enabled ()) {
        return;
    }
    std::cerr << "zlink auto-connect publish"
              << " status=" << write_status_name (status)
              << " type=spot"
              << " mesh=" << row.mesh_name
              << " role=" << location_value_codec_t::to_canonical_string (row.role)
              << " rid=" << (row.node_rid ? row.node_rid->to_string () : "")
              << " endpoint=" << row.endpoint << "\n";
}

void trace_spot_scan (const peer_location_t &local, std::size_t rows, std::size_t desired)
{
    if (!trace_enabled ()) {
        return;
    }
    std::cerr << "zlink auto-connect scan"
              << " type=spot"
              << " mesh=" << local.mesh_name
              << " role=" << location_value_codec_t::to_canonical_string (local.role)
              << " rows=" << rows
              << " desired=" << desired << "\n";
}

void trace_spot_scan_error (const peer_location_t &local, const std::exception &error)
{
    if (!trace_enabled ()) {
        return;
    }
    std::cerr << "zlink auto-connect scan-failed"
              << " type=spot"
              << " mesh=" << local.mesh_name
              << " role=" << location_value_codec_t::to_canonical_string (local.role)
              << " error=" << error.what () << "\n";
}

void trace_spot_dial (const peer_location_t &local, const peer_location_t &peer)
{
    if (!trace_enabled ()) {
        return;
    }
    std::cerr << "zlink auto-connect dial"
              << " type=spot"
              << " mesh=" << local.mesh_name
              << " fromRole=" << location_value_codec_t::to_canonical_string (local.role)
              << " targetRole=" << location_value_codec_t::to_canonical_string (peer.role)
              << " targetRid=" << (peer.node_rid ? peer.node_rid->to_string () : "")
              << " endpoint=" << peer.endpoint << "\n";
}

void trace_spot_connect_error (const peer_location_t &peer, const std::exception &error)
{
    if (!trace_enabled ()) {
        return;
    }
    std::cerr << "zlink auto-connect connect-failed"
              << " type=spot"
              << " targetRid=" << (peer.node_rid ? peer.node_rid->to_string () : "")
              << " endpoint=" << peer.endpoint
              << " error=" << error.what () << "\n";
}

void trace_spot_disconnect_error (const peer_location_t &peer, const std::exception &error)
{
    if (!trace_enabled ()) {
        return;
    }
    std::cerr << "zlink auto-connect disconnect-failed"
              << " type=spot"
              << " targetRid=" << (peer.node_rid ? peer.node_rid->to_string () : "")
              << " endpoint=" << peer.endpoint
              << " error=" << error.what () << "\n";
}

void connect_spot_peer (const spot_node_snapshot_t &snapshot,
                        const peer_location_t &local,
                        zlink::service::spot_node_t &node,
                        const peer_location_t &peer,
                        bool connect_router)
{
    trace_spot_dial (local, peer);
    if (connect_router && !peer.endpoint.empty ()
        && !is_manual_spot_endpoint (snapshot, peer.endpoint)) {
        try {
            if (peer.node_rid) {
                node.connect_peer_rid (*peer.node_rid, peer.endpoint);
            } else {
                node.connect_peer (peer.endpoint);
            }
        }
        catch (const std::exception &error) {
            trace_spot_connect_error (peer, error);
        }
    }
    if (const auto found = peer.metadata.find ("pub-endpoint");
        found != peer.metadata.end () && !found->second.empty ()
        && !is_manual_spot_endpoint (snapshot, found->second)) {
        try {
            node.connect_peer (found->second);
        }
        catch (const std::exception &error) {
            trace_spot_connect_error (peer, error);
        }
    }
}

void disconnect_spot_peer (const spot_node_snapshot_t &snapshot,
                           zlink::service::spot_node_t &node,
                           const peer_location_t &peer)
{
    if (!peer.endpoint.empty () && !is_manual_spot_endpoint (snapshot, peer.endpoint)) {
        try {
            if (peer.node_rid) {
                node.disconnect_peer_rid (*peer.node_rid);
            } else {
                node.disconnect_peer (peer.endpoint);
            }
        }
        catch (const std::exception &error) {
            trace_spot_disconnect_error (peer, error);
        }
    }
    if (const auto found = peer.metadata.find ("pub-endpoint");
        found != peer.metadata.end () && !found->second.empty ()
        && !is_manual_spot_endpoint (snapshot, found->second)) {
        try {
            node.disconnect_peer (found->second);
        }
        catch (const std::exception &error) {
            trace_spot_disconnect_error (peer, error);
        }
    }
}

void publish_local_spot_peer (spot_node_host_service_t::native_node_t &native,
                              location_runtime_t &runtime)
{
    if (!native.local_peer || native.local_published) {
        return;
    }
    auto row = *native.local_peer;
    const auto claim = runtime.write_peer (row, location_write_intent_t::new_claim);
    trace_spot_publish (row, claim.status);
    if (claim.status == location_write_status_t::stored) {
        native.local_generation = claim.generation;
        native.local_published = true;
        return;
    }
    if (claim.status == location_write_status_t::rejected_conflict
        && native.local_generation > 0) {
        row.generation = native.local_generation;
        const auto renewed = runtime.write_peer (row, location_write_intent_t::renew);
        trace_spot_publish (row, renewed.status);
        native.local_published = renewed.status == location_write_status_t::stored;
    }
}

void reconcile_spot_mesh (spot_node_host_service_t::native_node_t &native,
                          const spot_node_snapshot_t &snapshot,
                          location_store_t &store)
{
    if (!native.local_peer || !snapshot.discovery_channel_name || !native.node) {
        return;
    }
    auto rows_result =
      store
        .list_peers (peer_location_filter_t{
          .auto_connect_type = location_auto_connect_type_t::spot_mesh,
          .mesh_name = *snapshot.discovery_channel_name,
          .role = location_role_t::spot})
        .result ();
    if (!rows_result) {
        if (const auto *error = rows_result.error ()) {
            trace_spot_scan_error (*native.local_peer, *error);
        }
        return;
    }
    auto rows = rows_result.value ();
    std::map<std::string, peer_location_t> desired;
    for (auto &row : rows) {
        if (row.endpoint.empty () || is_local_spot_peer (*native.local_peer, row)) {
            continue;
        }
        desired[spot_target_key (row)] = std::move (row);
    }
    trace_spot_scan (*native.local_peer, rows.size (), desired.size ());
    for (const auto &[key, peer] : desired) {
        const auto found = native.active_peers.find (key);
        if (found == native.active_peers.end ()) {
            connect_spot_peer (snapshot, *native.local_peer, *native.node, peer,
                               local_spot_is_initiator (*native.local_peer, peer));
            native.active_peers[key] = peer;
            continue;
        }
        if (found->second.endpoint != peer.endpoint || found->second.owner_id != peer.owner_id) {
            disconnect_spot_peer (snapshot, *native.node, found->second);
            connect_spot_peer (snapshot, *native.local_peer, *native.node, peer,
                               local_spot_is_initiator (*native.local_peer, peer));
            native.active_peers[key] = peer;
        }
    }
    for (auto it = native.active_peers.begin (); it != native.active_peers.end ();) {
        if (desired.find (it->first) == desired.end ()) {
            disconnect_spot_peer (snapshot, *native.node, it->second);
            it = native.active_peers.erase (it);
        } else {
            ++it;
        }
    }
}

spot_node_host_service_t::spot_node_host_service_t (std::vector<node_runtime_t> spot_nodes) :
    _spot_nodes (std::move (spot_nodes))
{
}

spot_node_host_service_t::~spot_node_host_service_t () = default;

void spot_node_host_service_t::start (service_provider_t &services)
{
    auto &serializers = services.get_required<serializer_registry_t> ();
    _location_runtime = &services.get_required<location_runtime_t> ();
    _location_store = &services.get_required<location_store_t> ();
    for (const auto &configured : _spot_nodes) {
        const auto &snapshot = configured.snapshot;
        if (!snapshot.router_bind_endpoint && !snapshot.pub_bind_endpoint) {
            continue;
        }
        auto native =
          std::make_unique<native_node_t> (configured.runtime, native_spot_node_mode (snapshot));
        if (snapshot.routing_id) {
            native->node->set_routing_id (*snapshot.routing_id);
            if (snapshot.pub_bind_endpoint) {
                native->node->set_publisher_routing_id (
                  derive_routing_id (*snapshot.routing_id, "pub"));
                native->node->set_subscriber_routing_id (
                  derive_routing_id (*snapshot.routing_id, "sub"));
            }
            auto entry_spot = native->node->entry_spot ();
            if (entry_spot.routing_id ().to_string () != snapshot.routing_id->to_string ()) {
                entry_spot.set_routing_id (*snapshot.routing_id);
            }
        }
        if (snapshot.router_bind_endpoint) {
            native->node->set_router_bind (*snapshot.router_bind_endpoint);
        }
        if (snapshot.pub_bind_endpoint) {
            native->node->set_pub_bind (*snapshot.pub_bind_endpoint);
        }
        for (const auto &endpoint : snapshot.router_manual_connections) {
            native->node->connect_peer (endpoint);
        }
        for (const auto &connection : snapshot.router_manual_rid_connections) {
            native->node->connect_peer_rid (connection.first, connection.second);
        }
        for (const auto &endpoint : snapshot.pub_sub_manual_connections) {
            native->node->connect_peer (endpoint);
        }
        /* endpoint_connections live attach (CONN-001): role handles mutate
         * this native node from now on; attach replays only the endpoints
         * added through the handle (builder connects stay in the loops
         * above). */
        if (configured.router_connections) {
            auto handle = *configured.router_connections;
            detail::endpoint_connections_runtime_t::attach (
              handle,
              [node = native->node] (const std::string &endpoint) { node->connect_peer (endpoint); },
              [node = native->node] (const std::string &endpoint) {
                  node->disconnect_peer (endpoint);
              });
        }
        if (configured.pub_sub_connections) {
            auto handle = *configured.pub_sub_connections;
            detail::endpoint_connections_runtime_t::attach (
              handle,
              [node = native->node] (const std::string &endpoint) { node->connect_peer (endpoint); },
              [node = native->node] (const std::string &endpoint) {
                  node->disconnect_peer (endpoint);
              });
        }
        if (snapshot.discovery_channel_name && snapshot.router_bind_endpoint) {
            native->local_peer =
              peer_location_t{.auto_connect_type = location_auto_connect_type_t::spot_mesh,
                              .mesh_name = *snapshot.discovery_channel_name,
                              .node_rid = snapshot.routing_id,
                              .role = location_role_t::spot,
                              .endpoint = *snapshot.router_bind_endpoint,
                              .weight = 100,
                              .value = 0};
            if (snapshot.pub_bind_endpoint) {
                native->local_peer->metadata["pub-endpoint"] = *snapshot.pub_bind_endpoint;
            }
            if (!snapshot.actor_types.empty ()) {
                // Framework-reserved capability encoding ("actor:<type>"): drain
                // handoff target selection compares the configured actor type,
                // not a language runtime type name (.NET ZLinkPeerCapabilities).
                std::set<std::string> capabilities;
                for (const auto &actor_type : snapshot.actor_types) {
                    capabilities.insert ("actor:" + actor_type);
                }
                native->local_peer->capabilities.assign (capabilities.begin (),
                                                         capabilities.end ());
            }
            publish_local_spot_peer (*native, *_location_runtime);
        }
        native->runtime.attach_native_node (native->node);
        _nodes.push_back (std::move (native));
    }
    _running.store (true, std::memory_order_release);
    _receive_thread = std::thread ([this, &services, &serializers] {
        while (_running.load (std::memory_order_acquire)) {
            std::size_t dispatched = 0;
            for (const auto &native : _nodes) {
                if (!native) {
                    continue;
                }
                try {
                    if (_location_runtime != nullptr && _location_store != nullptr) {
                        publish_local_spot_peer (*native, *_location_runtime);
                        const auto configured = std::find_if (
                          _spot_nodes.begin (), _spot_nodes.end (), [&] (const auto &candidate) {
                              return candidate.runtime.node_rid ().value ()
                                     == native->runtime.node_rid ().value ();
                          });
                        if (configured != _spot_nodes.end ()) {
                            reconcile_spot_mesh (*native, configured->snapshot, *_location_store);
                        }
                    }
                    //  Draining also reaches native configuration calls, which throw. Keeping it
                    //  inside the guard is what stops one of those from unwinding out of this
                    //  thread and terminating the process.
                    native->runtime.publish_peer_snapshot_if_changed ();
                    dispatched += native->runtime.cleanup_expired_actor_admissions ();
                    dispatched += native->runtime.drain_actor_packets (services, serializers);
                    dispatched += native->runtime.drain_routed_packets (services, serializers);
                    dispatched += native->runtime.drain_subscriptions (services, serializers);
                }
                catch (const std::exception &error) {
                    if (native->local_peer) {
                        trace_spot_scan_error (*native->local_peer, error);
                    }
                }
                catch (...) {
                }
            }
            if (dispatched == 0) {
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
            }
        }
    });
}

void spot_node_host_service_t::request_stop () noexcept
{
    _running.store (false, std::memory_order_release);
    for (auto &native : _nodes) {
        if (native) {
            native->runtime.request_stop ();
            native->runtime.cancel_timers ();
            native->runtime.cancel_pending_dispatch ();
        }
    }
}

void spot_node_host_service_t::stop () noexcept
{
    trace_spot_node_stop ("stop-begin");
    request_stop ();
    if (_receive_thread.joinable ()) {
        try {
            trace_spot_node_stop ("join-receive-begin");
            _receive_thread.join ();
            trace_spot_node_stop ("join-receive-end");
        }
        catch (...) {
        }
    }
    trace_spot_node_stop ("cancel-timers-begin");
    for (auto &native : _nodes) {
        if (native) {
            native->runtime.cancel_timers ();
        }
    }
    trace_spot_node_stop ("cancel-timers-end");
    trace_spot_node_stop ("remove-peers-begin");
    for (auto &native : _nodes) {
        if (!native) {
            continue;
        }
        try {
            if (_location_runtime != nullptr && native->local_peer && native->local_published) {
                (void) _location_runtime->remove_peer (key_of (*native->local_peer),
                                                       native->local_generation);
                native->local_published = false;
            }
            // Shutdown must not wait for data-plane replies from workers or peers that are
            // being stopped concurrently. Native node close below owns the socket teardown.
            native->active_peers.clear ();
        }
        catch (...) {
        }
    }
    trace_spot_node_stop ("remove-peers-end");
    trace_spot_node_stop ("cancel-pending-begin");
    for (auto &native : _nodes) {
        if (native) {
            native->runtime.cancel_pending_work ();
        }
    }
    trace_spot_node_stop ("cancel-pending-end");
    trace_spot_node_stop ("close-nodes-begin");
    for (auto &native : _nodes) {
        if (native) {
            try {
                trace_spot_node_stop ("detach-native-node");
                native->runtime.detach_native_node ();
            }
            catch (...) {
                native->node_close_failed = true;
            }
            try {
                trace_spot_node_stop ("node-close");
                native->node->close ();
            }
            catch (const std::exception &error) {
                native->node_close_failed = true;
                std::cerr << "zlink framework spot node close failed: " << error.what () << '\n';
            }
            catch (...) {
                native->node_close_failed = true;
                std::cerr << "zlink framework spot node close failed\n";
            }
        }
    }
    trace_spot_node_stop ("close-nodes-end");
    trace_spot_node_stop ("context-close-begin");
    for (auto &native : _nodes) {
        if (!native) {
            continue;
        }
        try {
            /* The runtime still owns the native SPOT and actor handles created
             * on this context; a live socket keeps the context term waiting
             * forever, so they are released before the node and the context. */
            trace_spot_node_stop ("release-native-handles");
            native->runtime.release_native_handles ();
        }
        catch (...) {
        }
        try {
            trace_spot_node_stop ("node-reset");
            native->node.reset ();
        }
        catch (...) {
        }
        try {
            trace_spot_node_stop ("context-blocky-false");
            native->context.options ().blocky (false);
        }
        catch (...) {
        }
        try {
            trace_spot_node_stop ("context-shutdown");
            native->context.shutdown ();
        }
        catch (...) {
        }
        try {
            trace_spot_node_stop ("context-term");
            native->context.term ();
        }
        catch (...) {
        }
    }
    trace_spot_node_stop ("context-close-end");
    _nodes.clear ();
    _location_runtime = nullptr;
    _location_store = nullptr;
    trace_spot_node_stop ("stop-end");
}

} // namespace zlink::framework::runtime
