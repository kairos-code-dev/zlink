/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/route_channel_runtime.hpp"

#include "runtime/backend/native_route_backend.hpp"

#include <utility>

namespace zlink::framework::detail
{

route_channel_runtime_t::route_channel_runtime_t (std::string router_channel_id) :
    _router_channel_id (std::move (router_channel_id))
{
}

const std::string &route_channel_runtime_t::router_channel_id () const noexcept
{
    return _router_channel_id;
}

void route_channel_runtime_t::routing_id (zlink::routing_id_t routing_id)
{
    _routing_id = std::move (routing_id);
}

const std::optional<zlink::routing_id_t> &route_channel_runtime_t::routing_id () const noexcept
{
    return _routing_id;
}

void route_channel_runtime_t::spot_route_egress_target (std::string target_spot_node_channel_name)
{
    _spot_route_egress_target = std::move (target_spot_node_channel_name);
}

const std::optional<std::string> &
route_channel_runtime_t::spot_route_egress_target () const noexcept
{
    return _spot_route_egress_target;
}

void route_channel_runtime_t::start () noexcept
{
    _running = true;
}

void route_channel_runtime_t::stop () noexcept
{
    _running = false;
    _pending_requests.clear ();
}

bool route_channel_runtime_t::running () const noexcept
{
    return _running;
}

bool route_channel_runtime_t::connect (std::string endpoint)
{
    return _connections.connect (std::move (endpoint));
}

bool route_channel_runtime_t::disconnect (const std::string &endpoint)
{
    return _connections.disconnect (endpoint);
}

std::vector<std::string> route_channel_runtime_t::list_connections () const
{
    return _connections.list ();
}

result_t<void>
route_channel_runtime_t::submit_send_parts (const zlink::routing_id_t &target_node_rid,
                                            runtime::messaging::message_parts_t parts)
{
    if (auto connected = ensure_connected (); !connected) {
        return connected;
    }
    _outbound_packets.push_back (
      route_outbound_packet_t{target_node_rid, std::nullopt, std::move (parts), std::nullopt});
    if (_send_backend) {
        return _send_backend (_outbound_packets.back ().target_node_rid,
                              _outbound_packets.back ().parts);
    }
    return result_t<void>::success ();
}

result_t<std::uint64_t>
route_channel_runtime_t::submit_request_parts (const zlink::routing_id_t &target_node_rid,
                                               runtime::messaging::message_parts_t parts)
{
    if (auto connected = ensure_connected (); !connected) {
        return result_t<std::uint64_t>::failure (
          connected.error_kind (),
          connected.error () ? connected.error ()->what () : "route channel is not connected");
    }
    const auto request_seq = _pending_requests.next_request_seq ();
    _pending_requests.register_request (request_seq, _router_channel_id);
    _outbound_packets.push_back (
      route_outbound_packet_t{target_node_rid, std::nullopt, std::move (parts), request_seq});
    return result_t<std::uint64_t>::success (request_seq);
}

result_t<runtime::messaging::message_parts_t>
route_channel_runtime_t::request_reply_parts (const zlink::routing_id_t &target_node_rid,
                                              runtime::messaging::message_parts_t parts,
                                              std::chrono::milliseconds timeout)
{
    if (auto connected = ensure_connected (); !connected) {
        return result_t<runtime::messaging::message_parts_t>::failure (
          connected.error_kind (),
          connected.error () ? connected.error ()->what () : "route channel is not connected");
    }
    const auto request_seq = _pending_requests.next_request_seq ();
    _pending_requests.register_request (request_seq, _router_channel_id);
    _outbound_packets.push_back (
      route_outbound_packet_t{target_node_rid, std::nullopt, parts, request_seq});
    if (!_request_backend) {
        _pending_requests.remove (request_seq);
        return result_t<runtime::messaging::message_parts_t>::failure (
          framework_error_kind_t::timeout, "route request reply was not completed by a backend");
    }
    auto reply = _request_backend (target_node_rid, parts, timeout);
    _pending_requests.remove (request_seq);
    return reply;
}

result_t<void>
route_channel_runtime_t::submit_spot_send_parts (const zlink::routing_id_t &target_node_rid,
                                                 const zlink::routing_id_t &target_spot_rid,
                                                 runtime::messaging::message_parts_t parts)
{
    if (auto connected = ensure_connected (); !connected) {
        return connected;
    }
    _outbound_packets.push_back (
      route_outbound_packet_t{target_node_rid, target_spot_rid, std::move (parts), std::nullopt});
    return result_t<void>::success ();
}

result_t<std::uint64_t>
route_channel_runtime_t::request_to_spot_parts (const zlink::routing_id_t &target_node_rid,
                                                const zlink::routing_id_t &target_spot_rid,
                                                runtime::messaging::message_parts_t parts)
{
    if (auto connected = ensure_connected (); !connected) {
        return result_t<std::uint64_t>::failure (
          connected.error_kind (),
          connected.error () ? connected.error ()->what () : "route channel is not connected");
    }
    const auto request_seq = _pending_requests.next_request_seq ();
    _pending_requests.register_request (request_seq, _router_channel_id);
    _outbound_packets.push_back (
      route_outbound_packet_t{target_node_rid, target_spot_rid, std::move (parts), request_seq});
    return result_t<std::uint64_t>::success (request_seq);
}

result_t<void> route_channel_runtime_t::complete_request (std::uint64_t request_seq)
{
    if (!_pending_requests.remove (request_seq)) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "routed reply does not match a pending request");
    }
    return result_t<void>::success ();
}

void route_channel_runtime_t::set_send_backend (send_backend_t backend)
{
    _send_backend = std::move (backend);
}

void route_channel_runtime_t::set_request_backend (request_backend_t backend)
{
    _request_backend = std::move (backend);
}

void route_channel_runtime_t::attach_native_backend (backend::native_route_backend_t &backend)
{
    set_send_backend ([&backend] (const zlink::routing_id_t &target_node_rid,
                                  const runtime::messaging::message_parts_t &parts) {
        return backend.submit_send (target_node_rid, parts);
    });
    set_request_backend ([&backend] (const zlink::routing_id_t &target_node_rid,
                                     const runtime::messaging::message_parts_t &parts,
                                     std::chrono::milliseconds timeout) {
        return backend.submit_request (target_node_rid, parts, timeout);
    });
}

const std::vector<route_outbound_packet_t> &
route_channel_runtime_t::outbound_packets () const noexcept
{
    return _outbound_packets;
}

std::size_t route_channel_runtime_t::pending_request_count () const noexcept
{
    return _pending_requests.count ();
}

result_t<void> route_channel_runtime_t::ensure_connected () const
{
    if (!_running) {
        return result_t<void>::failure (framework_error_kind_t::route_not_connected,
                                        "route channel runtime is not running");
    }
    if (_connections.list ().empty ()) {
        return result_t<void>::failure (framework_error_kind_t::route_not_connected,
                                        "route channel has no connected endpoint");
    }
    return result_t<void>::success ();
}

} // namespace zlink::framework::detail
