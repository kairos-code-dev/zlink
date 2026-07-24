/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/mesh/raw_mesh_node_owner.hpp"

#include "runtime/protocol/service_wire_codec.hpp"

#include <zlink.hpp>

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace zlink::framework::runtime::mesh
{
namespace
{

std::vector<std::uint8_t> pack_infrastructure_reply (
  const detail::backend::raw_message_t &parts)
{
    std::size_t size = 1;
    for (const auto &part : parts) {
        if (part.size () > std::numeric_limits<std::uint32_t>::max ())
            throw protocol::service_wire_error_t (
              "infrastructure reply part is too large");
        size += 4 + part.size ();
    }
    std::vector<std::uint8_t> packed;
    packed.reserve (size);
    packed.push_back (static_cast<std::uint8_t> (parts.size ()));
    for (const auto &part : parts) {
        const auto length = static_cast<std::uint32_t> (part.size ());
        packed.push_back (
          static_cast<std::uint8_t> ((length >> 24u) & 0xffu));
        packed.push_back (
          static_cast<std::uint8_t> ((length >> 16u) & 0xffu));
        packed.push_back (
          static_cast<std::uint8_t> ((length >> 8u) & 0xffu));
        packed.push_back (static_cast<std::uint8_t> (length & 0xffu));
        packed.insert (packed.end (), part.begin (), part.end ());
    }
    return packed;
}

} // namespace

bool raw_mesh_byte_vector_less_t::operator() (
  const std::vector<std::uint8_t> &left,
  const std::vector<std::uint8_t> &right) const noexcept
{
    return std::lexicographical_compare (
      left.begin (), left.end (), right.begin (), right.end ());
}

raw_mesh_node_owner_t::raw_mesh_node_owner_t (raw_mesh_node_options_t options) :
    _options (std::move (options)),
    _topology (_options.descriptor),
    _mailbox (_options.application_message_budget,
              _options.application_byte_budget,
              _options.infrastructure_message_budget,
              _options.infrastructure_byte_budget),
    _operations (
      std::make_shared<foundation::operation_registry_t> (4096))
{
}

raw_mesh_node_owner_t::~raw_mesh_node_owner_t () noexcept
{
    close ();
}

void raw_mesh_node_owner_t::start ()
{
    std::lock_guard lifecycle_lock (_lifecycle_mutex);
    if (_port) {
        return;
    }
    if (_closed) {
        throw std::logic_error ("raw mesh node owner cannot restart after close");
    }
    auto context = std::make_unique<zlink::context_t> ();
    auto router = std::make_unique<zlink::router_socket_t> (*context);
    router->options ().handover (true);
    router->options ().mandatory (true);
    router->options ().linger (std::chrono::milliseconds (0));
    router->set_routing_id (
      zlink::routing_id_t::from (_options.descriptor.node_routing_id));
    auto monitor = std::make_unique<zlink::socket_monitor_t> (
      router->monitor_open (zlink::monitor_event::connection_ready
                            | zlink::monitor_event::disconnected));
    router->bind (_options.descriptor.advertised_endpoint);

    auto descriptor = _topology.local_descriptor ();
    if (descriptor.descriptor_revision
        == std::numeric_limits<std::uint64_t>::max ()) {
        throw std::overflow_error ("service descriptor revision is exhausted");
    }
    descriptor.advertised_endpoint = router->options ().last_endpoint ();
    descriptor.state = service_node_state_t::serving;
    ++descriptor.descriptor_revision;
    _topology.publish_local (descriptor);
    _options.descriptor = descriptor;

    _port = std::make_shared<detail::backend::raw_route_port_t> (
      *router, &_socket_mutex);
    _monitor = std::move (monitor);
    _router = std::move (router);
    _context = std::move (context);
}

void raw_mesh_node_owner_t::close () noexcept
{
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    std::unique_ptr<zlink::router_socket_t> router;
    std::unique_ptr<zlink::socket_monitor_t> monitor;
    std::unique_ptr<zlink::context_t> context;
    {
        std::lock_guard lifecycle_lock (_lifecycle_mutex);
        _closed = true;
        port = std::move (_port);
        monitor = std::move (_monitor);
        router = std::move (_router);
        context = std::move (_context);
    }
    _mailbox.close ();
    _operations->shutdown ();
    if (port) {
        port->close ();
    }
    if (monitor) {
        try {
            monitor->close ();
        }
        catch (...) {
        }
    }
    router.reset ();
    if (context) {
        try {
            context->shutdown ();
            context->term ();
        }
        catch (...) {
        }
    }
}

bool raw_mesh_node_owner_t::started () const noexcept
{
    std::lock_guard lifecycle_lock (_lifecycle_mutex);
    return static_cast<bool> (_port);
}

std::string raw_mesh_node_owner_t::endpoint () const
{
    return _topology.local_descriptor ().advertised_endpoint;
}

zlink::context_t &raw_mesh_node_owner_t::context ()
{
    std::lock_guard lifecycle_lock (_lifecycle_mutex);
    if (!_context) {
        throw std::logic_error ("raw mesh node owner is not started");
    }
    return *_context;
}

service_topology_registry_t &raw_mesh_node_owner_t::topology () noexcept
{
    return _topology;
}

service_liveness_registry_t &raw_mesh_node_owner_t::liveness () noexcept
{
    return _liveness;
}

service_mailbox_t &raw_mesh_node_owner_t::mailbox () noexcept
{
    return _mailbox;
}

bool raw_mesh_node_owner_t::connect_peer (const std::string &endpoint)
{
    std::lock_guard lifecycle_lock (_lifecycle_mutex);
    if (!_router || endpoint.empty ()) {
        return false;
    }
    try {
        std::lock_guard socket_lock (_socket_mutex);
        _router->connect (endpoint);
        return true;
    }
    catch (...) {
        return false;
    }
}

bool raw_mesh_node_owner_t::connect_peer (
  const std::string &endpoint,
  service_node_descriptor_t expected_descriptor)
{
    std::lock_guard lifecycle_lock (_lifecycle_mutex);
    if (!_router || endpoint.empty ()) {
        return false;
    }
    _expected_peers.insert_or_assign (
      expected_descriptor.node_routing_id, expected_descriptor);
    try {
        std::lock_guard socket_lock (_socket_mutex);
        _router->connect (endpoint);
        return true;
    }
    catch (...) {
        return false;
    }
}

peer_admission_result_t raw_mesh_node_owner_t::admit_peer (
  service_node_descriptor_t descriptor,
  std::vector<std::uint8_t> connection_id,
  service_liveness_registry_t::clock_t::time_point now)
{
    std::lock_guard lifecycle_lock (_lifecycle_mutex);
    if (!_router) {
        return peer_admission_result_t::invalid_descriptor;
    }
    auto node_routing_id = descriptor.node_routing_id;
    auto liveness_connection_id = connection_id;
    const auto admitted =
      _topology.admit (std::move (descriptor), std::move (connection_id));
    if (admitted != peer_admission_result_t::admitted) {
        return admitted;
    }
    _liveness.admit (std::move (node_routing_id),
                     std::move (liveness_connection_id), now);
    return admitted;
}

bool raw_mesh_node_owner_t::send_to_node (
  const std::vector<std::uint8_t> &target_routing_id,
  const protocol::application_payload_t &application_payload)
{
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    {
        std::lock_guard lifecycle_lock (_lifecycle_mutex);
        port = _port;
    }
    if (!port) {
        return false;
    }
    detail::backend::raw_message_t wire;
    wire.reserve (2);
    wire.push_back (protocol::encode_node_send_header ());
    wire.push_back (protocol::encode_application_payload (application_payload));
    return port->send (target_routing_id, wire);
}

bool raw_mesh_node_owner_t::request_to_node (
  const std::vector<std::uint8_t> &target_routing_id,
  const protocol::application_payload_t &application_payload,
  std::chrono::milliseconds timeout,
  foundation::operation_registry_t::callback_t callback)
{
    return request_to_target (
      target_routing_id, application_payload, timeout, std::move (callback),
      std::nullopt);
}

bool raw_mesh_node_owner_t::request_to_channel (
  const std::string &channel_name,
  const protocol::application_payload_t &application_payload,
  std::chrono::milliseconds timeout,
  foundation::operation_registry_t::callback_t callback)
{
    const auto selected = _topology.select (channel_name);
    if (!selected) {
        return false;
    }
    return request_to_target (
      selected->descriptor.node_routing_id, application_payload, timeout,
      std::move (callback), channel_name);
}

bool raw_mesh_node_owner_t::request_to_target (
  const std::vector<std::uint8_t> &target_routing_id,
  const protocol::application_payload_t &application_payload,
  std::chrono::milliseconds timeout,
  foundation::operation_registry_t::callback_t callback,
  const std::optional<std::string> &channel_name)
{
    return request_with_header (
      target_routing_id,
      [channel_name] (std::uint64_t correlation) {
          return channel_name
                   ? protocol::encode_channel_request_header (
                       correlation, *channel_name)
                   : protocol::encode_node_request_header (correlation);
      },
      application_payload, timeout, std::move (callback));
}

bool raw_mesh_node_owner_t::request_with_header (
  const std::vector<std::uint8_t> &target_routing_id,
  const std::function<std::vector<std::uint8_t> (std::uint64_t)> &header,
  const protocol::application_payload_t &application_payload,
  std::chrono::milliseconds timeout,
  foundation::operation_registry_t::callback_t callback)
{
    if (timeout <= std::chrono::milliseconds::zero ()) {
        throw std::invalid_argument ("raw mesh request timeout must be positive");
    }
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    std::uint64_t correlation = 0;
    const auto local = _topology.local_descriptor ();
    {
        std::lock_guard lifecycle_lock (_lifecycle_mutex);
        port = _port;
        if (!port) {
            return false;
        }
        correlation = _next_correlation++;
        if (correlation == 0 || _next_correlation == 0) {
            _next_correlation = 1;
            throw std::overflow_error ("raw mesh request correlation is exhausted");
        }
    }
    const auto id =
      operation_id (local.lifecycle_generation, correlation);
    if (!_operations->register_operation (
          id, foundation::operation_registry_t::clock_t::now () + timeout,
          std::move (callback))) {
        return false;
    }
    detail::backend::raw_message_t wire{
      header (correlation),
      protocol::encode_application_payload (application_payload)};
    const auto operations = _operations;
    const auto submitted = port->request (
      target_routing_id, wire, timeout,
      [operations, id, correlation] (
        detail::backend::raw_request_result_t result,
        detail::backend::raw_message_t parts) {
          if (result != detail::backend::raw_request_result_t::ok) {
              const auto terminal =
                result == detail::backend::raw_request_result_t::timed_out
                  ? foundation::operation_terminal_t::timed_out
                : result == detail::backend::raw_request_result_t::terminated
                  ? foundation::operation_terminal_t::shutdown
                  : foundation::operation_terminal_t::transport_failed;
              (void) operations->fail (id, terminal);
              return;
          }
          try {
              if (parts.empty () || parts.size () > 2) {
                  throw protocol::service_wire_error_t (
                    "request reply has an invalid part count");
              }
              const auto reply =
                protocol::decode_reply_header (parts.front ());
              if (reply.correlation != correlation) {
                  throw protocol::service_wire_error_t (
                    "request reply correlation does not match");
              }
              if (reply.terminal_result != 0) {
                  if (parts.size () != 1) {
                      throw protocol::service_wire_error_t (
                        "failed request reply cannot carry a payload");
                  }
                  (void) operations->fail (
                    id, foundation::operation_terminal_t::transport_failed);
                  return;
              }
              if (parts.size () != 2) {
                  throw protocol::service_wire_error_t (
                    "successful request reply must carry a payload");
              }
              (void) protocol::decode_application_payload (parts[1]);
              (void) operations->complete (id, std::move (parts[1]));
          }
          catch (const protocol::service_wire_error_t &) {
              (void) operations->fail (
                id, foundation::operation_terminal_t::transport_failed);
          }
      });
    if (!submitted) {
        (void) _operations->fail (
          id, foundation::operation_terminal_t::transport_failed);
    }
    return submitted;
}

bool raw_mesh_node_owner_t::send_with_header (
  const std::vector<std::uint8_t> &target_routing_id,
  std::vector<std::uint8_t> header,
  const protocol::application_payload_t &application_payload)
{
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    {
        std::lock_guard lifecycle_lock (_lifecycle_mutex);
        port = _port;
    }
    if (!port) {
        return false;
    }
    return port->send (
      target_routing_id,
      {std::move (header),
       protocol::encode_application_payload (application_payload)});
}

bool raw_mesh_node_owner_t::reply (
  const service_mailbox_record_t &request,
  const protocol::application_payload_t &application_payload)
{
    if (request.source_routing_id.empty () || !request.request_sequence
        || !request.correlation) {
        throw std::invalid_argument (
          "raw mesh reply requires a request mailbox record");
    }
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    {
        std::lock_guard lifecycle_lock (_lifecycle_mutex);
        port = _port;
    }
    if (!port) {
        return false;
    }
    return port->reply (
      detail::backend::raw_received_t{
        request.source_routing_id, request.request_sequence, {}},
      {protocol::encode_reply_header (*request.correlation, 0, 0),
       protocol::encode_application_payload (application_payload)});
}

bool raw_mesh_node_owner_t::reply_failure (
  const service_mailbox_record_t &request,
  std::uint32_t terminal_result,
  std::uint32_t failure_code)
{
    if (request.source_routing_id.empty () || !request.request_sequence
        || !request.correlation || terminal_result == 0) {
        throw std::invalid_argument (
          "raw mesh failed reply requires a request and terminal result");
    }
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    {
        std::lock_guard lifecycle_lock (_lifecycle_mutex);
        port = _port;
    }
    if (!port) {
        return false;
    }
    return port->reply (
      detail::backend::raw_received_t{
        request.source_routing_id, request.request_sequence, {}},
      {protocol::encode_reply_header (
        *request.correlation, terminal_result, failure_code)});
}

std::size_t raw_mesh_node_owner_t::expire_requests (
  foundation::operation_registry_t::clock_t::time_point now)
{
    return _operations->expire (now);
}

bool raw_mesh_node_owner_t::send_to_channel (
  const std::string &channel_name,
  const protocol::application_payload_t &application_payload)
{
    const auto selected = _topology.select (channel_name);
    if (!selected) {
        return false;
    }
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    {
        std::lock_guard lifecycle_lock (_lifecycle_mutex);
        port = _port;
    }
    if (!port) {
        return false;
    }
    detail::backend::raw_message_t wire;
    wire.reserve (2);
    wire.push_back (protocol::encode_channel_send_header (channel_name));
    wire.push_back (protocol::encode_application_payload (application_payload));
    return port->send (selected->descriptor.node_routing_id, wire);
}

bool raw_mesh_node_owner_t::send_to_spot (
  const std::vector<std::uint8_t> &target_routing_id,
  const std::vector<std::uint8_t> &source_spot_routing_id,
  const protocol::spot_route_fence_t &target,
  const protocol::application_payload_t &application_payload)
{
    return send_with_header (
      target_routing_id,
      protocol::encode_spot_message_header (
        protocol::command::spotSend, source_spot_routing_id, target),
      application_payload);
}

bool raw_mesh_node_owner_t::request_to_spot (
  const std::vector<std::uint8_t> &target_routing_id,
  const std::vector<std::uint8_t> &source_spot_routing_id,
  const protocol::spot_route_fence_t &target,
  const protocol::application_payload_t &application_payload,
  std::chrono::milliseconds timeout,
  foundation::operation_registry_t::callback_t callback)
{
    return request_with_header (
      target_routing_id,
      [source_spot_routing_id, target] (std::uint64_t correlation) {
          return protocol::encode_spot_message_header (
            protocol::command::spotRequest, source_spot_routing_id,
            target, correlation);
      },
      application_payload, timeout, std::move (callback));
}

bool raw_mesh_node_owner_t::send_to_actor (
  const std::vector<std::uint8_t> &target_routing_id,
  const std::optional<std::pair<std::string, std::uint64_t>> &source_actor,
  const protocol::actor_route_fence_t &target,
  const protocol::application_payload_t &application_payload)
{
    return send_with_header (
      target_routing_id,
      protocol::encode_actor_message_header (
        protocol::command::actorSend, source_actor, target),
      application_payload);
}

bool raw_mesh_node_owner_t::request_to_actor (
  const std::vector<std::uint8_t> &target_routing_id,
  const std::optional<std::pair<std::string, std::uint64_t>> &source_actor,
  const protocol::actor_route_fence_t &target,
  const protocol::application_payload_t &application_payload,
  std::chrono::milliseconds timeout,
  foundation::operation_registry_t::callback_t callback)
{
    return request_with_header (
      target_routing_id,
      [source_actor, target] (std::uint64_t correlation) {
          return protocol::encode_actor_message_header (
            protocol::command::actorRequest, source_actor, target,
            correlation);
      },
      application_payload, timeout, std::move (callback));
}

bool raw_mesh_node_owner_t::request_user_spot_create (
  const std::vector<std::uint8_t> &target_routing_id,
  protocol::user_spot_create_header_t request,
  std::chrono::milliseconds timeout,
  foundation::operation_registry_t::callback_t callback)
{
    const auto local = _topology.local_descriptor ();
    if (request.source_node_routing_id != local.node_routing_id
        || request.source_node_generation
             != local.lifecycle_generation
        || request.reservation.target_node_routing_id
             != target_routing_id) {
        throw std::invalid_argument (
          "User Spot create source or target fence is inconsistent");
    }
    return request_infrastructure (
      target_routing_id,
      [request = std::move (request)] (
        std::uint64_t correlation) mutable {
          request.correlation = correlation;
          return protocol::encode_user_spot_create_header (request);
      },
      [] (const detail::backend::raw_message_t &parts) {
          if (parts.empty () || parts.size () > 2)
              throw protocol::service_wire_error_t (
                "User Spot create reply has an invalid part count");
          const auto reply =
            protocol::decode_user_spot_create_reply (parts.front ());
          if (reply.header.terminal_result != 0
              && parts.size () != 1)
              throw protocol::service_wire_error_t (
                "failed User Spot create reply carries a payload");
          if (parts.size () == 2)
              (void) protocol::decode_application_payload (parts[1]);
          return pack_infrastructure_reply (parts);
      },
      timeout, std::move (callback));
}

bool raw_mesh_node_owner_t::request_user_spot_close (
  const std::vector<std::uint8_t> &target_routing_id,
  protocol::user_spot_close_header_t request,
  std::chrono::milliseconds timeout,
  foundation::operation_registry_t::callback_t callback)
{
    const auto local = _topology.local_descriptor ();
    if (request.source_node_routing_id != local.node_routing_id
        || request.source_node_generation
             != local.lifecycle_generation
        || request.target.target_node_routing_id != target_routing_id) {
        throw std::invalid_argument (
          "User Spot close source or target fence is inconsistent");
    }
    return request_infrastructure (
      target_routing_id,
      [request = std::move (request)] (
        std::uint64_t correlation) mutable {
          request.correlation = correlation;
          return protocol::encode_user_spot_close_header (request);
      },
      [] (const detail::backend::raw_message_t &parts) {
          if (parts.size () != 1)
              throw protocol::service_wire_error_t (
                "User Spot close reply must contain one header");
          (void) protocol::decode_user_spot_close_reply (parts.front ());
          return pack_infrastructure_reply (parts);
      },
      timeout, std::move (callback));
}

bool raw_mesh_node_owner_t::request_infrastructure (
  const std::vector<std::uint8_t> &target_routing_id,
  const std::function<std::vector<std::uint8_t> (std::uint64_t)> &header,
  const std::function<std::vector<std::uint8_t> (
    const detail::backend::raw_message_t &)> &decode_reply,
  std::chrono::milliseconds timeout,
  foundation::operation_registry_t::callback_t callback)
{
    if (timeout <= std::chrono::milliseconds::zero ()) {
        throw std::invalid_argument (
          "raw mesh infrastructure request timeout must be positive");
    }
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    std::uint64_t correlation = 0;
    const auto local = _topology.local_descriptor ();
    {
        std::lock_guard lifecycle_lock (_lifecycle_mutex);
        port = _port;
        if (!port) {
            return false;
        }
        correlation = _next_correlation++;
        if (correlation == 0 || _next_correlation == 0) {
            _next_correlation = 1;
            throw std::overflow_error (
              "raw mesh request correlation is exhausted");
        }
    }
    const auto id =
      operation_id (local.lifecycle_generation, correlation);
    if (!_operations->register_operation (
          id, foundation::operation_registry_t::clock_t::now () + timeout,
          std::move (callback))) {
        return false;
    }
    const auto operations = _operations;
    const auto submitted = port->request (
      target_routing_id, {header (correlation)}, timeout,
      [operations, id, correlation, decode_reply] (
        detail::backend::raw_request_result_t result,
        detail::backend::raw_message_t parts) {
          if (result != detail::backend::raw_request_result_t::ok) {
              const auto terminal =
                result == detail::backend::raw_request_result_t::timed_out
                  ? foundation::operation_terminal_t::timed_out
                : result == detail::backend::raw_request_result_t::terminated
                  ? foundation::operation_terminal_t::shutdown
                  : foundation::operation_terminal_t::transport_failed;
              (void) operations->fail (id, terminal);
              return;
          }
          try {
              if (parts.empty ()) {
                  throw protocol::service_wire_error_t (
                    "infrastructure reply has no header");
              }
              const auto prefix =
                protocol::decode_reply_header (
                  std::span<const std::uint8_t> (
                    parts.front ().data (),
                    std::min<std::size_t> (
                      parts.front ().size (), 21)));
              if (prefix.correlation != correlation) {
                  throw protocol::service_wire_error_t (
                    "infrastructure reply correlation does not match");
              }
              auto payload = decode_reply (parts);
              (void) operations->complete (
                id, std::move (payload));
          }
          catch (const protocol::service_wire_error_t &) {
              (void) operations->fail (
                id, foundation::operation_terminal_t::transport_failed);
          }
      });
    if (!submitted) {
        (void) _operations->fail (
          id, foundation::operation_terminal_t::transport_failed);
    }
    return submitted;
}

bool raw_mesh_node_owner_t::reply_infrastructure (
  const service_mailbox_record_t &request,
  std::vector<std::uint8_t> header)
{
    if (request.source_routing_id.empty () || !request.request_sequence
        || !request.correlation) {
        throw std::invalid_argument (
          "raw mesh infrastructure reply requires a request record");
    }
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    {
        std::lock_guard lifecycle_lock (_lifecycle_mutex);
        port = _port;
    }
    return port
           && port->reply (
             detail::backend::raw_received_t{
               request.source_routing_id, request.request_sequence, {}},
             {std::move (header)});
}

bool raw_mesh_node_owner_t::reply_user_spot_create (
  const service_mailbox_record_t &request,
  const protocol::user_spot_create_reply_t &reply,
  std::optional<protocol::application_payload_t> application_reply)
{
    if (!request.correlation
        || reply.header.correlation != *request.correlation) {
        throw std::invalid_argument (
          "User Spot create reply correlation does not match");
    }
    if (reply.header.terminal_result != 0 && application_reply)
        throw std::invalid_argument (
          "failed User Spot create reply cannot carry a payload");
    if (!application_reply)
        return reply_infrastructure (
          request,
          protocol::encode_user_spot_create_reply (
            reply.header.correlation, reply.header.terminal_result,
            reply.header.failure_code, reply.result,
            reply.spot_routing_id, reply.object_generation));
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    {
        std::lock_guard lifecycle_lock (_lifecycle_mutex);
        port = _port;
    }
    return port
           && port->reply (
             detail::backend::raw_received_t{
               request.source_routing_id, request.request_sequence, {}},
             {protocol::encode_user_spot_create_reply (
                reply.header.correlation, reply.header.terminal_result,
                reply.header.failure_code, reply.result,
                reply.spot_routing_id, reply.object_generation),
              protocol::encode_application_payload (*application_reply)});
}

bool raw_mesh_node_owner_t::reply_user_spot_close (
  const service_mailbox_record_t &request,
  const protocol::user_spot_close_reply_t &reply)
{
    if (!request.correlation
        || reply.header.correlation != *request.correlation) {
        throw std::invalid_argument (
          "User Spot close reply correlation does not match");
    }
    return reply_infrastructure (
      request,
      protocol::encode_user_spot_close_reply (
        reply.header.correlation, reply.header.terminal_result,
        reply.header.failure_code, reply.closed));
}

raw_mesh_pump_result_t raw_mesh_node_owner_t::pump_one (
  service_liveness_registry_t::clock_t::time_point now)
{
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    {
        std::lock_guard lifecycle_lock (_lifecycle_mutex);
        port = _port;
    }
    if (!port) {
        return raw_mesh_pump_result_t::no_data;
    }
    auto received = port->try_receive ();
    if (!received) {
        return raw_mesh_pump_result_t::no_data;
    }
    if (received->parts.empty ()) {
        return raw_mesh_pump_result_t::protocol_error;
    }
    try {
        const auto header = protocol::decode_header (received->parts.front ());
        if (header.kind == protocol::command::hello
            || header.kind == protocol::command::admit
            || header.kind == protocol::command::update) {
            if (received->parts.size () != 1) {
                return raw_mesh_pump_result_t::protocol_error;
            }
            const auto descriptor = protocol::decode_route_mesh_admission (
              received->parts.front (), header.kind,
              received->source_routing_id);
            std::vector<std::uint8_t> connection_id;
            {
                std::lock_guard lifecycle_lock (_lifecycle_mutex);
                const auto connection =
                  _connections.find (received->source_routing_id);
                if (connection == _connections.end ()) {
                    return raw_mesh_pump_result_t::protocol_error;
                }
                connection_id = connection->second;
                const auto expected =
                  _expected_peers.find (received->source_routing_id);
                if (expected != _expected_peers.end ()
                    && (expected->second.mesh_name != descriptor.mesh_name
                        || expected->second.node_routing_id
                             != descriptor.node_routing_id)) {
                    (void) port->send (
                      received->source_routing_id,
                      {protocol::encode_reject (3)});
                    return raw_mesh_pump_result_t::infrastructure;
                }
            }
            const auto admission =
              _topology.admit (descriptor, connection_id);
            if (admission != peer_admission_result_t::admitted) {
                const auto reason =
                  admission == peer_admission_result_t::mesh_mismatch ? 2u
                  : admission == peer_admission_result_t::stale_descriptor
                    ? 7u
                    : 11u;
                (void) port->send (
                  received->source_routing_id,
                  {protocol::encode_reject (reason)});
                return raw_mesh_pump_result_t::infrastructure;
            }
            _liveness.admit (
              descriptor.node_routing_id, connection_id, now);
            if (header.kind == protocol::command::hello) {
                (void) port->send (
                  received->source_routing_id,
                  {protocol::encode_route_mesh_admission (
                    protocol::command::admit,
                    _topology.local_descriptor ())});
            }
            return raw_mesh_pump_result_t::infrastructure;
        }
        if (header.kind == protocol::command::reject) {
            if (received->parts.size () != 1) {
                return raw_mesh_pump_result_t::protocol_error;
            }
            (void) protocol::decode_reject (received->parts.front ());
            const auto peer = _topology.peer (received->source_routing_id);
            if (peer) {
                (void) _topology.disconnect (
                  received->source_routing_id, peer->connection_id);
                (void) _liveness.disconnect (
                  received->source_routing_id, peer->connection_id);
            }
            return raw_mesh_pump_result_t::infrastructure;
        }
        const auto admitted = _topology.peer (received->source_routing_id);
        if (!admitted) {
            return raw_mesh_pump_result_t::protocol_error;
        }
        if (header.kind == protocol::command::livenessProbe
            || header.kind == protocol::command::livenessAck) {
            if (received->parts.size () != 1) {
                return raw_mesh_pump_result_t::protocol_error;
            }
            const auto record =
              protocol::decode_liveness (received->parts.front ());
            if (record.kind == protocol::command::livenessProbe) {
                const auto ack = _liveness.acknowledge_probe (
                  received->source_routing_id, admitted->connection_id,
                  record.probe_id);
                if (!ack
                    || !port->send (
                      received->source_routing_id,
                      {protocol::encode_liveness (
                        protocol::command::livenessAck, record.probe_id)})) {
                    return raw_mesh_pump_result_t::protocol_error;
                }
            } else {
                (void) _liveness.acknowledge (
                  received->source_routing_id, admitted->connection_id,
                  record.probe_id, now);
            }
            return raw_mesh_pump_result_t::infrastructure;
        }
        if (header.kind == protocol::command::userSpotCreate
            || header.kind == protocol::command::userSpotClose) {
            if (header.flags != 0 || received->parts.size () != 1
                || !received->request_sequence) {
                return raw_mesh_pump_result_t::protocol_error;
            }
            const auto local = _topology.local_descriptor ();
            std::vector<std::uint8_t> source_node_routing_id;
            std::uint64_t source_node_generation = 0;
            std::vector<std::uint8_t> target_node_routing_id;
            std::uint64_t target_node_generation = 0;
            std::uint64_t correlation = 0;
            if (header.kind
                == protocol::command::userSpotCreate) {
                const auto create =
                  protocol::decode_user_spot_create_header (
                    received->parts.front ());
                source_node_routing_id =
                  create.source_node_routing_id;
                source_node_generation =
                  create.source_node_generation;
                target_node_routing_id =
                  create.reservation.target_node_routing_id;
                target_node_generation =
                  create.reservation.target_node_generation;
                correlation = create.correlation;
            } else {
                const auto close =
                  protocol::decode_user_spot_close_header (
                    received->parts.front ());
                source_node_routing_id =
                  close.source_node_routing_id;
                source_node_generation =
                  close.source_node_generation;
                target_node_routing_id =
                  close.target.target_node_routing_id;
                target_node_generation =
                  close.target.target_node_generation;
                correlation = close.correlation;
            }
            if (source_node_routing_id
                  != received->source_routing_id
                || source_node_routing_id
                     != admitted->descriptor.node_routing_id
                || source_node_generation
                     != admitted->descriptor.lifecycle_generation
                || target_node_routing_id
                     != local.node_routing_id
                || target_node_generation
                     != local.lifecycle_generation) {
                return raw_mesh_pump_result_t::protocol_error;
            }
            if (!_mailbox.try_enqueue (
                  service_mailbox_record_t{
                    owner_key (local.node_routing_id),
                    service_mailbox_domain_t::infrastructure,
                    std::move (received->parts),
                    std::move (received->source_routing_id),
                    received->request_sequence,
                    correlation})) {
                return raw_mesh_pump_result_t::backpressured;
            }
            return raw_mesh_pump_result_t::infrastructure;
        }
        if ((header.kind != protocol::command::nodeSend
             && header.kind != protocol::command::nodeRequest
             && header.kind != protocol::command::channelSend
             && header.kind != protocol::command::channelRequest
             && header.kind != protocol::command::spotSend
             && header.kind != protocol::command::spotRequest
             && header.kind != protocol::command::actorSend
             && header.kind != protocol::command::actorRequest)
            || header.flags != 0 || received->parts.size () != 2) {
            return raw_mesh_pump_result_t::protocol_error;
        }
        (void) protocol::decode_application_payload (received->parts[1]);
        const auto local = _topology.local_descriptor ();
        std::string mailbox_owner;
        std::optional<std::uint64_t> correlation;
        if (header.kind == protocol::command::nodeSend
            || header.kind == protocol::command::nodeRequest) {
            if (header.kind == protocol::command::nodeSend
                && received->parts.front ().size () != 5) {
                return raw_mesh_pump_result_t::protocol_error;
            }
            if (header.kind == protocol::command::nodeRequest) {
                if (!received->request_sequence) {
                    return raw_mesh_pump_result_t::protocol_error;
                }
                correlation = protocol::decode_node_request_header (
                  received->parts.front ());
            }
            mailbox_owner = owner_key (local.node_routing_id);
        } else if (header.kind == protocol::command::channelSend
                   || header.kind == protocol::command::channelRequest) {
            std::string channel_name;
            if (header.kind == protocol::command::channelSend) {
                channel_name = protocol::decode_channel_send_header (
                  received->parts.front ());
            } else {
                if (!received->request_sequence) {
                    return raw_mesh_pump_result_t::protocol_error;
                }
                auto channel_request =
                  protocol::decode_channel_request_header (
                    received->parts.front ());
                correlation = channel_request.correlation;
                channel_name = std::move (channel_request.channel_name);
            }
            const auto channel = std::lower_bound (
              local.channels.begin (), local.channels.end (), channel_name,
              [] (const service_channel_descriptor_t &entry,
                  const std::string &name) {
                  return entry.name < name;
              });
            if (channel == local.channels.end () || channel->name != channel_name) {
                return raw_mesh_pump_result_t::protocol_error;
            }
            mailbox_owner = "channel:" + channel_name;
        } else if (header.kind == protocol::command::spotSend
                   || header.kind == protocol::command::spotRequest) {
            if (header.kind == protocol::command::spotRequest
                && !received->request_sequence) {
                return raw_mesh_pump_result_t::protocol_error;
            }
            const auto spot = protocol::decode_spot_message_header (
              received->parts.front (), header.kind);
            if (spot.target.target_node_routing_id
                  != local.node_routing_id
                || spot.target.target_node_generation
                     != local.lifecycle_generation) {
                return raw_mesh_pump_result_t::protocol_error;
            }
            correlation = spot.correlation;
            mailbox_owner = owner_key (spot.target.spot_routing_id);
            mailbox_owner.insert (0, "spot:");
        } else {
            if (header.kind == protocol::command::actorRequest
                && !received->request_sequence) {
                return raw_mesh_pump_result_t::protocol_error;
            }
            const auto actor = protocol::decode_actor_message_header (
              received->parts.front (), header.kind);
            if (actor.target.target_node_routing_id
                  != local.node_routing_id
                || actor.target.target_node_generation
                     != local.lifecycle_generation) {
                return raw_mesh_pump_result_t::protocol_error;
            }
            correlation = actor.correlation;
            mailbox_owner = "actor:" + actor.target.actor_id;
        }
        if (!_mailbox.try_enqueue (
              service_mailbox_record_t{
                std::move (mailbox_owner),
                service_mailbox_domain_t::application,
                std::move (received->parts),
                std::move (received->source_routing_id),
                received->request_sequence,
                correlation})) {
            return raw_mesh_pump_result_t::backpressured;
        }
        return raw_mesh_pump_result_t::application;
    }
    catch (const protocol::service_wire_error_t &) {
        return raw_mesh_pump_result_t::protocol_error;
    }
}

std::size_t raw_mesh_node_owner_t::drain_monitor_events (
  service_liveness_registry_t::clock_t::time_point now)
{
    std::size_t count = 0;
    for (;;) {
        std::optional<zlink::monitor_event_t> event;
        {
            std::lock_guard lifecycle_lock (_lifecycle_mutex);
            if (!_monitor || !_monitor->valid ()) {
                return count;
            }
            try {
                event = _monitor->recv (zlink::recv_flags_t::dontwait);
            }
            catch (...) {
                return count;
            }
        }
        if (!event) {
            return count;
        }
        ++count;
        if (!event->routing_id) {
            continue;
        }
        const auto node_routing_id = event->routing_id->to_bytes ();
        const std::vector<std::uint8_t> connection_id{
          static_cast<std::uint8_t> ((event->value >> 24u) & 0xffu),
          static_cast<std::uint8_t> ((event->value >> 16u) & 0xffu),
          static_cast<std::uint8_t> ((event->value >> 8u) & 0xffu),
          static_cast<std::uint8_t> (event->value & 0xffu)};
        if (event->event == zlink::monitor_event::connection_ready) {
            std::shared_ptr<detail::backend::raw_route_port_t> port;
            {
                std::lock_guard lifecycle_lock (_lifecycle_mutex);
                _connections.insert_or_assign (
                  node_routing_id, connection_id);
                port = _port;
            }
            if (port) {
                (void) port->send (
                  node_routing_id,
                  {protocol::encode_route_mesh_admission (
                    protocol::command::hello,
                    _topology.local_descriptor ())});
            }
        } else if (event->event == zlink::monitor_event::disconnected) {
            bool current = false;
            {
                std::lock_guard lifecycle_lock (_lifecycle_mutex);
                const auto found = _connections.find (node_routing_id);
                current = found != _connections.end ()
                          && found->second == connection_id;
                if (current) {
                    _connections.erase (found);
                }
            }
            if (current) {
                (void) _topology.disconnect (
                  node_routing_id, connection_id);
                (void) _liveness.disconnect (
                  node_routing_id, connection_id);
            }
        }
        static_cast<void> (now);
    }
}

service_liveness_tick_t raw_mesh_node_owner_t::tick_liveness (
  service_liveness_registry_t::clock_t::time_point now)
{
    auto result = _liveness.tick (now);
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    {
        std::lock_guard lifecycle_lock (_lifecycle_mutex);
        port = _port;
    }
    if (!port) {
        return result;
    }
    for (const auto &probe : result.probes) {
        (void) port->send (
          probe.node_routing_id,
          {protocol::encode_liveness (
            protocol::command::livenessProbe, probe.probe_id)});
    }
    for (const auto &timed_out : result.timed_out_nodes) {
        const auto peer = _topology.peer (timed_out);
        if (peer) {
            (void) _topology.disconnect (
              timed_out, peer->connection_id);
        }
    }
    return result;
}

std::string raw_mesh_node_owner_t::owner_key (
  const std::vector<std::uint8_t> &routing_id)
{
    std::ostringstream stream;
    stream << std::hex << std::setfill ('0');
    for (const auto byte : routing_id) {
        stream << std::setw (2) << static_cast<unsigned int> (byte);
    }
    return stream.str ();
}

foundation::operation_id_t raw_mesh_node_owner_t::operation_id (
  std::uint64_t lifecycle_generation,
  std::uint64_t correlation)
{
    foundation::operation_id_t id{};
    for (std::size_t index = 0; index < 8; ++index) {
        const auto shift = static_cast<unsigned int> ((7 - index) * 8);
        id[index] = static_cast<std::uint8_t> (
          (lifecycle_generation >> shift) & 0xffu);
        id[index + 8] =
          static_cast<std::uint8_t> ((correlation >> shift) & 0xffu);
    }
    return id;
}

} // namespace zlink::framework::runtime::mesh
