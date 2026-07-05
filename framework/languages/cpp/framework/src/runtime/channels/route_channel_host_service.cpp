/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/route_channel_host_service.hpp"

#include "runtime/backend/native_route_backend.hpp"
#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/channels/route_internal_packet_dispatcher.hpp"
#include "runtime/channels/route_packet_dispatcher.hpp"

#include <zlink.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

namespace zlink::framework::runtime
{

class route_channel_host_service_t::route_loop_t
{
  public:
    route_loop_t (message_bus_t bus,
                  std::string route_channel_id,
                  service_provider_t &services,
                  serializer_registry_t &serializers,
                  std::vector<route_channel_host_service_t::spot_node_runtime_t> spot_nodes,
                  std::shared_ptr<detail::route_internal_packet_dispatcher_t> internal_packets,
                  std::atomic_bool &stop) :
        _manager (detail::channel_runtime_manager_t::from (bus)),
        _runtime (&_manager.get_route_channel (route_channel_id)),
        _route_channel_id (std::move (route_channel_id)),
        _internal_packets (std::move (internal_packets)),
        _dispatcher (_route_channel_id,
                     services,
                     serializers,
                     _manager.get_route_handlers (_runtime->router_channel_id ()),
                     _internal_packets ? *_internal_packets : _no_internal_packets,
                     detail::channel_runtime_t::from (bus).dispatch_options ()),
        _stop (&stop),
        _context (std::make_unique<zlink::context_t> ()),
        _router (std::make_unique<zlink::router_socket_t> (*_context)),
        _backend (std::make_unique<detail::backend::native_route_backend_t> (
          *_router, stop, _runtime->manual_connections ()))
    {
        _router->options ().handover (true);
        _router->options ().mandatory (true);
        _router->options ().heartbeat_interval (std::chrono::milliseconds (250));
        _router->options ().heartbeat_timeout (std::chrono::milliseconds (1000));
        _router->options ().reconnect_interval (std::chrono::milliseconds (50));
        _router->options ().reconnect_interval_max (std::chrono::milliseconds (250));
        if (_runtime->routing_id ()) {
            _router->set_routing_id (*_runtime->routing_id ());
        }
        if (!_runtime->bind_endpoint ().empty ()) {
            try {
                _router->bind (_runtime->bind_endpoint ());
            }
            catch (const std::exception &error) {
                throw framework_exception_t (framework_error_kind_t::request_failed,
                                             "route channel '" + _route_channel_id
                                               + "' bind failed at " + _runtime->bind_endpoint ()
                                               + ": " + error.what ());
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

    ~route_loop_t ()
    {
        stop ();
        term ();
    }

    void run ()
    {
        while (!_stop->load (std::memory_order_acquire)) {
            sync_runtime_connections ();
            flush_replies ();
            zlink::received_t received;
            const int rc = _router->recv (received, zlink::recv_flags_t::dontwait);
            if (rc == static_cast<int> (zlink::recv_result_t::no_data)) {
                const int bridge_drained = _backend->drain_spot_route_bridge ();
                if (bridge_drained <= 0) {
                    std::this_thread::sleep_for (std::chrono::milliseconds (1));
                }
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

            dispatch_async (std::move (received), std::move (copied));
        }
        flush_replies ();
    }

    void stop () noexcept
    {
        if (_backend) {
            _backend->close ();
        }
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
            }
            catch (...) {
            }
        }
        join_workers ();
    }

    void term () noexcept
    {
        clear_replies ();
        if (_backend) {
            _backend->close ();
            _backend.reset ();
        }
        _router.reset ();
        if (_context) {
            try {
                _context->term ();
            }
            catch (...) {
            }
            _context.reset ();
        }
    }

  private:
    struct completed_reply_t
    {
        std::optional<zlink::routing_id_t> target_rid;
        std::uint64_t request_seq = 0;
        zlink::framework::runtime::messaging::message_parts_t parts;
    };

    void sync_runtime_connections ()
    {
        if (!_router || !_runtime) {
            return;
        }
        const auto bind_endpoint = _runtime->bind_endpoint ();
        std::map<std::string, std::optional<zlink::routing_id_t>> desired;
        for (const auto &target : _runtime->list_connection_targets ()) {
            if (!target.endpoint.empty () && target.endpoint != bind_endpoint) {
                desired[target.endpoint] = target.peer_rid;
            }
        }
        for (const auto &[endpoint, peer_rid] : desired) {
            if (_connected_endpoints.find (endpoint) != _connected_endpoints.end ()) {
                continue;
            }
            try {
                if (peer_rid) {
                    _router->options ().connect_routing_id (*peer_rid);
                    _router->options ().probe (true);
                }
                _router->connect (endpoint);
                _connected_endpoints.insert (endpoint);
            }
            catch (...) {
            }
        }
        for (auto it = _connected_endpoints.begin (); it != _connected_endpoints.end ();) {
            if (desired.find (*it) != desired.end ()) {
                ++it;
                continue;
            }
            try {
                _router->disconnect (*it);
            }
            catch (...) {
            }
            it = _connected_endpoints.erase (it);
        }
    }

    bool accepts_spot_routes_from (
      const std::vector<route_channel_host_service_t::spot_node_runtime_t> &spot_nodes) const
    {
        return std::any_of (spot_nodes.begin (), spot_nodes.end (), [this] (const auto &spot_node) {
            return std::any_of (spot_node.snapshot.accepted_route_channels.begin (),
                                spot_node.snapshot.accepted_route_channels.end (),
                                [this] (const accepted_spot_route_channel_t &accepted) {
                                    return accepted.channel_name == _route_channel_id;
                                });
        });
    }

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
                throw framework_exception_t (framework_error_kind_t::request_failed,
                                             "accepted SPOT route channel '" + _route_channel_id
                                               + "' requires an active native SpotNode");
            }
            auto bridge = std::make_unique<zlink::service::spot_route_bridge_t> (
              native_node->create_route_bridge ());
            try {
                bridge->attach_router_channel (_route_channel_id, *_router);
            }
            catch (const std::exception &error) {
                throw framework_exception_t (framework_error_kind_t::request_failed,
                                             "route channel '" + _route_channel_id
                                               + "' SPOT route bridge attach failed: "
                                               + error.what ());
            }
            _backend->attach_spot_route_bridge (std::move (bridge), _route_channel_id);
            return;
        }
    }

    static zlink::message_t clone (const zlink::message_t &message) { return message; }

    static std::vector<zlink::message_t> clone_messages (const std::vector<zlink::message_t> &parts)
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

    void reply (const completed_reply_t &completed)
    {
        const auto &parts = completed.parts;
        if (parts.size () == 0 || completed.request_seq == 0 || !completed.target_rid) {
            return;
        }
        std::vector<zlink::message_t> copied;
        copied.reserve (parts.size ());
        for (std::size_t index = 0; index < parts.size (); ++index) {
            copied.push_back (clone (parts[index]));
        }
        auto operation =
          _router->reply (*completed.target_rid, completed.request_seq).message (copied[0]);
        for (std::size_t index = 1; index < copied.size (); ++index) {
            operation = std::move (operation).message (copied[index]);
        }
        std::move (operation).submit ();
    }

    void dispatch_async (zlink::received_t received, std::vector<zlink::message_t> copied)
    {
        auto routing_id = *received.routing_id ();
        const auto request_seq = received.request_seq ();
        std::lock_guard<std::mutex> lock (_workers_mutex);
        _workers.emplace_back ([this, routing_id = std::move (routing_id), request_seq,
                                copied = std::move (copied)] () mutable {
            auto dispatched = _dispatcher.dispatch (detail::route_received_packet_t{
              routing_id, request_seq,
              zlink::framework::runtime::messaging::message_parts_t (std::move (copied))});
            if (!dispatched || !dispatched.value ()) {
                return;
            }
            if (!request_seq) {
                return;
            }
            std::lock_guard<std::mutex> reply_lock (_replies_mutex);
            _replies.push_back (completed_reply_t{std::move (routing_id), *request_seq,
                                                  std::move (dispatched.value ()->parts)});
        });
    }

    void flush_replies ()
    {
        for (;;) {
            completed_reply_t completed;
            {
                std::lock_guard<std::mutex> lock (_replies_mutex);
                if (_replies.empty ()) {
                    return;
                }
                completed = std::move (_replies.front ());
                _replies.pop_front ();
            }
            try {
                reply (completed);
            }
            catch (const std::exception &error) {
                std::cerr << "zlink framework route late reply ignored: " << error.what () << '\n';
            }
            catch (...) {
                std::cerr << "zlink framework route late reply ignored\n";
            }
        }
    }

    void join_workers () noexcept
    {
        std::lock_guard<std::mutex> lock (_workers_mutex);
        for (auto &worker : _workers) {
            if (worker.joinable ()) {
                worker.join ();
            }
        }
        _workers.clear ();
    }

    void clear_replies () noexcept
    {
        std::lock_guard<std::mutex> lock (_replies_mutex);
        _replies.clear ();
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
    std::set<std::string> _connected_endpoints;
    std::mutex _workers_mutex;
    std::vector<std::thread> _workers;
    std::mutex _replies_mutex;
    std::deque<completed_reply_t> _replies;
};

route_channel_host_service_t::route_channel_host_service_t (
  message_bus_t bus,
  serializer_registry_t &serializers,
  std::vector<spot_node_runtime_t> spot_nodes,
  std::map<std::string, std::shared_ptr<detail::route_internal_packet_dispatcher_t>>
    internal_dispatchers) :
    _bus (std::move (bus)),
    _serializers (&serializers),
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
        auto loop = std::make_unique<route_loop_t> (_bus, route_channel_id, services, *_serializers,
                                                    _spot_nodes, internal_packets, _stop);
        auto *raw = loop.get ();
        _loops.push_back (std::move (loop));
        _threads.emplace_back ([this, raw] {
            try {
                raw->run ();
            }
            catch (const std::exception &error) {
                std::cerr << "zlink framework route channel stopped: " << error.what () << '\n';
                _stop.store (true, std::memory_order_release);
            }
            catch (...) {
                std::cerr << "zlink framework route channel stopped\n";
                _stop.store (true, std::memory_order_release);
            }
        });
    }
}

void route_channel_host_service_t::stop () noexcept
{
    _stop.store (true, std::memory_order_release);
    for (auto &thread : _threads) {
        if (thread.joinable ()) {
            thread.join ();
        }
    }
    for (auto &loop : _loops) {
        loop->stop ();
        loop->term ();
    }
    _threads.clear ();
    _loops.clear ();
    _services = nullptr;
}

} // namespace zlink::framework::runtime
