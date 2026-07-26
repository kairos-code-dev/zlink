/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once
#include "runtime/channels/channel_runtime.hpp"
#include "runtime/channels/channel_runtime_bundle.hpp"
#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/client_server/client_server_location_runtime.hpp"
#include "runtime/fanout/fanout_location_runtime.hpp"
#include "runtime/locations/location_runtime.hpp"
#include "runtime/locations/live_location_reader.hpp"
#include "runtime/locations/store_location_resolvers.hpp"
#include "runtime/locations/location_value_codec.hpp"
#include "runtime/mesh/mesh_node_runtime.hpp"

#include <zlink/framework/contracts/configuration/module.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <typeindex>
#include <utility>
#include <vector>

namespace zlink::framework::runtime
{

class location_auto_connect_host_service_t final : public hosted_service_t
{
  public:
    location_auto_connect_host_service_t (message_bus_t bus,
                                          std::vector<channel_snapshot_t> channels,
                                          handler_registry_t &handlers,
                                          serializer_registry_t &serializers,
                                          std::map<std::string, std::string>
                                            client_server_advertise_hosts = {},
                                          std::set<std::string> route_mesh_client_channels = {},
                                          std::vector<std::shared_ptr<detail::mesh_node_runtime_t>>
                                            mesh_nodes = {}) :
        _bus (std::move (bus)),
        _channels (std::move (channels)),
        _handlers (&handlers),
        _serializers (&serializers),
        _client_server_advertise_hosts (
          std::move (client_server_advertise_hosts)),
        _route_mesh_client_channels (std::move (route_mesh_client_channels)),
        _mesh_nodes (std::move (mesh_nodes))
    {
    }

    ~location_auto_connect_host_service_t () override { stop (); }

    void start (service_provider_t &services) override
    {
        _runtime = &services.get_required<location_runtime_t> ();
        _store = &services.get_required<live_location_reader_t> ();
        if (auto route_cache = services.get<store_location_resolvers_t> ()) {
            _route_cache = &route_cache->get ();
        }
        detail::channel_runtime_manager_t manager = detail::channel_runtime_manager_t::from (_bus);
        manager.initialize_publisher_channels ();
        manager.initialize_client_channels ();
        manager.initialize_inbound_channels ();

        auto &location_store =
          services.get_required<location_store_t> ();
        auto *client_server_store =
          dynamic_cast<client_server_location_store_t *> (
            &location_store);
        const auto needs_client_server =
          std::any_of (
            _channels.begin (), _channels.end (),
            [] (const auto &channel) {
                return (channel.server.enabled
                        && channel.server.discovery)
                       || (channel.client.enabled
                           && channel.client.discovery);
            });
        if (needs_client_server && client_server_store == nullptr)
            throw std::invalid_argument (
              "the configured location store does not implement the ClientServer location contract");
        if (client_server_store != nullptr) {
            _client_server = std::make_unique<
              client_server::client_server_location_runtime_t> (
              _bus, _channels, *_runtime, *client_server_store,
              location_store, services, *_serializers, *_handlers,
              _client_server_advertise_hosts);
            _client_server->start ();
        }

        auto *fanout_store =
          dynamic_cast<fanout_location_store_t *> (
            &location_store);
        const auto needs_fanout =
          std::any_of (
            _channels.begin (), _channels.end (),
            [] (const auto &channel) {
                return (channel.publisher.enabled
                        && channel.publisher.discovery)
                       || (channel.subscriber.enabled
                           && channel.subscriber.discovery);
            });
        if (needs_fanout && fanout_store == nullptr)
            throw std::invalid_argument (
              "the configured location store does not implement the fanout location contract");
        if (fanout_store != nullptr) {
            _fanout = std::make_unique<
              fanout::fanout_location_runtime_t> (
              _bus, _channels, *_runtime, *fanout_store,
              location_store, services, *_serializers,
              *_handlers);
            _fanout->start ();
        }
        std::set<std::string> route_loop_meshes;
        for (const auto &route_channel_id : manager.route_channel_ids ()) {
            auto &route = manager.get_route_channel (route_channel_id);
            route_loop_meshes.insert (route.router_channel_id ());
            auto manual = route.manual_connections ();
            auto connect_route = [this, &route, manual] (const auto &target) {
                if (std::find (manual.begin (), manual.end (), target.endpoint) == manual.end ()) {
                    if (target.node_rid) {
                        (void) route.connect (*target.node_rid, target.endpoint);
                        for (const auto &mesh_node : _mesh_nodes) {
                            if (mesh_node
                                && mesh_node->mesh_name () == route.router_channel_id ()) {
                                mesh_node->connect_peer (*target.node_rid, target.endpoint);
                            }
                        }
                    } else {
                        (void) route.connect (target.endpoint);
                    }
                }
            };
            auto disconnect_route = [this, &route, manual] (const auto &target) {
                if (std::find (manual.begin (), manual.end (), target.endpoint) == manual.end ()) {
                    (void) route.disconnect (target.endpoint);
                    for (const auto &mesh_node : _mesh_nodes) {
                        if (mesh_node
                            && mesh_node->mesh_name () == route.router_channel_id ()) {
                            mesh_node->disconnect_peer (target.endpoint);
                        }
                    }
                }
            };
            const auto route_rid = route.routing_id ();
            if (!route.bind_endpoint ().empty ()) {
                add_loop (local_t{location_auto_connect_type_t::route_mesh,
                                  route.router_channel_id (), location_role_t::router, route_rid,
                                  route.bind_endpoint (), 100},
                          nullptr, connect_route, disconnect_route);
            }
            const bool endpointless_client = route.bind_endpoint ().empty ();
            if (endpointless_client) {
                add_loop (local_t{location_auto_connect_type_t::route_mesh,
                                  route.router_channel_id (), location_role_t::router, route_rid,
                                  {}, 100},
                          nullptr, connect_route, disconnect_route);
            }
        }
        for (const auto &mesh_node : _mesh_nodes) {
            if (!mesh_node || route_loop_meshes.contains (mesh_node->mesh_name ()))
                continue;
            const auto local_rid = mesh_node->routing_id ();
            const auto local_endpoint = mesh_node->listen_endpoint ();
            auto connect_mesh = [mesh_node] (const auto &target) {
                if (target.node_rid)
                    mesh_node->connect_peer (*target.node_rid, target.endpoint);
            };
            auto disconnect_mesh = [mesh_node] (const auto &target) {
                mesh_node->disconnect_peer (target.endpoint);
            };
            add_loop (local_t{location_auto_connect_type_t::route_mesh,
                              mesh_node->mesh_name (), location_role_t::router, local_rid,
                              local_endpoint, 100},
                      nullptr, std::move (connect_mesh), std::move (disconnect_mesh));
        }

        _stop.store (false, std::memory_order_release);
        if (!_loops.empty ()) {
            detail::channel_runtime_t::from (_bus).mark_auto_connect_active ();
        }
        for (auto &loop : _loops) {
            loop.thread = std::thread ([this, &loop] { run_loop (loop); });
        }
    }

    void stop () noexcept override
    {
        _stop.store (true, std::memory_order_release);
        if (_client_server) {
            _client_server->stop ();
            _client_server.reset ();
        }
        if (_fanout) {
            _fanout->stop ();
            _fanout.reset ();
        }
        for (auto &loop : _loops) {
            if (loop.thread.joinable ()) {
                loop.thread.join ();
            }
            cleanup_loop (loop);
        }
        _loops.clear ();
    }

  private:
    struct local_t
    {
        location_auto_connect_type_t type = location_auto_connect_type_t::invalid;
        std::string mesh_name;
        location_role_t role = location_role_t::invalid;
        std::optional<zlink::routing_id_t> node_rid;
        std::string endpoint;
        std::uint32_t weight = 100;
    };

    struct target_t
    {
        std::string key;
        std::optional<zlink::routing_id_t> node_rid;
        location_role_t role = location_role_t::invalid;
        std::string endpoint;
        std::string owner_id;
        std::int64_t lifecycle_generation = 0;
        std::uint32_t weight = 100;
    };

    struct loop_t
    {
        local_t local;
        detail::channel_runtime_bundle_t *bundle = nullptr;
        std::optional<peer_location_t> local_row;
        std::int64_t local_generation = 0;
        bool local_published = false;
        std::map<std::string, target_t> active;
        std::function<void (const target_t &)> connect_target;
        std::function<void (const target_t &)> disconnect_target;
        std::size_t discovered = 0;
        std::map<std::string, target_t> last_desired;
        bool recovering_from_store_failure = false;
        std::optional<std::chrono::steady_clock::time_point> store_failure_started_at;
        std::optional<std::chrono::steady_clock::time_point> reconcile_after;
        std::thread thread;
    };

    /* Discovered peers for the location.peers observable (runtime-metrics
     * §4.5): everything discoverable in this loop's mesh view minus self —
     * intentionally wider than the dial set, matching the .NET reconciler. */
    static std::size_t count_discovered (const local_t &local,
                                         const std::vector<peer_location_t> &peers)
    {
        std::size_t count = 0;
        for (const auto &peer : peers) {
            if (peer.auto_connect_type == local.type && peer.mesh_name == local.mesh_name
                && role_allowed (local.type, peer.role) && !peer.endpoint.empty ()
                && !is_self (local, peer)) {
                count++;
            }
        }
        return count;
    }

    /* Folds this loop's count into the service total and emits one observable
     * sample when the total changes; each loop polls on its own thread, so the
     * gauge freshness follows the fastest tick (§7.2 polling reuse). */
    void observe_discovered (loop_t &loop, const std::vector<peer_location_t> &rows)
    {
        const auto count = count_discovered (loop.local, rows);
        std::size_t total = 0;
        {
            std::lock_guard lock (_peers_gate);
            _peers_total += count;
            _peers_total -= loop.discovered;
            loop.discovered = count;
            if (_peers_observed && *_peers_observed == _peers_total) {
                return;
            }
            _peers_observed = _peers_total;
            total = _peers_total;
        }
        _runtime->observe_discovered_peers (total);
    }

    void add_loop (local_t local,
                   detail::channel_runtime_bundle_t *bundle,
                   std::function<void (const target_t &)> connect_target = {},
                   std::function<void (const target_t &)> disconnect_target = {})
    {
        loop_t loop;
        loop.local = std::move (local);
        loop.bundle = bundle;
        loop.connect_target = std::move (connect_target);
        loop.disconnect_target = std::move (disconnect_target);
        if (loop.local.node_rid || !loop.local.endpoint.empty ()) {
            loop.local_row = peer_location_t{.auto_connect_type = loop.local.type,
                                             .mesh_name = loop.local.mesh_name,
                                             .node_rid = loop.local.node_rid,
                                             .role = loop.local.role,
                                             .endpoint = loop.local.endpoint,
                                             .weight = loop.local.weight,
                                             .value = 0};
        }
        if (!loop.local_row && loop.bundle == nullptr) {
            return;
        }
        _loops.push_back (std::move (loop));
    }

    void run_loop (loop_t &loop)
    {
        while (!_stop.load (std::memory_order_acquire)) {
            try {
                tick (loop);
            }
            catch (const std::exception &ex) {
                trace_error (loop.local, ex.what ());
            }
            catch (...) {
                trace_error (loop.local, "unknown");
            }
            std::this_thread::sleep_for (_runtime->options ().polling_interval);
        }
    }

    void tick (loop_t &loop)
    {
        publish_local (loop);
        std::vector<peer_location_t> rows;
        try {
            rows = _store
                     ->list_peers (peer_location_filter_t{.auto_connect_type = loop.local.type,
                                                          .mesh_name = loop.local.mesh_name})
                     .result ()
                     .value ();
        }
        catch (...) {
            if (!loop.store_failure_started_at) {
                loop.store_failure_started_at = std::chrono::steady_clock::now ();
            }
            loop.recovering_from_store_failure = true;
            retry_pending_targets (loop);
            _runtime->record_store_error ();
            throw;
        }
        if (loop.recovering_from_store_failure) {
            loop.recovering_from_store_failure = false;
            loop.store_failure_started_at.reset ();
            if (_route_cache != nullptr) {
                _route_cache->invalidate_all_routes_after_store_recovery ();
            }
            republish_after_store_recovery (loop);
            /* A restarted store can be empty. Give every live node one heartbeat
             * interval to restore its local rows, then observe the result on the
             * following polling tick before removing prior targets. */
            loop.reconcile_after =
              std::chrono::steady_clock::now () + _runtime->options ().owner_lease_renew_interval
              + _runtime->options ().polling_interval;
            return;
        }
        if (loop.reconcile_after) {
            if (std::chrono::steady_clock::now () < *loop.reconcile_after) {
                return;
            }
            loop.reconcile_after.reset ();
        }
        observe_discovered (loop, rows);
        auto desired = compute_desired (loop.local, rows);
        loop.last_desired = desired;
        trace_scan (loop.local, rows.size (), desired.size ());
        for (auto it = loop.active.begin (); it != loop.active.end ();) {
            if (desired.find (it->first) == desired.end ()) {
                disconnect (loop, it->second);
                it = loop.active.erase (it);
            } else {
                ++it;
            }
        }
        for (const auto &[key, target] : desired) {
            const auto found = loop.active.find (key);
            if (found == loop.active.end ()) {
                connect (loop, target);
                loop.active[key] = target;
                continue;
            }
            if (found->second.endpoint != target.endpoint
                || found->second.owner_id != target.owner_id
                || found->second.lifecycle_generation != target.lifecycle_generation) {
                disconnect (loop, found->second);
                connect (loop, target);
                loop.active[key] = target;
                continue;
            }
            if (found->second.weight != target.weight) {
                connect (loop, target);
                loop.active[key] = target;
            }
        }
    }

    void publish_local (loop_t &loop)
    {
        if (!loop.local_row) {
            return;
        }
        auto row = *loop.local_row;
        row.weight = current_local_weight (loop.local);
        if (loop.local_published) {
            if (row.weight == loop.local_row->weight) {
                return;
            }
            row.generation = loop.local_generation;
            const auto renewed = _runtime->write_peer (row, location_write_intent_t::renew);
            if (renewed.status == location_write_status_t::stored) {
                loop.local_row = row;
                return;
            }
            loop.local_published = false;
            return;
        }
        const auto claim = _runtime->write_peer (row, location_write_intent_t::new_claim);
        if (claim.status == location_write_status_t::stored) {
            loop.local_generation = claim.generation;
            row.generation = claim.generation;
            loop.local_row = row;
            loop.local_published = true;
            trace_publish (row, "stored");
            return;
        }
        if (claim.status == location_write_status_t::rejected_conflict
            && loop.local_generation > 0) {
            row.generation = loop.local_generation;
            const auto renewed = _runtime->write_peer (row, location_write_intent_t::renew);
            if (renewed.status == location_write_status_t::stored) {
                loop.local_row = row;
                loop.local_published = true;
                trace_publish (row, "renewed");
            }
        }
    }

    void republish_after_store_recovery (loop_t &loop)
    {
        if (!loop.local_row) {
            return;
        }
        loop.local_published = false;
        publish_local (loop);
    }

    void retry_pending_targets (loop_t &loop)
    {
        if (!loop.store_failure_started_at
            || _runtime->options ().store_failure_grace <= std::chrono::milliseconds::zero ()
            || std::chrono::steady_clock::now () - *loop.store_failure_started_at
                 > _runtime->options ().store_failure_grace) {
            return;
        }
        for (const auto &[key, target] : loop.last_desired) {
            if (loop.active.contains (key)) {
                continue;
            }
            connect (loop, target);
            loop.active[key] = target;
        }
    }

    std::uint32_t current_local_weight (const local_t &local) const
    {
        if (local.type == location_auto_connect_type_t::client_server
            && local.role == location_role_t::router) {
            const auto override =
              detail::channel_runtime_t::from (_bus).server_peer_weight_override (local.mesh_name);
            if (override) {
                return static_cast<std::uint32_t> (
                  std::min (*override, 100));
            }
        }
        return local.weight;
    }

    static std::map<std::string, target_t>
    compute_desired (const local_t &local, const std::vector<peer_location_t> &peers)
    {
        std::map<std::string, target_t> desired;
        for (const auto &peer : peers) {
            if (peer.auto_connect_type != local.type || peer.mesh_name != local.mesh_name
                || !role_allowed (local.type, peer.role) || peer.endpoint.empty ()
                || is_self (local, peer) || !should_dial (local, peer)) {
                continue;
            }
            auto target = target_t{target_key (peer), peer.node_rid, peer.role,
                                   peer.endpoint,     peer.owner_id,
                                   peer.generation,
                                   peer.draining ? 0u : peer.weight};
            desired[target.key] = std::move (target);
        }
        return desired;
    }

    static bool role_allowed (location_auto_connect_type_t type, location_role_t role)
    {
        switch (type) {
            case location_auto_connect_type_t::route_mesh:
                return role == location_role_t::router;
            case location_auto_connect_type_t::client_server:
                return role == location_role_t::router || role == location_role_t::dealer;
            case location_auto_connect_type_t::dealer_mesh:
                return role == location_role_t::dealer;
            case location_auto_connect_type_t::fanout:
                return false;
            case location_auto_connect_type_t::spot_mesh:
                return role == location_role_t::spot || role == location_role_t::router;
            case location_auto_connect_type_t::invalid:
                return false;
        }
        return false;
    }

    static bool is_self (const local_t &local, const peer_location_t &peer)
    {
        if (local.node_rid && peer.node_rid
            && local.node_rid->to_hex () == peer.node_rid->to_hex ()) {
            return true;
        }
        return !local.endpoint.empty () && peer.endpoint == local.endpoint;
    }

    static bool should_dial (const local_t &local, const peer_location_t &peer)
    {
        switch (local.type) {
            case location_auto_connect_type_t::client_server:
                return local.role == location_role_t::dealer
                       && peer.role == location_role_t::router;
            case location_auto_connect_type_t::fanout:
                return false;
            case location_auto_connect_type_t::route_mesh:
                return local.role == location_role_t::router && peer.role == location_role_t::router
                       && local_is_initiator (local, peer);
            case location_auto_connect_type_t::dealer_mesh:
                return local.role == peer.role && local_is_initiator (local, peer);
            case location_auto_connect_type_t::spot_mesh:
                return local.role == location_role_t::spot && peer.role == location_role_t::spot
                       && local_is_initiator (local, peer);
            case location_auto_connect_type_t::invalid:
                return false;
        }
        return false;
    }

    static bool local_is_initiator (const local_t &local, const peer_location_t &peer)
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

    static std::string target_key (const peer_location_t &peer)
    {
        const auto identity = peer.node_rid ? peer.node_rid->to_hex () : peer.endpoint;
        return location_value_codec_t::to_canonical_string (peer.role) + "|" + identity + "|"
               + std::to_string (peer.generation);
    }

    static peer_location_key_t key_of (const peer_location_t &row)
    {
        return peer_location_key_t{row.auto_connect_type, row.mesh_name, row.role, row.node_rid,
                                   row.endpoint};
    }

    static void connect (loop_t &loop, const target_t &target)
    {
        trace_connect (loop.local, target);
        if (loop.connect_target) {
            loop.connect_target (target);
            return;
        }
        if (loop.bundle == nullptr || target.endpoint.empty ()
            || loop.bundle->contains_manual_connection (target.endpoint)) {
            return;
        }
        (void) loop.bundle->try_add_auto_connection (target.endpoint, target.weight);
    }

    static bool trace_enabled ()
    {
        const char *value = std::getenv ("ZLINK_CPP_AUTO_CONNECT_TRACE");
        return value != nullptr && *value != '\0';
    }

    static long long trace_monotonic_ms ()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds> (
                 std::chrono::steady_clock::now ().time_since_epoch ())
          .count ();
    }

    static void trace_scan (const local_t &local, std::size_t rows, std::size_t desired)
    {
        if (!trace_enabled ()) {
            return;
        }
        std::cerr << "zlink auto-connect scan"
                  << " monotonicMs=" << trace_monotonic_ms ()
                  << " type=" << location_value_codec_t::to_canonical_string (local.type)
                  << " mesh=" << local.mesh_name
                  << " role=" << location_value_codec_t::to_canonical_string (local.role)
                  << " rows=" << rows
                  << " desired=" << desired << "\n";
    }

    static void trace_publish (const peer_location_t &row, std::string_view status)
    {
        if (!trace_enabled ()) {
            return;
        }
        std::cerr << "zlink auto-connect publish"
                  << " monotonicMs=" << trace_monotonic_ms ()
                  << " status=" << status
                  << " type=" << location_value_codec_t::to_canonical_string (
                       row.auto_connect_type)
                  << " mesh=" << row.mesh_name
                  << " role=" << location_value_codec_t::to_canonical_string (row.role)
                  << " rid=" << (row.node_rid ? row.node_rid->to_string () : "")
                  << " endpoint=" << row.endpoint << "\n";
    }

    static void trace_connect (const local_t &local, const target_t &target)
    {
        if (!trace_enabled ()) {
            return;
        }
        std::cerr << "zlink auto-connect dial"
                  << " monotonicMs=" << trace_monotonic_ms ()
                  << " type=" << location_value_codec_t::to_canonical_string (local.type)
                  << " mesh=" << local.mesh_name
                  << " fromRole=" << location_value_codec_t::to_canonical_string (local.role)
                  << " targetRole=" << location_value_codec_t::to_canonical_string (target.role)
                  << " targetRid=" << (target.node_rid ? target.node_rid->to_string () : "")
                  << " endpoint=" << target.endpoint << "\n";
    }

    static void trace_error (const local_t &local, std::string_view error)
    {
        if (!trace_enabled ()) {
            return;
        }
        std::cerr << "zlink auto-connect error"
                  << " monotonicMs=" << trace_monotonic_ms ()
                  << " type=" << location_value_codec_t::to_canonical_string (local.type)
                  << " mesh=" << local.mesh_name
                  << " role=" << location_value_codec_t::to_canonical_string (local.role)
                  << " error=" << error << "\n";
    }

    static void disconnect (loop_t &loop, const target_t &target)
    {
        if (loop.disconnect_target) {
            loop.disconnect_target (target);
            return;
        }
        if (loop.bundle == nullptr || target.endpoint.empty ()
            || loop.bundle->contains_manual_connection (target.endpoint)) {
            return;
        }
        loop.bundle->remove_auto_connection (target.endpoint);
    }

    void cleanup_loop (loop_t &loop) noexcept
    {
        try {
            for (const auto &[_, target] : loop.active) {
                disconnect (loop, target);
            }
            loop.active.clear ();
            if (loop.local_row && loop.local_published) {
                (void) _runtime->remove_peer (key_of (*loop.local_row), loop.local_generation);
                loop.local_published = false;
            }
        }
        catch (...) {
        }
    }

    message_bus_t _bus;
    std::vector<channel_snapshot_t> _channels;
    handler_registry_t *_handlers;
    serializer_registry_t *_serializers;
    std::map<std::string, std::string>
      _client_server_advertise_hosts;
    std::set<std::string> _route_mesh_client_channels;
    std::vector<std::shared_ptr<detail::mesh_node_runtime_t>> _mesh_nodes;
    location_runtime_t *_runtime = nullptr;
    live_location_reader_t *_store = nullptr;
    store_location_resolvers_t *_route_cache = nullptr;
    std::mutex _peers_gate;
    std::size_t _peers_total = 0;
    std::optional<std::size_t> _peers_observed;
    std::atomic_bool _stop{false};
    std::vector<loop_t> _loops;
    std::unique_ptr<
      client_server::client_server_location_runtime_t>
      _client_server;
    std::unique_ptr<fanout::fanout_location_runtime_t>
      _fanout;
};

} // namespace zlink::framework::runtime
