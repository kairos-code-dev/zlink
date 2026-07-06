/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/spots/spot_node_host_service.hpp"

#include <zlink.hpp>

#include "runtime/locations/location_value_codec.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <algorithm>
#include <cstdlib>
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
    std::map<std::string, peer_location_t> active_peers;

    explicit native_node_t (detail::spot_node_runtime_t runtime_) :
        context (),
        node (std::make_shared<zlink::service::spot_node_t> (context)),
        runtime (std::move (runtime_))
    {
    }
};

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

void connect_spot_peer (const spot_node_snapshot_t &snapshot,
                        const peer_location_t &local,
                        zlink::service::spot_node_t &node,
                        const peer_location_t &peer)
{
    if (peer.endpoint.empty () || is_manual_spot_endpoint (snapshot, peer.endpoint)) {
        return;
    }
    trace_spot_dial (local, peer);
    node.connect_peer (peer.endpoint);
    if (const auto found = peer.metadata.find ("pub-endpoint");
        found != peer.metadata.end () && !found->second.empty ()
        && !is_manual_spot_endpoint (snapshot, found->second)) {
        node.connect_peer (found->second);
    }
}

void disconnect_spot_peer (const spot_node_snapshot_t &snapshot,
                           zlink::service::spot_node_t &node,
                           const peer_location_t &peer)
{
    if (!peer.endpoint.empty () && !is_manual_spot_endpoint (snapshot, peer.endpoint)) {
        node.disconnect_peer (peer.endpoint);
    }
    if (const auto found = peer.metadata.find ("pub-endpoint");
        found != peer.metadata.end () && !found->second.empty ()
        && !is_manual_spot_endpoint (snapshot, found->second)) {
        node.disconnect_peer (found->second);
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
    auto rows =
      store
        .list_peers (peer_location_filter_t{
          .auto_connect_type = location_auto_connect_type_t::spot_mesh,
          .mesh_name = *snapshot.discovery_channel_name,
          .role = location_role_t::spot})
        .result ()
        .value ();
    std::map<std::string, peer_location_t> desired;
    for (auto &row : rows) {
        if (row.endpoint.empty () || is_local_spot_peer (*native.local_peer, row)
            || !local_spot_is_initiator (*native.local_peer, row)) {
            continue;
        }
        desired[spot_target_key (row)] = std::move (row);
    }
    trace_spot_scan (*native.local_peer, rows.size (), desired.size ());
    for (const auto &[key, peer] : desired) {
        const auto found = native.active_peers.find (key);
        if (found == native.active_peers.end ()) {
            connect_spot_peer (snapshot, *native.local_peer, *native.node, peer);
            native.active_peers[key] = peer;
            continue;
        }
        if (found->second.endpoint != peer.endpoint || found->second.owner_id != peer.owner_id) {
            disconnect_spot_peer (snapshot, *native.node, found->second);
            connect_spot_peer (snapshot, *native.local_peer, *native.node, peer);
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
        auto native = std::make_unique<native_node_t> (configured.runtime);
        if (snapshot.routing_id) {
            native->node->set_routing_id (*snapshot.routing_id);
            if (snapshot.pub_bind_endpoint) {
                native->node->set_publisher_routing_id (
                  derive_routing_id (*snapshot.routing_id, "pub"));
                native->node->set_subscriber_routing_id (
                  derive_routing_id (*snapshot.routing_id, "sub"));
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
        for (const auto &endpoint : snapshot.pub_sub_manual_connections) {
            native->node->connect_peer (endpoint);
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
                native->runtime.publish_peer_snapshot_if_changed ();
                dispatched += native->runtime.drain_actor_packets (services, serializers);
                dispatched += native->runtime.drain_routed_packets (services, serializers);
                dispatched += native->runtime.drain_subscriptions (services, serializers);
            }
            if (dispatched == 0) {
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
            }
        }
    });
}

void spot_node_host_service_t::stop () noexcept
{
    _running.store (false, std::memory_order_release);
    if (_receive_thread.joinable ()) {
        try {
            _receive_thread.join ();
        }
        catch (...) {
        }
    }
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
            for (const auto &[_, peer] : native->active_peers) {
                const auto configured = std::find_if (
                  _spot_nodes.begin (), _spot_nodes.end (), [&] (const auto &candidate) {
                      return candidate.runtime.node_rid ().value ()
                             == native->runtime.node_rid ().value ();
                  });
                if (configured != _spot_nodes.end ()) {
                    disconnect_spot_peer (configured->snapshot, *native->node, peer);
                }
            }
            native->active_peers.clear ();
        }
        catch (...) {
        }
        try {
            native->runtime.detach_native_node ();
        }
        catch (...) {
        }
        try {
            native->node->close ();
            native->node.reset ();
        }
        catch (...) {
        }
        try {
            native->context.options ().blocky (false);
        }
        catch (...) {
        }
        try {
            native->context.term ();
        }
        catch (...) {
        }
    }
    _nodes.clear ();
    _location_runtime = nullptr;
    _location_store = nullptr;
}

} // namespace zlink::framework::runtime
