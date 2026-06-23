/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/channel_host_service.hpp"

#include "runtime/channels/channel_packet_dispatcher.hpp"
#include "runtime/channels/channel_runtime.hpp"
#include "runtime/channels/discovery_registry_connection.hpp"

#include <zlink.hpp>

#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <utility>

namespace zlink::framework::runtime
{

class channel_host_service_t::server_loop_t
{
  public:
    server_loop_t (message_bus_t bus,
                   std::string channel_name,
                   bool use_discovery,
                   std::vector<std::string> endpoints,
                   std::optional<zlink::routing_id_t> routing_id,
                   discovery_snapshot_t discovery,
                   service_provider_t &services,
                   serializer_registry_t &serializers,
                   const handler_registry_t &handlers,
                   std::atomic_bool &stop) :
        _runtime (detail::channel_runtime_t::from (bus)),
        _channel_name (std::move (channel_name)),
        _endpoints (std::move (endpoints)),
        _discovery_snapshot (std::move (discovery)),
        _services (&services),
        _serializers (&serializers),
        _handlers (&handlers),
        _stop (&stop),
        _context (std::make_unique<zlink::context_t> ()),
        _router (std::make_unique<zlink::router_socket_t> (*_context))
    {
        if (routing_id) {
            _router->set_routing_id (*routing_id);
        }
        if (use_discovery && !_discovery_snapshot.registry_endpoints.empty ()) {
            try {
                _discovery = std::make_unique<zlink::service::discovery_t> (
                  *_context, zlink::auto_connect_type::client_server, _channel_name);
                for (const auto &endpoint : _discovery_snapshot.registry_endpoints) {
                    detail::connect_registry_with_retry (*_discovery, endpoint);
                }
                _router->attach_discovery (*_discovery);
            }
            catch (const std::exception &error) {
                throw framework_exception_t (
                  framework_error_kind_t::request_failed,
                  "channel '" + _channel_name
                    + "' discovery attach failed: " + error.what ());
            }
        }
        for (const auto &endpoint : _endpoints) {
            _router->bind (endpoint);
        }
    }

    ~server_loop_t () { stop (); }

    void run ()
    {
        while (!_stop->load (std::memory_order_acquire)) {
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
            dispatch_async (std::move (received));
        }
        flush_replies ();
    }

    void stop () noexcept
    {
        if (_discovery) {
            try {
                _discovery->close ();
            }
            catch (...) {
            }
            _discovery.reset ();
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
                _context->term ();
            }
            catch (...) {
            }
        }
        join_workers ();
    }

  private:
    struct completed_reply_t
    {
        zlink::received_t received;
        zlink::framework::runtime::messaging::message_parts_t parts;
    };

    static zlink::message_t clone (const zlink::message_t &message)
    {
        return zlink::message_t::from (message.to_string ());
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

    void dispatch_async (zlink::received_t received)
    {
        auto request_parts = copy_parts (received.parts ());
        std::lock_guard<std::mutex> lock (_workers_mutex);
        _workers.emplace_back (
          [this, received = std::move (received), request_parts = std::move (request_parts)] () mutable {
              detail::channel_packet_dispatcher_t dispatcher (_runtime);
              auto scope = _services->create_scope (service_scope_kind_t::handler_invocation);
              auto reply = dispatcher.dispatch_server_message (
                _channel_name, request_parts, scope.provider (), *_serializers, *_handlers);
              if (!reply || reply.value ().size () == 0 || !received.request_seq ()) {
                  return;
              }
              std::lock_guard<std::mutex> reply_lock (_replies_mutex);
              _replies.push_back (completed_reply_t{std::move (received), std::move (reply.value ())});
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
            if (completed.parts.size () == 1) {
                zlink::message_t part = clone (completed.parts[0]);
                try {
                    completed.received.reply ().message (part).submit ();
                }
                catch (const std::exception &error) {
                    std::cerr << "zlink framework channel late reply ignored: " << error.what ()
                              << '\n';
                }
                catch (...) {
                    std::cerr << "zlink framework channel late reply ignored\n";
                }
                continue;
            }
            zlink::message_t header = clone (completed.parts[0]);
            zlink::message_t body = clone (completed.parts[1]);
            try {
                completed.received.reply ().message (header).message (body).submit ();
            }
            catch (const std::exception &error) {
                std::cerr << "zlink framework channel late reply ignored: " << error.what ()
                          << '\n';
            }
            catch (...) {
                std::cerr << "zlink framework channel late reply ignored\n";
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

    detail::channel_runtime_t _runtime;
    std::string _channel_name;
    std::vector<std::string> _endpoints;
    discovery_snapshot_t _discovery_snapshot;
    service_provider_t *_services;
    serializer_registry_t *_serializers;
    const handler_registry_t *_handlers;
    std::atomic_bool *_stop;
    std::unique_ptr<zlink::context_t> _context;
    std::unique_ptr<zlink::router_socket_t> _router;
    std::unique_ptr<zlink::service::discovery_t> _discovery;
    std::mutex _workers_mutex;
    std::vector<std::thread> _workers;
    std::mutex _replies_mutex;
    std::deque<completed_reply_t> _replies;
};

channel_host_service_t::channel_host_service_t (message_bus_t bus,
                                                std::vector<channel_snapshot_t> channels,
                                                discovery_snapshot_t discovery,
                                                handler_registry_t &handlers,
                                                serializer_registry_t &serializers) :
    _bus (std::move (bus)),
    _channels (std::move (channels)),
    _discovery (std::move (discovery)),
    _handlers (&handlers),
    _serializers (&serializers)
{
}

channel_host_service_t::~channel_host_service_t () = default;

void channel_host_service_t::start (service_provider_t &services)
{
    _services = &services;
    _stop.store (false, std::memory_order_release);
    for (const auto &channel : _channels) {
        if (!channel.server.enabled || channel.server.bind_endpoints.empty ()) {
            continue;
        }
        const bool publish_to_discovery = !_discovery.registry_endpoints.empty ();
        auto loop = std::make_unique<server_loop_t> (_bus, channel.name,
                                                     publish_to_discovery,
                                                     channel.server.bind_endpoints,
                                                     channel.server.routing_id, _discovery, services,
                                                     *_serializers, *_handlers, _stop);
        auto *raw = loop.get ();
        _loops.push_back (std::move (loop));
        _threads.emplace_back ([raw] { raw->run (); });
    }
}

void channel_host_service_t::stop () noexcept
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
