/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/spots/spot_node_host_service.hpp"

#include <zlink.hpp>

#include "runtime/registry/discovery_registry_connection.hpp"
#include "runtime/spots/spot_runtime.hpp"

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
    std::unique_ptr<zlink::service::discovery_t> discovery;
    std::shared_ptr<zlink::service::spot_node_t> node;
    detail::spot_node_runtime_t runtime;

    explicit native_node_t (detail::spot_node_runtime_t runtime_) :
        context (),
        node (std::make_shared<zlink::service::spot_node_t> (context)),
        runtime (std::move (runtime_))
    {
    }
};

spot_node_host_service_t::spot_node_host_service_t (
  std::vector<node_runtime_t> spot_nodes,
  discovery_snapshot_t discovery) :
    _spot_nodes (std::move (spot_nodes)), _discovery (std::move (discovery))
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
        if (snapshot.discovery_channel_name && snapshot.pub_sub_manual_connections.empty ()) {
            native->discovery = std::make_unique<zlink::service::discovery_t> (
              native->context, zlink::auto_connect_type::spot_mesh,
              *snapshot.discovery_channel_name);
            for (const auto &endpoint : _discovery.registry_endpoints) {
                detail::connect_registry_with_retry (*native->discovery, endpoint);
            }
            native->node->attach_discovery (*native->discovery);
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
            native->runtime.detach_native_node ();
        }
        catch (...) {
        }
        try {
            native->node->close ();
        }
        catch (...) {
        }
        try {
            native->context.shutdown ();
            native->context.term ();
        }
        catch (...) {
        }
    }
    _nodes.clear ();
}

} // namespace zlink::framework::runtime
