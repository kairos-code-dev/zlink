/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/channel_host_service.hpp"

#include "runtime/channels/channel_packet_dispatcher.hpp"
#include "runtime/channels/channel_runtime.hpp"

#include <zlink.hpp>

#include <chrono>
#include <utility>

namespace zlink::framework::runtime
{

class channel_host_service_t::server_loop_t
{
  public:
    server_loop_t (message_bus_t bus,
                   std::string channel_name,
                   std::vector<std::string> endpoints,
                   service_provider_t &services,
                   serializer_registry_t &serializers,
                   const handler_registry_t &handlers,
                   std::atomic_bool &stop) :
        _runtime (detail::channel_runtime_t::from (bus)),
        _channel_name (std::move (channel_name)),
        _endpoints (std::move (endpoints)),
        _services (&services),
        _serializers (&serializers),
        _handlers (&handlers),
        _stop (&stop),
        _context (std::make_unique<zlink::context_t> ()),
        _router (std::make_unique<zlink::router_socket_t> (*_context))
    {
        for (const auto &endpoint : _endpoints) {
            _router->bind (endpoint);
        }
    }

    ~server_loop_t () { stop (); }

    void run ()
    {
        detail::channel_packet_dispatcher_t dispatcher (_runtime);
        while (!_stop->load (std::memory_order_acquire)) {
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
            auto reply = dispatcher.dispatch_server_message (
              _channel_name, copy_parts (received.parts ()), *_services, *_serializers, *_handlers);
            if (!reply || reply.value ().size () == 0 || !received.request_seq ()) {
                continue;
            }
            if (reply.value ().size () == 1) {
                zlink::message_t part = clone (reply.value ()[0]);
                received.reply ().message (part).submit ();
                continue;
            }
            zlink::message_t header = clone (reply.value ()[0]);
            zlink::message_t body = clone (reply.value ()[1]);
            received.reply ().message (header).message (body).submit ();
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

    detail::channel_runtime_t _runtime;
    std::string _channel_name;
    std::vector<std::string> _endpoints;
    service_provider_t *_services;
    serializer_registry_t *_serializers;
    const handler_registry_t *_handlers;
    std::atomic_bool *_stop;
    std::unique_ptr<zlink::context_t> _context;
    std::unique_ptr<zlink::router_socket_t> _router;
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
    _stop.store (false, std::memory_order_release);
    for (const auto &channel : _channels) {
        if (!channel.server.enabled || channel.server.bind_endpoints.empty ()) {
            continue;
        }
        auto loop = std::make_unique<server_loop_t> (_bus, channel.name,
                                                     channel.server.bind_endpoints, services,
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
