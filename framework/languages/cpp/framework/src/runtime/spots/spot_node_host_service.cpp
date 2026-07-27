/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/spots/spot_node_host_service.hpp"

#include "runtime/configuration/endpoint_connections.hpp"

#include <zlink.hpp>

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
    bool node_close_failed = false;

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

spot_node_host_service_t::spot_node_host_service_t (std::vector<node_runtime_t> spot_nodes) :
    _spot_nodes (std::move (spot_nodes))
{
}

spot_node_host_service_t::~spot_node_host_service_t () = default;

void spot_node_host_service_t::start (service_provider_t &services)
{
    auto &serializers = services.get_required<serializer_registry_t> ();
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
            std::map<std::string, zlink::routing_id_t> rid_by_endpoint;
            for (const auto &[rid, endpoint] : snapshot.router_manual_rid_connections) {
                rid_by_endpoint.emplace (endpoint, rid);
            }
            detail::endpoint_connections_runtime_t::attach (
              handle,
              [node = native->node, rid_by_endpoint] (const std::string &endpoint) {
                  const auto found = rid_by_endpoint.find (endpoint);
                  if (found == rid_by_endpoint.end ()) {
                      node->connect_peer (endpoint);
                  } else {
                      node->connect_peer_rid (found->second, endpoint);
                  }
              },
              [node = native->node, rid_by_endpoint] (const std::string &endpoint) {
                  const auto found = rid_by_endpoint.find (endpoint);
                  if (found == rid_by_endpoint.end ()) {
                      node->disconnect_peer (endpoint);
                  } else {
                      node->disconnect_peer_rid (found->second);
                  }
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
                    //  Draining also reaches native configuration calls, which throw. Keeping it
                    //  inside the guard is what stops one of those from unwinding out of this
                    //  thread and terminating the process.
                    dispatched += native->runtime.cleanup_expired_actor_admissions ();
                    dispatched += native->runtime.drain_actor_packets (services, serializers);
                    dispatched += native->runtime.drain_routed_packets (services, serializers);
                    dispatched += native->runtime.drain_subscriptions (services, serializers);
                }
                catch (const std::exception &) {
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
    trace_spot_node_stop ("stop-end");
}

} // namespace zlink::framework::runtime
