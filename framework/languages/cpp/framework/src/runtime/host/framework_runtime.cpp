/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/host/framework_runtime.hpp"

#include <zlink.hpp>

namespace zlink::framework::runtime
{

framework_runtime_t::framework_runtime_t () : _context (std::make_unique<zlink::context_t> ()), _offload (1)
{
}

framework_runtime_t::~framework_runtime_t ()
{
    drain ();
}

bool framework_runtime_t::owns_native_context () const noexcept
{
    return static_cast<bool> (_context);
}

zlink::router_socket_t &framework_runtime_t::channel_router ()
{
    if (!_router) {
        _router = std::make_unique<zlink::router_socket_t> (*_context);
    }
    return *_router;
}

zlink::dealer_socket_t &framework_runtime_t::channel_dealer ()
{
    if (!_dealer) {
        _dealer = std::make_unique<zlink::dealer_socket_t> (*_context);
    }
    return *_dealer;
}

zlink::stream_socket_t &framework_runtime_t::stream_socket ()
{
    if (!_stream) {
        _stream = std::make_unique<zlink::stream_socket_t> (*_context);
    }
    return *_stream;
}

zlink::service::discovery_t &framework_runtime_t::discovery ()
{
    if (!_discovery) {
        _discovery =
          std::make_unique<zlink::service::discovery_t> (*_context, zlink::auto_connect_type::route_mesh, "framework");
    }
    return *_discovery;
}

zlink::service::registry_t &framework_runtime_t::registry ()
{
    if (!_registry) {
        _registry = std::make_unique<zlink::service::registry_t> (*_context);
    }
    return *_registry;
}

zlink::service::spot_node_t &framework_runtime_t::add_spot_node ()
{
    if (!_spot_node) {
        _spot_node = std::make_unique<zlink::service::spot_node_t> (*_context);
    }
    return *_spot_node;
}

void framework_runtime_t::drain ()
{
    _offload.drain ();
    _spot_node.reset ();
    _registry.reset ();
    _discovery.reset ();
    _stream.reset ();
    _dealer.reset ();
    _router.reset ();
    if (_context) {
        _context->shutdown ();
        _context->term ();
        _context.reset ();
    }
}

offload_executor_t &framework_runtime_t::offload_executor () noexcept
{
    return _offload;
}

} // namespace zlink::framework::runtime
