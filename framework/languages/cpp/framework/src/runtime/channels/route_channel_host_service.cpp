/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/channels/route_channel_host_service.hpp"

#include "runtime/backend/native_route_backend.hpp"
#include "runtime/channels/channel_runtime.hpp"
#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/channels/route_internal_packet_dispatcher.hpp"
#include "runtime/channels/route_packet_dispatcher.hpp"
#include "runtime/channels/socket_monitor_event.hpp"

#include <zlink.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string_view>
#include <thread>
#include <utility>

namespace zlink::framework::runtime
{

namespace
{

bool route_channel_trace_enabled ()
{
    const char *value = std::getenv ("ZLINK_CPP_CHANNEL_TRACE");
    if (value != nullptr && *value != '\0') {
        return true;
    }
    value = std::getenv ("ZLINK_CPP_HOST_STOP_TRACE");
    return value != nullptr && *value != '\0' && std::string_view (value) != "0";
}

void trace_route_channel (const std::string &message)
{
    if (route_channel_trace_enabled ()) {
        std::cerr << "zlink route-channel " << message << '\n';
    }
}

std::string route_rid_text (const std::optional<zlink::routing_id_t> &rid)
{
    return rid ? rid->to_string () : std::string ();
}

} // namespace

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
        _channel_runtime (detail::channel_runtime_t::from (bus)),
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
          *_router, stop, _channel_runtime.dispatch_options ()))
    {
        _router->options ().handover (true);
        _router->options ().mandatory (true);
        _router->options ().linger (std::chrono::milliseconds (0));
        _router->options ().heartbeat_interval (std::chrono::milliseconds (250));
        _router->options ().heartbeat_timeout (std::chrono::milliseconds (1000));
        _router->options ().reconnect_interval (std::chrono::milliseconds (50));
        _router->options ().reconnect_interval_max (std::chrono::milliseconds (250));
        if (_runtime->routing_id ()) {
            _router->set_routing_id (*_runtime->routing_id ());
        }
        _monitor = _router->monitor_open (zlink::monitor_event::connected
                                          | zlink::monitor_event::connection_ready
                                          | zlink::monitor_event::disconnected);
        if (!_runtime->bind_endpoint ().empty ()) {
            try {
                std::lock_guard route_lock (_backend->router_mutex ());
                _router->bind (_runtime->bind_endpoint ());
                trace_route_channel ("bind channel=" + _route_channel_id
                                     + " endpoint=" + _runtime->bind_endpoint ()
                                     + " rid=" + route_rid_text (_runtime->routing_id ()));
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
                std::lock_guard route_lock (_backend->router_mutex ());
                _router->connect (endpoint);
                _connected_endpoints.insert (endpoint);
                trace_route_channel ("manual-connect channel=" + _route_channel_id
                                     + " endpoint=" + endpoint);
            }
            catch (...) {
            }
        }
        static_cast<void> (spot_nodes);
        _runtime->attach_native_backend (*_backend);
    }

    ~route_loop_t ()
    {
        stop ();
        term ();
    }

    void run ()
    {
        trace_route_channel ("loop-run-begin channel=" + _route_channel_id);
        while (!_stop->load (std::memory_order_acquire)) {
            drain_monitor_events ();
            sync_runtime_connections ();
            flush_replies ();
            zlink::received_t received;
            int rc = static_cast<int> (zlink::recv_result_t::no_data);
            {
                std::lock_guard route_lock (_backend->router_mutex ());
                rc = _router->recv (received, zlink::recv_flags_t::dontwait);
            }
            if (rc == static_cast<int> (zlink::recv_result_t::no_data)) {
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue;
            }
            if (rc != static_cast<int> (zlink::recv_result_t::ok) || !received.routing_id ()) {
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue;
            }

            auto copied = clone_messages (received.parts ());
            _runtime->mark_peer_ready (*received.routing_id ());
            trace_route_channel ("recv channel=" + _route_channel_id
                                 + " source=" + received.routing_id ()->to_string ()
                                 + " requestSeq="
                                 + (received.request_seq ()
                                      ? std::to_string (*received.request_seq ())
                                      : std::string (""))
                                 + " parts=" + std::to_string (copied.size ()));
            if (_backend->handle_router_received (*received.routing_id (), copied,
                                                  received.request_seq ())) {
                trace_route_channel ("bridge-handled channel=" + _route_channel_id
                                     + " source=" + received.routing_id ()->to_string ());
                continue;
            }

            dispatch_async (std::move (received), std::move (copied));
        }
        if (_stop->load (std::memory_order_acquire)) {
            clear_replies ();
        } else {
            flush_replies ();
        }
        drain_monitor_events ();
        trace_route_channel ("loop-run-end channel=" + _route_channel_id);
    }

    void stop () noexcept
    {
        trace_route_channel ("loop-stop-begin channel=" + _route_channel_id);
        request_stop ();
        trace_route_channel ("loop-stop-router-linger-begin channel=" + _route_channel_id);
        if (_router) {
            try {
                _router->options ().linger (std::chrono::milliseconds (0));
            }
            catch (...) {
            }
        }
        if (_backend) {
            trace_route_channel ("loop-stop-backend-close-begin channel=" + _route_channel_id);
            _backend->close ();
            trace_route_channel ("loop-stop-backend-close-end channel=" + _route_channel_id);
        }
        trace_route_channel ("loop-stop-join-workers-begin channel=" + _route_channel_id);
        join_workers ();
        trace_route_channel ("loop-stop-end channel=" + _route_channel_id);
    }

    void request_stop () noexcept
    {
        trace_route_channel ("loop-request-stop channel=" + _route_channel_id);
        if (_runtime)
            _runtime->stop ();
    }

    void term () noexcept
    {
        trace_route_channel ("loop-term-begin channel=" + _route_channel_id);
        clear_replies ();
        if (_router) {
            try {
                _router->options ().linger (std::chrono::milliseconds (0));
            }
            catch (...) {
            }
        }
        if (_backend) {
            trace_route_channel ("loop-term-backend-close-begin channel=" + _route_channel_id);
            _backend->close ();
            trace_route_channel ("loop-term-backend-close-end channel=" + _route_channel_id);
        }
        disconnect_router_endpoints ();
        if (_monitor.valid ()) {
            try {
                trace_route_channel ("loop-term-monitor-close-begin channel=" + _route_channel_id);
                _monitor.close ();
                trace_route_channel ("loop-term-monitor-close-end channel=" + _route_channel_id);
            }
            catch (...) {
            }
        }
        if (_router) {
            try {
                if (_backend) {
                    std::lock_guard route_lock (_backend->router_mutex ());
                    trace_route_channel ("loop-term-router-close-begin channel=" + _route_channel_id);
                    _router->close ();
                    trace_route_channel ("loop-term-router-close-end channel=" + _route_channel_id);
                } else {
                    trace_route_channel ("loop-term-router-close-begin channel=" + _route_channel_id);
                    _router->close ();
                    trace_route_channel ("loop-term-router-close-end channel=" + _route_channel_id);
                }
            }
            catch (const std::exception &error) {
                trace_route_channel ("loop-term-router-close-failed channel=" + _route_channel_id
                                     + " error=" + error.what ());
            }
            catch (...) {
                trace_route_channel ("loop-term-router-close-failed channel=" + _route_channel_id
                                     + " error=unknown");
            }
        }
        if (_backend) {
            _backend.reset ();
        }
        _router.reset ();
        if (_context) {
            try {
                _context->options ().blocky (false);
            }
            catch (...) {
            }
            try {
                trace_route_channel ("loop-term-context-term-begin channel=" + _route_channel_id);
                _context->term ();
                trace_route_channel ("loop-term-context-term-end channel=" + _route_channel_id);
            }
            catch (...) {
            }
            _context.reset ();
        }
        trace_route_channel ("loop-term-end channel=" + _route_channel_id);
    }

  private:
    struct completed_reply_t
    {
        std::optional<zlink::routing_id_t> target_rid;
        std::optional<zlink::routing_id_t> target_spot_id;
        std::uint64_t request_seq = 0;
        zlink::framework::runtime::messaging::message_parts_t parts;
    };

    void disconnect_router_endpoints () noexcept
    {
        if (!_router) {
            return;
        }

        std::set<std::string> endpoints = _connected_endpoints;
        for (const auto &endpoint : _runtime->manual_connections ()) {
            if (!endpoint.empty ()) {
                endpoints.insert (endpoint);
            }
        }
        for (const auto &target : _runtime->list_connection_targets ()) {
            if (!target.endpoint.empty () && target.endpoint != _runtime->bind_endpoint ()) {
                endpoints.insert (target.endpoint);
            }
        }

        auto disconnect_all = [this, &endpoints] {
            for (const auto &endpoint : endpoints) {
                try {
                    trace_route_channel ("loop-term-disconnect channel=" + _route_channel_id
                                         + " endpoint=" + endpoint);
                    _router->disconnect (endpoint);
                }
                catch (const std::exception &error) {
                    trace_route_channel ("loop-term-disconnect-failed channel=" + _route_channel_id
                                         + " endpoint=" + endpoint + " error=" + error.what ());
                }
                catch (...) {
                    trace_route_channel ("loop-term-disconnect-failed channel=" + _route_channel_id
                                         + " endpoint=" + endpoint + " error=unknown");
                }
            }

            const auto bind_endpoint = _runtime->bind_endpoint ();
            if (!bind_endpoint.empty ()) {
                try {
                    trace_route_channel ("loop-term-unbind channel=" + _route_channel_id
                                         + " endpoint=" + bind_endpoint);
                    _router->unbind (bind_endpoint);
                }
                catch (const std::exception &error) {
                    trace_route_channel ("loop-term-unbind-failed channel=" + _route_channel_id
                                         + " endpoint=" + bind_endpoint + " error=" + error.what ());
                }
                catch (...) {
                    trace_route_channel ("loop-term-unbind-failed channel=" + _route_channel_id
                                         + " endpoint=" + bind_endpoint + " error=unknown");
                }
            }
        };

        if (_backend) {
            std::lock_guard route_lock (_backend->router_mutex ());
            disconnect_all ();
        } else {
            disconnect_all ();
        }
        _connected_endpoints.clear ();
    }

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
                std::lock_guard route_lock (_backend->router_mutex ());
                if (peer_rid) {
                    _router->options ().connect_routing_id (*peer_rid);
                    _router->options ().probe (true);
                }
                _router->connect (endpoint);
                trace_route_channel ("connect channel=" + _route_channel_id
                                     + " endpoint=" + endpoint
                                     + " peerRid="
                                     + (peer_rid ? peer_rid->to_string () : std::string ()));
                _connected_endpoints.insert (endpoint);
            }
            catch (const std::exception &error) {
                trace_route_channel ("connect-failed channel=" + _route_channel_id
                                     + " endpoint=" + endpoint
                                     + " peerRid="
                                     + (peer_rid ? peer_rid->to_string () : std::string ())
                                     + " error=" + error.what ());
            }
            catch (...) {
                trace_route_channel ("connect-failed channel=" + _route_channel_id
                                     + " endpoint=" + endpoint
                                     + " peerRid="
                                     + (peer_rid ? peer_rid->to_string () : std::string ())
                                     + " error=unknown");
            }
        }
        for (auto it = _connected_endpoints.begin (); it != _connected_endpoints.end ();) {
            if (desired.find (*it) != desired.end ()) {
                ++it;
                continue;
            }
            try {
                std::lock_guard route_lock (_backend->router_mutex ());
                _router->disconnect (*it);
                trace_route_channel ("disconnect channel=" + _route_channel_id
                                     + " endpoint=" + *it);
            }
            catch (const std::exception &error) {
                trace_route_channel ("disconnect-failed channel=" + _route_channel_id
                                     + " endpoint=" + *it + " error=" + error.what ());
            }
            catch (...) {
                trace_route_channel ("disconnect-failed channel=" + _route_channel_id
                                     + " endpoint=" + *it + " error=unknown");
            }
            it = _connected_endpoints.erase (it);
        }
    }

    void drain_monitor_events ()
    {
        if (!_monitor.valid () || !_runtime) {
            return;
        }
        for (;;) {
            std::optional<zlink::monitor_event_t> event;
            try {
                event = _monitor.recv (zlink::recv_flags_t::dontwait);
            }
            catch (...) {
                return;
            }
            if (!event) {
                return;
            }
            const auto kind = detail::map_socket_monitor_event (event->event);
            if (kind) {
                _channel_runtime.publish_socket_event (
                  _route_channel_id, *kind, event->local_addr, event->remote_addr,
                  static_cast<std::uint32_t> (event->event), event->value);
            }
            if (!event->routing_id) {
                continue;
            }
            if (event->event == zlink::monitor_event::connection_ready) {
                _runtime->mark_peer_ready (*event->routing_id);
                trace_route_channel ("peer-ready channel=" + _route_channel_id
                                     + " peerRid=" + event->routing_id->to_string ());
            } else if (event->event == zlink::monitor_event::disconnected) {
                _runtime->mark_peer_disconnected (*event->routing_id);
                trace_route_channel ("peer-disconnected channel=" + _route_channel_id
                                     + " peerRid=" + event->routing_id->to_string ());
            }
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
        if (_stop->load (std::memory_order_acquire)) {
            return;
        }
        const auto &parts = completed.parts;
        if (parts.size () == 0 || completed.request_seq == 0 || !completed.target_rid) {
            return;
        }
        if (_runtime) {
            if (auto connected = _runtime->wait_until_connected (_runtime->default_request_timeout ());
                !connected) {
                trace_route_channel ("reply-skipped channel=" + _route_channel_id
                                     + " target=" + completed.target_rid->to_string ()
                                     + " requestSeq=" + std::to_string (completed.request_seq)
                                     + " error="
                                     + (connected.error () ? connected.error ()->what ()
                                                          : "route channel is not connected"));
                return;
            }
            if (auto ready = _runtime->wait_until_peer_ready (
                  *completed.target_rid, _runtime->default_request_timeout ());
                !ready) {
                trace_route_channel ("reply-skipped channel=" + _route_channel_id
                                     + " target=" + completed.target_rid->to_string ()
                                     + " requestSeq=" + std::to_string (completed.request_seq)
                                     + " error="
                                     + (ready.error () ? ready.error ()->what ()
                                                      : "route peer is not ready"));
                return;
            }
        }
        std::vector<zlink::message_t> copied;
        copied.reserve (parts.size ());
        for (std::size_t index = 0; index < parts.size (); ++index) {
            copied.push_back (clone (parts[index]));
        }
        std::lock_guard route_lock (_backend->router_mutex ());
        auto operation =
          completed.target_spot_id
            ? _router
                ->reply_to_spot (*completed.target_rid, *completed.target_spot_id,
                                 completed.request_seq)
                .message (copied[0])
            : _router->reply (*completed.target_rid, completed.request_seq).message (copied[0]);
        for (std::size_t index = 1; index < copied.size (); ++index) {
            operation = std::move (operation).message (copied[index]);
        }
        std::move (operation).submit ();
    }

    void dispatch_async (zlink::received_t received, std::vector<zlink::message_t> copied)
    {
        auto routing_id = *received.routing_id ();
        auto spot_id = received.spot_id ();
        const auto request_seq = received.request_seq ();
        trace_route_channel ("dispatch-submit channel=" + _route_channel_id
                             + " source=" + routing_id.to_string ()
                             + " requestSeq="
                             + (request_seq ? std::to_string (*request_seq) : std::string (""))
                             + " parts=" + std::to_string (copied.size ()));
        std::lock_guard<std::mutex> lock (_workers_mutex);
        _workers.emplace_back ([this, routing_id = std::move (routing_id),
                                spot_id = std::move (spot_id), request_seq,
                                copied = std::move (copied)] () mutable {
            auto dispatched = _dispatcher.dispatch (detail::route_received_packet_t{
              routing_id, request_seq,
              zlink::framework::runtime::messaging::message_parts_t (std::move (copied))});
            if (!dispatched || !dispatched.value ()) {
                trace_route_channel ("dispatch-complete channel=" + _route_channel_id
                                     + " source=" + routing_id.to_string ()
                                     + " reply=none");
                return;
            }
            if (!request_seq) {
                trace_route_channel ("dispatch-complete channel=" + _route_channel_id
                                     + " source=" + routing_id.to_string ()
                                     + " reply=no-request-seq");
                return;
            }
            const auto reply_source = routing_id.to_string ();
            std::lock_guard<std::mutex> reply_lock (_replies_mutex);
            _replies.push_back (completed_reply_t{std::move (routing_id), std::move (spot_id),
                                                  *request_seq,
                                                  std::move (dispatched.value ()->parts)});
            trace_route_channel ("dispatch-complete channel=" + _route_channel_id
                                 + " source=" + reply_source
                                 + " reply=queued requestSeq=" + std::to_string (*request_seq));
        });
    }

    void flush_replies ()
    {
        if (_stop->load (std::memory_order_acquire)) {
            clear_replies ();
            return;
        }
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
                trace_route_channel ("reply channel=" + _route_channel_id
                                     + " target="
                                     + (completed.target_rid ? completed.target_rid->to_string ()
                                                             : std::string ())
                                     + " requestSeq="
                                     + std::to_string (completed.request_seq)
                                     + " parts=" + std::to_string (completed.parts.size ()));
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
        trace_route_channel ("loop-join-workers-begin channel=" + _route_channel_id);
        std::lock_guard<std::mutex> lock (_workers_mutex);
        for (auto &worker : _workers) {
            if (worker.joinable ()) {
                worker.join ();
            }
        }
        _workers.clear ();
        trace_route_channel ("loop-join-workers-end channel=" + _route_channel_id);
    }

    void clear_replies () noexcept
    {
        std::lock_guard<std::mutex> lock (_replies_mutex);
        _replies.clear ();
    }

    detail::channel_runtime_manager_t _manager;
    detail::channel_runtime_t _channel_runtime;
    detail::route_channel_runtime_t *_runtime;
    std::string _route_channel_id;
    detail::no_route_internal_packet_dispatcher_t _no_internal_packets;
    std::shared_ptr<detail::route_internal_packet_dispatcher_t> _internal_packets;
    detail::route_packet_dispatcher_t _dispatcher;
    std::atomic_bool *_stop;
    std::unique_ptr<zlink::context_t> _context;
    std::unique_ptr<zlink::router_socket_t> _router;
    zlink::socket_monitor_t _monitor;
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

void route_channel_host_service_t::request_stop () noexcept
{
    trace_route_channel ("host-request-stop-begin");
    _stop.store (true, std::memory_order_release);
    for (auto &loop : _loops) {
        loop->request_stop ();
    }
    trace_route_channel ("host-request-stop-end");
}

void route_channel_host_service_t::stop () noexcept
{
    trace_route_channel ("host-stop-begin");
    request_stop ();
    trace_route_channel ("host-stop-join-route-threads-begin");
    for (auto &thread : _threads) {
        if (thread.joinable ()) {
            thread.join ();
        }
    }
    trace_route_channel ("host-stop-join-route-threads-end");
    trace_route_channel ("host-stop-loop-stop-begin");
    for (auto &loop : _loops) {
        loop->stop ();
    }
    trace_route_channel ("host-stop-loop-stop-end");
    trace_route_channel ("host-stop-loop-term-begin");
    for (auto &loop : _loops) {
        loop->term ();
    }
    trace_route_channel ("host-stop-loop-term-end");
    _threads.clear ();
    _loops.clear ();
    _services = nullptr;
    trace_route_channel ("host-stop-end");
}

} // namespace zlink::framework::runtime
