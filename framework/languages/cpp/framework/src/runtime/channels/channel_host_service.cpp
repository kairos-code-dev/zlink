/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/channels/channel_host_service.hpp"

#include "runtime/channels/channel_packet_dispatcher.hpp"
#include "runtime/channels/channel_runtime.hpp"
#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/channels/channel_socket_options.hpp"
#include "runtime/channels/socket_monitor_event.hpp"

#include <zlink.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <optional>
#include <set>
#include <utility>

namespace zlink::framework::runtime
{

namespace
{

bool channel_trace_enabled ()
{
    const char *value = std::getenv ("ZLINK_CPP_CHANNEL_TRACE");
    return value != nullptr && *value != '\0';
}

void trace_channel (const std::string &message)
{
    if (channel_trace_enabled ()) {
        std::cerr << "zlink channel " << message << '\n';
    }
}

} // namespace

class channel_host_service_t::server_loop_t
{
  public:
    server_loop_t (message_bus_t bus,
                   std::string channel_name,
                   std::vector<std::string> endpoints,
                   std::optional<zlink::routing_id_t> routing_id,
                   channel_capability_snapshot_t capability,
                   service_provider_t &services,
                   serializer_registry_t &serializers,
                   const handler_registry_t &handlers,
                   std::atomic_bool &stop) :
        _runtime (detail::channel_runtime_t::from (bus)),
        _channel_name (std::move (channel_name)),
        _endpoints (std::move (endpoints)),
        _capability (std::move (capability)),
        _services (&services),
        _serializers (&serializers),
        _handlers (&handlers),
        _stop (&stop),
        _context (std::make_unique<zlink::context_t> ()),
        _router (std::make_unique<zlink::router_socket_t> (*_context))
    {
        detail::apply_weighted_channel_socket_options (*_router, _capability);
        if (routing_id) {
            _router->set_routing_id (*routing_id);
        }
        _monitor = _router->monitor_open (
          zlink::monitor_event::connected | zlink::monitor_event::accepted
          | zlink::monitor_event::connection_ready | zlink::monitor_event::disconnected
          | zlink::monitor_event::closed | zlink::monitor_event::handshake_failed_no_detail
          | zlink::monitor_event::handshake_failed_protocol
          | zlink::monitor_event::handshake_failed_auth);
        for (const auto &endpoint : _endpoints) {
            _router->bind (endpoint);
        }
    }

    ~server_loop_t () { stop (); }

    void run ()
    {
        while (!_stop->load (std::memory_order_acquire)) {
            drain_monitor_events ();
            apply_runtime_options ();
            flush_replies ();
            zlink::received_t received;
            const int rc = _router->recv (received, zlink::recv_flags_t::dontwait);
            if (rc == static_cast<int> (zlink::recv_result_t::no_data)) {
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue;
            }
            if (rc != static_cast<int> (zlink::recv_result_t::ok)) {
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue;
            }
            if (is_drained ()) {
                continue;
            }
            trace_channel ("server recv channel=" + _channel_name
                           + " parts=" + std::to_string (received.parts ().size ()));
            dispatch_async (std::move (received));
        }
        join_workers ();
        flush_replies ();
        drain_monitor_events ();
    }

    void stop () noexcept
    {
        join_workers ();
        flush_replies ();
        if (_monitor.valid ()) {
            try {
                _monitor.close ();
            }
            catch (...) {
            }
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
        clear_replies ();
        if (_router) {
            _router.reset ();
        }
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
        std::optional<zlink::routing_id_t> routing_id;
        std::uint64_t request_seq = 0;
        zlink::framework::runtime::messaging::message_parts_t parts;
    };

    static zlink::message_t clone (const zlink::message_t &message) { return message; }

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

    void dispatch_async (zlink::received_t received)
    {
        auto routing_id = received.routing_id ();
        const auto request_seq = received.request_seq ();
        auto request_parts = copy_parts (received.parts ());
        std::lock_guard<std::mutex> lock (_workers_mutex);
        _workers.emplace_back ([this, routing_id = std::move (routing_id), request_seq,
                                request_parts = std::move (request_parts)] () mutable {
            trace_channel ("server dispatch channel=" + _channel_name
                           + " parts=" + std::to_string (request_parts.size ()));
            detail::channel_packet_dispatcher_t dispatcher (_runtime);
            auto scope = _services->create_scope (service_scope_kind_t::handler_invocation);
            auto reply = dispatcher.dispatch_server_message (
              _channel_name, request_parts, scope.provider (), *_serializers, *_handlers);
            if (!reply || reply.value ().size () == 0 || !routing_id || !request_seq) {
                trace_channel ("server dispatch no-reply channel=" + _channel_name);
                return;
            }
            std::lock_guard<std::mutex> reply_lock (_replies_mutex);
            _replies.push_back (completed_reply_t{std::move (routing_id), *request_seq,
                                                  std::move (reply.value ())});
        });
    }

    void reply (const completed_reply_t &completed)
    {
        if (!completed.routing_id || completed.request_seq == 0 || completed.parts.size () == 0) {
            return;
        }
        std::vector<zlink::message_t> copied;
        copied.reserve (completed.parts.size ());
        for (std::size_t index = 0; index < completed.parts.size (); ++index) {
            copied.push_back (clone (completed.parts[index]));
        }
        auto operation =
          _router->reply (*completed.routing_id, completed.request_seq).message (copied[0]);
        for (std::size_t index = 1; index < copied.size (); ++index) {
            operation = std::move (operation).message (copied[index]);
        }
        std::move (operation).submit ();
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
                std::cerr << "zlink framework channel late reply ignored: " << error.what ()
                          << '\n';
            }
            catch (...) {
                std::cerr << "zlink framework channel late reply ignored\n";
            }
            trace_channel ("server replied channel=" + _channel_name);
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

    void apply_runtime_options ()
    {
        const auto peer_weight = _runtime.server_peer_weight_override (_channel_name);
        if (!peer_weight
            || (_applied_peer_weight && *_applied_peer_weight == *peer_weight)) {
            return;
        }
        _router->options ().peer_weight (
          zlink::peer_weight_t::value (
            static_cast<std::uint32_t> (
              std::min (*peer_weight, 100))));
        _applied_peer_weight = *peer_weight;
    }

    bool is_drained () const noexcept
    {
        return _applied_peer_weight && *_applied_peer_weight == 0;
    }

    void drain_monitor_events ()
    {
        if (!_monitor.valid ()) {
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
            if (!kind) {
                continue;
            }
            if (!event->remote_addr.empty ()) {
                if (*kind == socket_event_kind_t::connected) {
                    _pending_handshake_remotes.insert (event->remote_addr);
                } else if (*kind == socket_event_kind_t::connection_ready) {
                    _pending_handshake_remotes.erase (event->remote_addr);
                } else if (*kind == socket_event_kind_t::disconnected
                           && _pending_handshake_remotes.erase (event->remote_addr) != 0) {
                    _runtime.publish_socket_event (
                      _channel_name, socket_event_kind_t::handshake_failed, event->local_addr,
                      event->remote_addr, static_cast<std::uint32_t> (event->event), event->value);
                }
            }
            trace_channel ("server monitor channel=" + _channel_name + " kind="
                           + std::to_string (static_cast<std::uint32_t> (*kind))
                           + " local=" + event->local_addr + " remote=" + event->remote_addr);
            _runtime.publish_socket_event (_channel_name, *kind, event->local_addr,
                                           event->remote_addr,
                                           static_cast<std::uint32_t> (event->event), event->value);
        }
    }

    detail::channel_runtime_t _runtime;
    std::string _channel_name;
    std::vector<std::string> _endpoints;
    channel_capability_snapshot_t _capability;
    service_provider_t *_services;
    serializer_registry_t *_serializers;
    const handler_registry_t *_handlers;
    std::atomic_bool *_stop;
    std::unique_ptr<zlink::context_t> _context;
    std::unique_ptr<zlink::router_socket_t> _router;
    zlink::socket_monitor_t _monitor;
    std::set<std::string> _pending_handshake_remotes;
    std::optional<int> _applied_peer_weight;
    std::mutex _workers_mutex;
    std::vector<std::thread> _workers;
    std::mutex _replies_mutex;
    std::deque<completed_reply_t> _replies;
};

class channel_host_service_t::subscriber_loop_t
{
  public:
    subscriber_loop_t (message_bus_t bus,
                       std::string channel_name,
                       detail::channel_runtime_bundle_t &bundle,
                       channel_capability_snapshot_t capability,
                       service_provider_t &services,
                       serializer_registry_t &serializers,
                       const handler_registry_t &handlers,
                       std::atomic_bool &stop) :
        _runtime (detail::channel_runtime_t::from (bus)),
        _channel_name (std::move (channel_name)),
        _bundle (&bundle),
        _capability (std::move (capability)),
        _services (&services),
        _serializers (&serializers),
        _handlers (&handlers),
        _stop (&stop),
        _context (std::make_unique<zlink::context_t> ()),
        _subscriber (std::make_unique<zlink::sub_socket_t> (*_context))
    {
        detail::apply_common_channel_socket_options (*_subscriber, _capability);
        _subscriber->set_subscription ("");
        apply_runtime_connections ();
    }

    ~subscriber_loop_t () { stop (); }

    void run ()
    {
        while (!_stop->load (std::memory_order_acquire)) {
            apply_runtime_connections ();
            zlink::topic_message_t message;
            const int rc = _subscriber->subscribe (message, zlink::recv_flags_t::dontwait);
            if (rc == static_cast<int> (zlink::recv_result_t::no_data)) {
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue;
            }
            if (rc != static_cast<int> (zlink::recv_result_t::ok)) {
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue;
            }
            dispatch_async (std::move (message));
        }
        join_workers ();
    }

    void stop () noexcept
    {
        if (_subscriber) {
            try {
                _subscriber->close ();
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
        if (_subscriber) {
            _subscriber.reset ();
        }
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
    static zlink::message_t clone (const zlink::message_t &message) { return message; }

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

    void dispatch_async (zlink::topic_message_t message)
    {
        auto parts = copy_parts (message.parts ());
        std::lock_guard<std::mutex> lock (_workers_mutex);
        _workers.emplace_back ([this, parts = std::move (parts)] () mutable {
            detail::channel_packet_dispatcher_t dispatcher (_runtime);
            auto scope = _services->create_scope (service_scope_kind_t::handler_invocation);
            (void) dispatcher.dispatch_server_message (_channel_name, parts, scope.provider (),
                                                       *_serializers, *_handlers);
        });
    }

    void join_workers ()
    {
        std::vector<std::thread> workers;
        {
            std::lock_guard<std::mutex> lock (_workers_mutex);
            workers.swap (_workers);
        }
        for (auto &worker : workers) {
            if (worker.joinable ()) {
                worker.join ();
            }
        }
    }

    void apply_runtime_connections ()
    {
        if (_bundle == nullptr || _subscriber == nullptr) {
            return;
        }
        std::set<std::string> desired;
        for (const auto &endpoint : _bundle->connections_from_next ()) {
            desired.insert (endpoint);
            if (_connected.insert (endpoint).second) {
                _subscriber->connect (endpoint);
            }
        }
        for (auto it = _connected.begin (); it != _connected.end ();) {
            if (desired.find (*it) != desired.end ()) {
                ++it;
                continue;
            }
            try {
                _subscriber->disconnect (*it);
            }
            catch (...) {
            }
            it = _connected.erase (it);
        }
    }

    detail::channel_runtime_t _runtime;
    std::string _channel_name;
    detail::channel_runtime_bundle_t *_bundle;
    channel_capability_snapshot_t _capability;
    service_provider_t *_services;
    serializer_registry_t *_serializers;
    const handler_registry_t *_handlers;
    std::atomic_bool *_stop;
    std::unique_ptr<zlink::context_t> _context;
    std::unique_ptr<zlink::sub_socket_t> _subscriber;
    std::set<std::string> _connected;
    std::mutex _workers_mutex;
    std::vector<std::thread> _workers;
};

channel_host_service_t::channel_host_service_t (message_bus_t bus,
                                                std::vector<channel_snapshot_t> channels,
                                                handler_registry_t &handlers,
                                                serializer_registry_t &serializers) :
    _bus (std::move (bus)),
    _channels (std::move (channels)),
    _handlers (&handlers),
    _serializers (&serializers)
{
}

channel_host_service_t::~channel_host_service_t () = default;

void channel_host_service_t::start (service_provider_t &services)
{
    _services = &services;
    auto manager = detail::channel_runtime_manager_t::from (_bus);
    const bool auto_connect_active =
      detail::channel_runtime_t::from (_bus).auto_connect_active ();
    _stop.store (false, std::memory_order_release);
    for (const auto &channel : _channels) {
        if (!channel.server.enabled
            || (channel.server.discovery && auto_connect_active)
            || channel.server.bind_endpoints.empty ()) {
            continue;
        }
        auto loop = std::make_unique<server_loop_t> (
          _bus, channel.name, channel.server.bind_endpoints, channel.server.routing_id,
          channel.server, services, *_serializers, *_handlers, _stop);
        auto *raw = loop.get ();
        _loops.push_back (std::move (loop));
        _threads.emplace_back ([raw] { raw->run (); });
    }
    for (const auto &channel : _channels) {
        if (!channel.subscriber.enabled
            || channel.subscriber.discovery
            || (!channel.subscriber.discovery && channel.subscriber.connect_endpoints.empty ())) {
            continue;
        }
        auto &bundle = manager.get_or_create_subscriber_bundle (channel.name);
        auto loop = std::make_unique<subscriber_loop_t> (
          _bus, channel.name, bundle, channel.subscriber, services, *_serializers, *_handlers,
          _stop);
        auto *raw = loop.get ();
        _subscriber_loops.push_back (std::move (loop));
        _threads.emplace_back ([raw] { raw->run (); });
    }
}

void channel_host_service_t::request_stop () noexcept
{
    _stop.store (true, std::memory_order_release);
}

void channel_host_service_t::stop () noexcept
{
    request_stop ();
    for (auto &loop : _loops) {
        loop->stop ();
    }
    for (auto &loop : _subscriber_loops) {
        loop->stop ();
    }
    for (auto &thread : _threads) {
        if (thread.joinable ()) {
            thread.join ();
        }
    }
    _threads.clear ();
    _loops.clear ();
    _subscriber_loops.clear ();
    _services = nullptr;
}

} // namespace zlink::framework::runtime
