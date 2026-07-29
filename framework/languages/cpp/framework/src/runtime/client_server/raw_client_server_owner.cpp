/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/client_server/raw_client_server_owner.hpp"

#include <zlink.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace zlink::framework::runtime::client_server
{
namespace
{

std::string advertised_endpoint (
  std::string bound_endpoint,
  const std::optional<std::string> &advertise_host)
{
    if (!advertise_host)
        return bound_endpoint;
    const auto port = bound_endpoint.rfind (':');
    if (!bound_endpoint.starts_with ("tcp://")
        || port == std::string::npos || port < 6) {
        throw std::invalid_argument (
          "ClientServer advertise host requires a TCP bind endpoint");
    }
    const auto host =
      advertise_host->find (':') == std::string::npos
        ? *advertise_host
        : "[" + *advertise_host + "]";
    return "tcp://" + host + bound_endpoint.substr (port);
}

std::vector<std::uint8_t> monitor_connection_id (
  const zlink::monitor_event_t &event)
{
    return {
      static_cast<std::uint8_t> ((event.value >> 24u) & 0xffu),
      static_cast<std::uint8_t> ((event.value >> 16u) & 0xffu),
      static_cast<std::uint8_t> ((event.value >> 8u) & 0xffu),
      static_cast<std::uint8_t> (event.value & 0xffu)};
}

foundation::operation_terminal_t request_failure (
  detail::backend::raw_request_result_t result)
{
    if (result == detail::backend::raw_request_result_t::timed_out) {
        return foundation::operation_terminal_t::timed_out;
    }
    if (result == detail::backend::raw_request_result_t::terminated) {
        return foundation::operation_terminal_t::shutdown;
    }
    return foundation::operation_terminal_t::transport_failed;
}

} // namespace

raw_client_server_server_t::raw_client_server_server_t (
  raw_client_server_server_options_t options) :
    _options (std::move (options)),
    _mailbox (_options.mailbox_message_budget,
              _options.mailbox_byte_budget, 256, 1024 * 1024)
{
    if (_options.descriptor.channel_name.empty ()
        || _options.descriptor.server_routing_id.empty ()) {
        throw std::invalid_argument (
          "ClientServer server channel and routing id are required");
    }
}

raw_client_server_server_t::~raw_client_server_server_t () noexcept
{
    close ();
}

void raw_client_server_server_t::start ()
{
    std::lock_guard lock (_mutex);
    if (_port) {
        return;
    }
    if (_closed) {
        throw std::logic_error (
          "ClientServer server cannot restart after close");
    }
    auto context = std::make_unique<zlink::context_t> ();
    auto router = std::make_unique<zlink::router_socket_t> (*context);
    router->options ().handover (true);
    router->options ().mandatory (true);
    router->options ().linger (std::chrono::milliseconds (0));
    router->set_routing_id (
      zlink::routing_id_t::from (
        _options.descriptor.server_routing_id));
    auto monitor = std::make_unique<zlink::socket_monitor_t> (
      router->monitor_open (zlink::monitor_event::connection_ready
                            | zlink::monitor_event::disconnected));
    router->bind (_options.descriptor.advertised_endpoint);
    _options.descriptor.advertised_endpoint =
      advertised_endpoint (
        router->options ().last_endpoint (),
        _options.advertise_host);
    if (_options.descriptor.descriptor_revision
        == std::numeric_limits<std::uint64_t>::max ()) {
        throw std::overflow_error (
          "ClientServer descriptor revision is exhausted");
    }
    ++_options.descriptor.descriptor_revision;
    _options.descriptor.state = mesh::service_node_state_t::serving;
    _port = std::make_shared<detail::backend::raw_route_port_t> (
      *router, &_socket_mutex);
    _monitor = std::move (monitor);
    _router = std::move (router);
    _context = std::move (context);
}

void raw_client_server_server_t::close () noexcept
{
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    std::unique_ptr<zlink::router_socket_t> router;
    std::unique_ptr<zlink::socket_monitor_t> monitor;
    std::unique_ptr<zlink::context_t> context;
    {
        std::lock_guard lock (_mutex);
        if (_closed) {
            return;
        }
        _closed = true;
        port = std::move (_port);
        router = std::move (_router);
        monitor = std::move (_monitor);
        context = std::move (_context);
    }
    _mailbox.close ();
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

std::string raw_client_server_server_t::endpoint () const
{
    std::lock_guard lock (_mutex);
    return _options.descriptor.advertised_endpoint;
}

protocol::client_server_server_admission_t
raw_client_server_server_t::descriptor () const
{
    std::lock_guard lock (_mutex);
    return _options.descriptor;
}

void raw_client_server_server_t::update_descriptor (
  protocol::client_server_server_admission_t descriptor)
{
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    std::vector<std::vector<std::uint8_t>> clients;
    {
        std::lock_guard lock (_mutex);
        if (descriptor.channel_name
              != _options.descriptor.channel_name
            || descriptor.server_routing_id
                 != _options.descriptor.server_routing_id
            || descriptor.lifecycle_generation
                 != _options.descriptor.lifecycle_generation
            || descriptor.security_identity
                 != _options.descriptor.security_identity
            || descriptor.advertised_endpoint
                 != _options.descriptor.advertised_endpoint
            || descriptor.descriptor_revision
                 <= _options.descriptor.descriptor_revision) {
            throw std::invalid_argument (
              "ClientServer descriptor update violates its immutable fence");
        }
        _options.descriptor = descriptor;
        port = _port;
        clients.reserve (_connections.size ());
        for (const auto &[client, _] : _connections)
            clients.push_back (client);
    }
    if (!port)
        return;
    const auto update =
      protocol::encode_client_server_server_admission (
        protocol::command::update, descriptor);
    for (const auto &client : clients)
        (void) port->send (client, {update});
}

mesh::service_mailbox_t &
raw_client_server_server_t::mailbox () noexcept
{
    return _mailbox;
}

std::size_t raw_client_server_server_t::drain_monitor_events (
  mesh::service_liveness_registry_t::clock_t::time_point now)
{
    static_cast<void> (now);
    std::size_t count = 0;
    for (;;) {
        std::optional<zlink::monitor_event_t> event;
        {
            std::lock_guard lock (_mutex);
            if (!_monitor || !_monitor->valid ()) {
                return count;
            }
            event = _monitor->recv (zlink::recv_flags_t::dontwait);
        }
        if (!event) {
            return count;
        }
        ++count;
        if (!event->routing_id) {
            continue;
        }
        const auto client = event->routing_id->to_bytes ();
        const auto connection = monitor_connection_id (*event);
        if (event->event == zlink::monitor_event::connection_ready) {
            std::lock_guard lock (_mutex);
            _connections.insert_or_assign (client, connection);
        } else if (event->event == zlink::monitor_event::disconnected) {
            bool current = false;
            {
                std::lock_guard lock (_mutex);
                const auto found = _connections.find (client);
                current = found != _connections.end ()
                          && found->second == connection;
                if (current) {
                    _connections.erase (found);
                }
            }
            if (current) {
                (void) _liveness.disconnect (client, connection);
            }
        }
    }
}

client_server_pump_result_t raw_client_server_server_t::pump_one (
  mesh::service_liveness_registry_t::clock_t::time_point now)
{
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    {
        std::lock_guard lock (_mutex);
        port = _port;
    }
    if (!port) {
        return client_server_pump_result_t::no_data;
    }
    if (_pending_received) {
        if (!_mailbox.try_enqueue (
              std::move (*_pending_received))) {
            return client_server_pump_result_t::backpressured;
        }
        _pending_received.reset ();
        return client_server_pump_result_t::application;
    }
    const auto received = port->try_receive ();
    if (!received) {
        return client_server_pump_result_t::no_data;
    }
    if (received->parts.empty ()) {
        return client_server_pump_result_t::protocol_error;
    }
    try {
        const auto header =
          protocol::decode_header (received->parts.front ());
        if (header.kind == protocol::command::hello) {
            if (received->parts.size () != 1) {
                return client_server_pump_result_t::protocol_error;
            }
            const auto client =
              protocol::decode_client_server_client_admission (
                received->parts.front (), protocol::command::hello);
            if (client.channel_name != _options.descriptor.channel_name
                || client.security_identity
                     != _options.descriptor.security_identity) {
                (void) port->send (
                  received->source_routing_id,
                  {protocol::encode_reject (3)});
                return client_server_pump_result_t::infrastructure;
            }
            std::vector<std::uint8_t> connection;
            {
                std::lock_guard lock (_mutex);
                const auto found =
                  _connections.find (received->source_routing_id);
                if (found == _connections.end ()) {
                    return client_server_pump_result_t::protocol_error;
                }
                connection = found->second;
            }
            _liveness.admit (
              received->source_routing_id, connection, now);
            if (!port->send (
                  received->source_routing_id,
                  {protocol::encode_client_server_server_admission (
                    protocol::command::admit, _options.descriptor)})) {
                return client_server_pump_result_t::protocol_error;
            }
            return client_server_pump_result_t::infrastructure;
        }
        std::vector<std::uint8_t> connection;
        {
            std::lock_guard lock (_mutex);
            const auto found =
              _connections.find (received->source_routing_id);
            if (found == _connections.end ()) {
                return client_server_pump_result_t::protocol_error;
            }
            connection = found->second;
        }
        if (header.kind == protocol::command::livenessProbe
            || header.kind == protocol::command::livenessAck) {
            if (received->parts.size () != 1) {
                return client_server_pump_result_t::protocol_error;
            }
            const auto liveness =
              protocol::decode_liveness (received->parts.front ());
            if (liveness.kind == protocol::command::livenessProbe) {
                if (!_liveness.acknowledge_probe (
                      received->source_routing_id, connection,
                      liveness.probe_id)
                    || !port->send (
                      received->source_routing_id,
                      {protocol::encode_liveness (
                        protocol::command::livenessAck,
                        liveness.probe_id)})) {
                    return client_server_pump_result_t::protocol_error;
                }
            } else {
                (void) _liveness.acknowledge (
                  received->source_routing_id, connection,
                  liveness.probe_id, now);
            }
            return client_server_pump_result_t::infrastructure;
        }
        if ((header.kind != protocol::command::channelSend
             && header.kind != protocol::command::channelRequest)
            || received->parts.size () != 2) {
            return client_server_pump_result_t::protocol_error;
        }
        std::optional<std::uint64_t> correlation;
        std::string channel;
        if (header.kind == protocol::command::channelSend) {
            channel = protocol::decode_channel_send_header (
              received->parts.front ());
        } else {
            if (!received->request_sequence) {
                return client_server_pump_result_t::protocol_error;
            }
            auto request = protocol::decode_channel_request_header (
              received->parts.front ());
            correlation = request.correlation;
            channel = std::move (request.channel_name);
        }
        if (channel != _options.descriptor.channel_name) {
            return client_server_pump_result_t::protocol_error;
        }
        (void) protocol::decode_application_payload (received->parts[1]);
        mesh::service_mailbox_record_t record{
          std::move (channel),
          mesh::service_mailbox_domain_t::application,
          std::move (received->parts),
          std::move (received->source_routing_id),
          received->request_sequence,
          correlation};
        if (!_mailbox.try_enqueue (std::move (record))) {
            _pending_received.emplace (std::move (record));
            return client_server_pump_result_t::backpressured;
        }
        return client_server_pump_result_t::application;
    }
    catch (const protocol::service_wire_error_t &) {
        return client_server_pump_result_t::protocol_error;
    }
}

mesh::service_liveness_tick_t
raw_client_server_server_t::tick_liveness (
  mesh::service_liveness_registry_t::clock_t::time_point now)
{
    auto result = _liveness.tick (now);
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    {
        std::lock_guard lock (_mutex);
        port = _port;
    }
    if (port) {
        for (const auto &probe : result.probes) {
            (void) port->send (
              probe.node_routing_id,
              {protocol::encode_liveness (
                protocol::command::livenessProbe, probe.probe_id)});
        }
    }
    return result;
}

bool raw_client_server_server_t::reply (
  const mesh::service_mailbox_record_t &request,
  const protocol::application_payload_t &payload)
{
    if (request.source_routing_id.empty () || !request.request_sequence
        || !request.correlation) {
        throw std::invalid_argument (
          "ClientServer reply requires request context");
    }
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    {
        std::lock_guard lock (_mutex);
        port = _port;
    }
    return port
           && port->reply (
             {request.source_routing_id, request.request_sequence, {}},
             {protocol::encode_reply_header (
                *request.correlation, 0, 0),
              protocol::encode_application_payload (payload)});
}

bool raw_client_server_server_t::reply (
  const mesh::service_mailbox_record_t &request,
  std::uint32_t terminal_result,
  protocol::framework_error_code failure_code)
{
    if (request.source_routing_id.empty () || !request.request_sequence
        || !request.correlation) {
        throw std::invalid_argument (
          "ClientServer reply requires request context");
    }
    if (terminal_result == 0) {
        throw std::invalid_argument (
          "ClientServer failure reply requires terminal fields");
    }
    std::shared_ptr<detail::backend::raw_route_port_t> port;
    {
        std::lock_guard lock (_mutex);
        port = _port;
    }
    return port
           && port->reply (
             {request.source_routing_id, request.request_sequence, {}},
             {protocol::encode_reply_header (
               *request.correlation, terminal_result,
               static_cast<std::uint32_t> (failure_code))});
}

bool raw_client_server_server_t::byte_vector_less_t::operator() (
  const std::vector<std::uint8_t> &left,
  const std::vector<std::uint8_t> &right) const noexcept
{
    return std::lexicographical_compare (
      left.begin (), left.end (), right.begin (), right.end ());
}

raw_client_server_client_t::raw_client_server_client_t (
  raw_client_server_client_options_t options) :
    _options (std::move (options)),
    _operations (
      std::make_shared<foundation::operation_registry_t> (4096))
{
    if (_options.client_routing_id.empty ()
        || _options.admission.channel_name.empty ()
        || _options.expected_server.advertised_endpoint.empty ()) {
        throw std::invalid_argument (
          "ClientServer client identity, channel and endpoint are required");
    }
}

raw_client_server_client_t::~raw_client_server_client_t () noexcept
{
    close ();
}

void raw_client_server_client_t::start ()
{
    std::lock_guard lock (_mutex);
    if (_port) {
        return;
    }
    if (_closed) {
        throw std::logic_error (
          "ClientServer client cannot restart after close");
    }
    auto context = std::make_unique<zlink::context_t> ();
    auto dealer = std::make_unique<zlink::dealer_socket_t> (*context);
    dealer->options ().linger (std::chrono::milliseconds (0));
    dealer->set_routing_id (
      zlink::routing_id_t::from (_options.client_routing_id));
    auto monitor = std::make_unique<zlink::socket_monitor_t> (
      dealer->monitor_open (zlink::monitor_event::connection_ready
                            | zlink::monitor_event::disconnected));
    dealer->connect (_options.expected_server.advertised_endpoint);
    _port = std::make_shared<detail::backend::raw_dealer_port_t> (
      *dealer, &_socket_mutex);
    _monitor = std::move (monitor);
    _dealer = std::move (dealer);
    _context = std::move (context);
}

void raw_client_server_client_t::close () noexcept
{
    std::shared_ptr<detail::backend::raw_dealer_port_t> port;
    std::unique_ptr<zlink::dealer_socket_t> dealer;
    std::unique_ptr<zlink::socket_monitor_t> monitor;
    std::unique_ptr<zlink::context_t> context;
    {
        std::lock_guard lock (_mutex);
        if (_closed) {
            return;
        }
        _closed = true;
        _ready = false;
        port = std::move (_port);
        dealer = std::move (_dealer);
        monitor = std::move (_monitor);
        context = std::move (_context);
    }
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
    dealer.reset ();
    if (context) {
        try {
            context->shutdown ();
            context->term ();
        }
        catch (...) {
        }
    }
}

bool raw_client_server_client_t::ready () const noexcept
{
    std::lock_guard lock (_mutex);
    return _ready;
}

std::size_t raw_client_server_client_t::drain_monitor_events (
  mesh::service_liveness_registry_t::clock_t::time_point now)
{
    static_cast<void> (now);
    std::size_t count = 0;
    for (;;) {
        std::optional<zlink::monitor_event_t> event;
        std::shared_ptr<detail::backend::raw_dealer_port_t> port;
        {
            std::lock_guard lock (_mutex);
            if (!_monitor || !_monitor->valid ()) {
                return count;
            }
            event = _monitor->recv (zlink::recv_flags_t::dontwait);
            port = _port;
        }
        if (!event) {
            return count;
        }
        ++count;
        const auto connection = monitor_connection_id (*event);
        if (event->event == zlink::monitor_event::connection_ready) {
            {
                std::lock_guard lock (_mutex);
                _connection_id = connection;
                _ready = false;
            }
            if (port) {
                (void) port->send (
                  {protocol::encode_client_server_client_admission (
                    protocol::command::hello, _options.admission)});
            }
        } else if (event->event == zlink::monitor_event::disconnected) {
            bool current = false;
            {
                std::lock_guard lock (_mutex);
                current = _connection_id == connection;
                if (current) {
                    _ready = false;
                    _connection_id.clear ();
                }
            }
            if (current) {
                (void) _liveness.disconnect (
                  _options.expected_server.server_routing_id, connection);
            }
        }
    }
}

client_server_pump_result_t raw_client_server_client_t::pump_one (
  mesh::service_liveness_registry_t::clock_t::time_point now)
{
    std::shared_ptr<detail::backend::raw_dealer_port_t> port;
    {
        std::lock_guard lock (_mutex);
        port = _port;
    }
    if (!port) {
        return client_server_pump_result_t::no_data;
    }
    const auto received = port->try_receive ();
    if (!received) {
        return client_server_pump_result_t::no_data;
    }
    if (received->empty ()) {
        return client_server_pump_result_t::protocol_error;
    }
    try {
        const auto header = protocol::decode_header (received->front ());
        if (header.kind == protocol::command::admit
            || header.kind == protocol::command::update) {
            if (received->size () != 1) {
                return client_server_pump_result_t::protocol_error;
            }
            const auto server =
              protocol::decode_client_server_server_admission (
                received->front (), header.kind);
            std::vector<std::uint8_t> connection;
            {
                std::lock_guard lock (_mutex);
                const auto identity_is_not_pinned =
                  _options.expected_server.server_routing_id.empty ()
                  && _options.expected_server.lifecycle_generation == 0;
                if (server.channel_name
                      != _options.expected_server.channel_name
                    || (!identity_is_not_pinned
                        && (server.server_routing_id
                              != _options.expected_server.server_routing_id
                            || server.lifecycle_generation
                                 != _options.expected_server.lifecycle_generation))
                    || server.security_identity
                         != _options.expected_server.security_identity
                    || server.advertised_endpoint
                         != _options.expected_server.advertised_endpoint
                    || server.descriptor_revision
                         < _options.expected_server.descriptor_revision
                    || server.server_routing_id.empty ()
                    || server.lifecycle_generation == 0
                    || _connection_id.empty ()) {
                    return client_server_pump_result_t::protocol_error;
                }
                _options.expected_server = server;
                _ready =
                  server.state == mesh::service_node_state_t::serving
                  && server.weight > 0;
                connection = _connection_id;
            }
            _liveness.admit (
              server.server_routing_id, connection, now);
            return client_server_pump_result_t::infrastructure;
        }
        if (header.kind == protocol::command::reject) {
            (void) protocol::decode_reject (received->front ());
            std::lock_guard lock (_mutex);
            _ready = false;
            return client_server_pump_result_t::infrastructure;
        }
        if (header.kind == protocol::command::livenessProbe
            || header.kind == protocol::command::livenessAck) {
            if (received->size () != 1) {
                return client_server_pump_result_t::protocol_error;
            }
            std::vector<std::uint8_t> connection;
            {
                std::lock_guard lock (_mutex);
                connection = _connection_id;
            }
            const auto liveness =
              protocol::decode_liveness (received->front ());
            if (liveness.kind == protocol::command::livenessProbe) {
                if (!_liveness.acknowledge_probe (
                      _options.expected_server.server_routing_id,
                      connection, liveness.probe_id)
                    || !port->send (
                      {protocol::encode_liveness (
                        protocol::command::livenessAck,
                        liveness.probe_id)})) {
                    return client_server_pump_result_t::protocol_error;
                }
            } else {
                (void) _liveness.acknowledge (
                  _options.expected_server.server_routing_id,
                  connection, liveness.probe_id, now);
            }
            return client_server_pump_result_t::infrastructure;
        }
        return client_server_pump_result_t::protocol_error;
    }
    catch (const protocol::service_wire_error_t &) {
        return client_server_pump_result_t::protocol_error;
    }
}

mesh::service_liveness_tick_t
raw_client_server_client_t::tick_liveness (
  mesh::service_liveness_registry_t::clock_t::time_point now)
{
    auto result = _liveness.tick (now);
    std::shared_ptr<detail::backend::raw_dealer_port_t> port;
    {
        std::lock_guard lock (_mutex);
        port = _port;
    }
    if (port) {
        for (const auto &probe : result.probes) {
            (void) port->send (
              {protocol::encode_liveness (
                protocol::command::livenessProbe, probe.probe_id)});
        }
    }
    if (!result.timed_out_nodes.empty ()) {
        std::lock_guard lock (_mutex);
        _ready = false;
    }
    return result;
}

bool raw_client_server_client_t::send (
  const protocol::application_payload_t &payload)
{
    std::shared_ptr<detail::backend::raw_dealer_port_t> port;
    std::string channel;
    {
        std::lock_guard lock (_mutex);
        if (!_ready) {
            return false;
        }
        port = _port;
        channel = _options.admission.channel_name;
    }
    return port
           && port->send (
             {protocol::encode_channel_send_header (channel),
              protocol::encode_application_payload (payload)});
}

bool raw_client_server_client_t::request (
  const protocol::application_payload_t &payload,
  std::chrono::milliseconds timeout,
  client_server_request_callback_t callback)
{
    if (timeout <= std::chrono::milliseconds::zero ()) {
        throw std::invalid_argument (
          "ClientServer request timeout must be positive");
    }
    std::shared_ptr<detail::backend::raw_dealer_port_t> port;
    std::string channel;
    std::uint64_t correlation = 0;
    std::uint64_t lifecycle = 0;
    {
        std::lock_guard lock (_mutex);
        if (!_ready || !_port) {
            return false;
        }
        port = _port;
        channel = _options.admission.channel_name;
        lifecycle = _options.expected_server.lifecycle_generation;
        correlation = _next_correlation++;
        if (correlation == 0 || _next_correlation == 0) {
            _next_correlation = 1;
            throw std::overflow_error (
              "ClientServer correlation is exhausted");
        }
    }
    const auto id = operation_id (lifecycle, correlation);
    struct reply_state_t
    {
        std::mutex mutex;
        protocol::reply_header_t header{};
    };
    auto reply_state = std::make_shared<reply_state_t> ();
    if (!_operations->register_operation (
          id, foundation::operation_registry_t::clock_t::now () + timeout,
          [callback = std::move (callback), reply_state] (
            foundation::operation_terminal_t terminal,
            std::vector<std::uint8_t> reply_payload) mutable {
              protocol::reply_header_t reply_header{};
              {
                  std::lock_guard lock (reply_state->mutex);
                  reply_header = reply_state->header;
              }
              callback (client_server_request_completion_t{
                terminal, reply_header, std::move (reply_payload)});
          })) {
        return false;
    }
    const auto operations = _operations;
    const auto submitted = port->request (
      {protocol::encode_channel_request_header (
         correlation, channel),
       protocol::encode_application_payload (payload)},
      timeout,
      [operations, id, correlation, reply_state] (
        detail::backend::raw_request_result_t result,
        detail::backend::raw_message_t parts) {
          if (result != detail::backend::raw_request_result_t::ok) {
              (void) operations->fail (id, request_failure (result));
              return;
          }
          try {
              if (parts.empty () || parts.size () > 2) {
                  throw protocol::service_wire_error_t (
                    "ClientServer reply has an invalid part count");
              }
              const auto reply =
                protocol::decode_reply_header (parts.front ());
              if (reply.correlation != correlation) {
                  throw protocol::service_wire_error_t (
                    "ClientServer reply correlation does not match");
              }
              {
                  std::lock_guard lock (reply_state->mutex);
                  reply_state->header = reply;
              }
              if (reply.terminal_result != 0) {
                  if (parts.size () != 1) {
                      throw protocol::service_wire_error_t (
                        "failed ClientServer reply cannot carry payload");
                  }
                  (void) operations->complete (id, {});
                  return;
              }
              if (parts.size () != 2) {
                  throw protocol::service_wire_error_t (
                    "successful ClientServer reply requires payload");
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

std::size_t raw_client_server_client_t::expire_requests (
  foundation::operation_registry_t::clock_t::time_point now)
{
    return _operations->expire (now);
}

foundation::operation_id_t
raw_client_server_client_t::operation_id (
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

} // namespace zlink::framework::runtime::client_server
