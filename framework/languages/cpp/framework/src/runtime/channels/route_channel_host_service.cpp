/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/route_channel_host_service.hpp"

#include "runtime/backend/native_route_backend.hpp"
#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/channels/route_internal_packet_dispatcher.hpp"
#include "runtime/channels/route_packet_dispatcher.hpp"
#include "runtime/registry/registry_runtime.hpp"

#include <zlink.hpp>

#include <algorithm>
#include <chrono>
#include <span>
#include <thread>
#include <utility>

namespace zlink::framework::runtime
{

namespace
{

class native_spot_route_discovery_bridge_t final
  : public detail::spot_route_discovery_bridge_t
{
  public:
    native_spot_route_discovery_bridge_t (zlink::context_t &context,
                                          const std::string &route_channel_id,
                                          const discovery_snapshot_t &snapshot) :
        _discovery (std::make_unique<zlink::service::discovery_t> (
          context, zlink::auto_connect_type::route_mesh, route_channel_id)),
        _registry_endpoints (snapshot.registry_endpoints)
    {
    }

    zlink::service::discovery_t &discovery () noexcept { return *_discovery; }

    result_t<void> bind_spot_route (const spot_route_t &route) override
    {
        try {
            ensure_connected ();
            const auto key = std::string (route.spot_rid.value ());
            const auto value = route.spot_name;
            _discovery->bind_route (
              zlink::route_kind_t::spot_name,
              std::as_bytes (std::span<const char> (key.data (), key.size ())),
              std::as_bytes (std::span<const char> (value.data (), value.size ())));
            return result_t<void>::success ();
        }
        catch (const std::exception &error) {
            return result_t<void>::failure (
              framework_error_kind_t::request_failed,
              std::string ("registry SPOT route bind failed: ") + error.what ());
        }
    }

    result_t<spot_route_t> resolve_spot_route (spot_rid_t spot_rid) override
    {
        try {
            ensure_connected ();
            const auto key = std::string (spot_rid.value ());
            auto route = _discovery->resolve_route (
              zlink::route_kind_t::spot_name,
              std::as_bytes (std::span<const char> (key.data (), key.size ())));
            return result_t<spot_route_t>::success (spot_route_t{
              node_rid_t::from_string (route.owner_routing_id.to_string ()),
              std::move (spot_rid), route.value.to_string ()});
        }
        catch (const std::exception &error) {
            return result_t<spot_route_t>::failure (
              framework_error_kind_t::spot_route_not_found,
              std::string ("registry SPOT route resolve failed: ") + error.what ());
        }
    }

  private:
    void ensure_connected ()
    {
        if (_connected) {
            return;
        }
        for (const auto &endpoint : _registry_endpoints) {
            connect_registry_with_retry (endpoint);
        }
        _connected = true;
    }

    void connect_registry_with_retry (const std::string &endpoint)
    {
        std::exception_ptr last_error;
        for (int attempt = 0; attempt < 100; ++attempt) {
            try {
                _discovery->connect_registry (endpoint);
                return;
            }
            catch (...) {
                last_error = std::current_exception ();
                std::this_thread::sleep_for (std::chrono::milliseconds (10));
            }
        }
        if (last_error) {
            std::rethrow_exception (last_error);
        }
    }

    std::unique_ptr<zlink::service::discovery_t> _discovery;
    std::vector<std::string> _registry_endpoints;
    bool _connected = false;
};

} // namespace

class route_channel_host_service_t::route_loop_t
{
  public:
    route_loop_t (message_bus_t bus,
                  std::string route_channel_id,
                  service_provider_t &services,
                  serializer_registry_t &serializers,
                  detail::registry_runtime_t registry,
                  discovery_snapshot_t discovery,
                  std::vector<route_channel_host_service_t::spot_node_runtime_t> spot_nodes,
                  std::shared_ptr<detail::route_internal_packet_dispatcher_t> internal_packets,
                  std::atomic_bool &stop) :
        _manager (detail::channel_runtime_manager_t::from (bus)),
        _runtime (&_manager.get_route_channel (route_channel_id)),
        _route_channel_id (std::move (route_channel_id)),
        _internal_packets (std::move (internal_packets)),
        _dispatcher (_route_channel_id, services, serializers,
                     _manager.get_route_handlers (_runtime->router_channel_id ()),
                     _internal_packets ? *_internal_packets : _no_internal_packets,
                     detail::channel_runtime_t::from (bus).dispatch_options ()),
        _stop (&stop),
        _context (std::make_unique<zlink::context_t> ()),
        _router (std::make_unique<zlink::router_socket_t> (*_context)),
        _backend (std::make_unique<detail::backend::native_route_backend_t> (*_router))
    {
        if (_runtime->routing_id ()) {
            _router->set_routing_id (*_runtime->routing_id ());
        }
        const bool use_spot_route_discovery =
          !discovery.registry_endpoints.empty () && _runtime->spot_route_egress_target ();
        if (use_spot_route_discovery) {
            try {
                _spot_route_discovery = std::make_shared<native_spot_route_discovery_bridge_t> (
                  *_context, _route_channel_id, discovery);
                _router->attach_discovery (_spot_route_discovery->discovery ());
                registry.attach_spot_route_discovery (_route_channel_id, _spot_route_discovery);
            }
            catch (const std::exception &error) {
                throw framework_exception_t (
                  framework_error_kind_t::request_failed,
                  "route channel '" + _route_channel_id
                    + "' discovery attach failed: " + error.what ());
            }
        }
        if (!_runtime->bind_endpoint ().empty ()) {
            try {
                _router->bind (_runtime->bind_endpoint ());
            }
            catch (const std::exception &error) {
                throw framework_exception_t (
                  framework_error_kind_t::request_failed,
                  "route channel '" + _route_channel_id + "' bind failed at "
                    + _runtime->bind_endpoint () + ": " + error.what ());
            }
        }
        for (const auto &endpoint : _runtime->manual_connections ()) {
            try {
                _router->connect (endpoint);
            }
            catch (...) {
            }
        }
        attach_spot_route_bridge (spot_nodes);
        _runtime->attach_native_backend (*_backend);
    }

    ~route_loop_t () { stop (); }

    void run ()
    {
        while (!_stop->load (std::memory_order_acquire)) {
            zlink::received_t received;
            const int rc = _router->recv (received, zlink::recv_flags_t::dontwait);
            if (rc == static_cast<int> (zlink::recv_result_t::no_data)) {
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue;
            }
            if (rc != static_cast<int> (zlink::recv_result_t::ok) || !received.routing_id ()) {
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue;
            }

            auto copied = clone_messages (received.parts ());
            if (_backend->handle_router_received (*received.routing_id (), copied,
                                                  received.request_seq ())) {
                continue;
            }

            auto dispatched = _dispatcher.dispatch (detail::route_received_packet_t{
              *received.routing_id (), received.request_seq (),
              zlink::framework::runtime::messaging::message_parts_t (std::move (copied))});
            if (!dispatched || !dispatched.value ()) {
                continue;
            }
            reply (received, dispatched.value ()->parts);
        }
    }

    void stop () noexcept
    {
        if (_router) {
            try {
                _router->close ();
            }
            catch (...) {
            }
        }
        if (_context) {
            try {
                _context->shutdown ();
                _context->term ();
            }
            catch (...) {
            }
        }
    }

  private:
    void attach_spot_route_bridge (
      const std::vector<route_channel_host_service_t::spot_node_runtime_t> &spot_nodes)
    {
        for (const auto &spot_node : spot_nodes) {
            const bool accepts_channel =
              std::any_of (spot_node.snapshot.accepted_route_channels.begin (),
                           spot_node.snapshot.accepted_route_channels.end (),
                           [this] (const accepted_spot_route_channel_t &accepted) {
                               return accepted.channel_name == _route_channel_id;
                           });
            if (!accepts_channel) {
                continue;
            }
            auto native_node = spot_node.runtime.native_node ();
            if (!native_node) {
                throw framework_exception_t (
                  framework_error_kind_t::request_failed,
                  "accepted SPOT route channel '" + _route_channel_id
                    + "' requires an active native SpotNode");
            }
            auto bridge =
              std::make_unique<zlink::service::spot_route_bridge_t> (
                native_node->create_route_bridge ());
            bridge->attach_router_channel (
              _route_channel_id, *_router,
              zlink::service::spot_route_bridge_t::endpoint_capabilities_t::
                route_with_channel_inbound);
            _backend->attach_spot_route_bridge (std::move (bridge), _route_channel_id);
            return;
        }
    }

    static zlink::message_t clone (const zlink::message_t &message)
    {
        return zlink::message_t::from (message.to_string ());
    }

    static std::vector<zlink::message_t> clone_messages (
      const std::vector<zlink::message_t> &parts)
    {
        std::vector<zlink::message_t> copied;
        copied.reserve (parts.size ());
        for (const auto &part : parts) {
            copied.push_back (clone (part));
        }
        return copied;
    }

    static zlink::framework::runtime::messaging::message_parts_t
    copy_parts (const std::vector<zlink::message_t> &parts)
    {
        std::vector<zlink::message_t> copied;
        copied.reserve (parts.size ());
        for (const auto &part : parts) {
            copied.push_back (clone (part));
        }
        return zlink::framework::runtime::messaging::message_parts_t (std::move (copied));
    }

    static void reply (zlink::received_t &received,
                       const zlink::framework::runtime::messaging::message_parts_t &parts)
    {
        if (parts.size () == 0 || !received.request_seq ()) {
            return;
        }
        std::vector<zlink::message_t> copied;
        copied.reserve (parts.size ());
        for (std::size_t index = 0; index < parts.size (); ++index) {
            copied.push_back (clone (parts[index]));
        }
        auto operation = received.reply ().message (copied[0]);
        for (std::size_t index = 1; index < copied.size (); ++index) {
            operation = std::move (operation).message (copied[index]);
        }
        std::move (operation).submit ();
    }

    detail::channel_runtime_manager_t _manager;
    detail::route_channel_runtime_t *_runtime;
    std::string _route_channel_id;
    detail::no_route_internal_packet_dispatcher_t _no_internal_packets;
    std::shared_ptr<detail::route_internal_packet_dispatcher_t> _internal_packets;
    detail::route_packet_dispatcher_t _dispatcher;
    std::atomic_bool *_stop;
    std::unique_ptr<zlink::context_t> _context;
    std::unique_ptr<zlink::router_socket_t> _router;
    std::unique_ptr<detail::backend::native_route_backend_t> _backend;
    std::shared_ptr<native_spot_route_discovery_bridge_t> _spot_route_discovery;
};

route_channel_host_service_t::route_channel_host_service_t (
  message_bus_t bus,
  serializer_registry_t &serializers,
  registry_query_t registry,
  discovery_snapshot_t discovery,
  std::vector<spot_node_runtime_t> spot_nodes,
  std::map<std::string, std::shared_ptr<detail::route_internal_packet_dispatcher_t>>
    internal_dispatchers) :
    _bus (std::move (bus)),
    _serializers (&serializers),
    _registry (std::move (registry)),
    _discovery (std::move (discovery)),
    _spot_nodes (std::move (spot_nodes)),
    _internal_dispatchers (std::move (internal_dispatchers))
{
}

route_channel_host_service_t::~route_channel_host_service_t () = default;

void route_channel_host_service_t::start (service_provider_t &services)
{
    _services = &services;
    _stop.store (false, std::memory_order_release);
    auto manager = detail::channel_runtime_manager_t::from (_bus);
    for (const auto &route_channel_id : manager.route_channel_ids ()) {
        std::shared_ptr<detail::route_internal_packet_dispatcher_t> internal_packets;
        if (const auto found = _internal_dispatchers.find (route_channel_id);
            found != _internal_dispatchers.end ()) {
            internal_packets = found->second;
        }
        auto loop = std::make_unique<route_loop_t> (_bus, route_channel_id, services,
                                                    *_serializers,
                                                    detail::registry_runtime_t::from (_registry),
                                                    _discovery, _spot_nodes,
                                                    std::move (internal_packets), _stop);
        auto *raw = loop.get ();
        _loops.push_back (std::move (loop));
        _threads.emplace_back ([raw] { raw->run (); });
    }
}

void route_channel_host_service_t::stop () noexcept
{
    _stop.store (true, std::memory_order_release);
    for (auto &loop : _loops) {
        loop->stop ();
    }
    for (auto &thread : _threads) {
        if (thread.joinable ()) {
            thread.join ();
        }
    }
    _threads.clear ();
    _loops.clear ();
    _services = nullptr;
}

} // namespace zlink::framework::runtime
