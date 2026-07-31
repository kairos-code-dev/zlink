/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/client_server/client_server_location_runtime.hpp"
#include "runtime/diagnostics/message_flow_tracer.hpp"
#include <runtime/locations/location_repository.hpp>
#include "runtime/client_server/weighted_selector.hpp"
#include "runtime/configuration/service_scope.hpp"
#include "runtime/messaging/request_failure_mapper.hpp"

#include <zlink.hpp>

#include <algorithm>
#include <chrono>
#include <future>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <utility>

namespace zlink::framework::runtime::client_server
{

namespace
{

constexpr std::string_view default_security_identity = "default";
constexpr auto pump_interval = std::chrono::milliseconds (2);
constexpr auto maximum_ready_wait = std::chrono::seconds (5);
constexpr std::uint32_t default_effective_max_message_bytes =
  static_cast<std::uint32_t> (
    std::numeric_limits<std::int32_t>::max ());

std::string connection_key (
  const client_server_server_descriptor_t &descriptor)
{
    return descriptor.server_rid.to_hex () + "|"
           + std::to_string (descriptor.lifecycle_generation);
}

struct client_server_wire_failure_t
{
    std::uint32_t terminal_result;
    protocol::framework_error_code failure_code;
};

client_server_wire_failure_t
wire_failure (const framework_exception_t &error)
{
    switch (error.kind ()) {
        case framework_error_kind_t::handler_not_found:
            return {102, protocol::framework_error_code::handlerNotFound};
        case framework_error_kind_t::payload_decode_failed:
            return {
              104, protocol::framework_error_code::payloadDecodeFailed};
        case framework_error_kind_t::route_not_connected:
            return {
              105, protocol::framework_error_code::routeNotConnected};
        case framework_error_kind_t::request_target_not_found:
            return {
              102,
              protocol::framework_error_code::requestTargetNotFound};
        case framework_error_kind_t::request_rejected:
            return {
              106, protocol::framework_error_code::requestRejected};
        case framework_error_kind_t::request_protocol_error:
            return {
              104,
              protocol::framework_error_code::requestProtocolError};
        default:
            return {105, protocol::framework_error_code::requestFailed};
    }
}

std::string stable_key (
  const client_server_server_descriptor_t &descriptor)
{
    return descriptor.server_rid.to_hex ();
}

std::string manual_connection_key (std::string_view endpoint)
{
    return "manual|" + std::string (endpoint);
}

client_server_server_descriptor_t manual_descriptor (
  std::string_view channel_name,
  std::string_view endpoint)
{
    return client_server_server_descriptor_t{
      .channel_name = std::string (channel_name),
      .endpoint = std::string (endpoint),
      .weight = 100,
      .state = framework_runtime_state_t::serving,
      .security_identity = std::string (default_security_identity)};
}

framework_runtime_state_t current_state (
  const location_runtime_t &locations)
{
    return locations.draining ()
             ? framework_runtime_state_t::draining
             : framework_runtime_state_t::serving;
}

} // namespace

mesh::service_node_state_t client_server_service_state (
  framework_runtime_state_t state)
{
    switch (state) {
        case framework_runtime_state_t::preparing:
            return mesh::service_node_state_t::preparing;
        case framework_runtime_state_t::serving:
            return mesh::service_node_state_t::serving;
        case framework_runtime_state_t::relocating:
        case framework_runtime_state_t::relocated:
        case framework_runtime_state_t::draining:
            return mesh::service_node_state_t::draining;
        case framework_runtime_state_t::stopped:
            return mesh::service_node_state_t::stopped;
        case framework_runtime_state_t::error:
            return mesh::service_node_state_t::error;
        default:
            throw std::invalid_argument (
              "invalid framework ClientServer runtime state");
    }
}

framework_runtime_state_t client_server_framework_state (
  mesh::service_node_state_t state)
{
    switch (state) {
        case mesh::service_node_state_t::preparing:
            return framework_runtime_state_t::preparing;
        case mesh::service_node_state_t::serving:
            return framework_runtime_state_t::serving;
        case mesh::service_node_state_t::retiring:
            return framework_runtime_state_t::relocating;
        case mesh::service_node_state_t::draining:
            return framework_runtime_state_t::draining;
        case mesh::service_node_state_t::stopped:
            return framework_runtime_state_t::stopped;
        case mesh::service_node_state_t::error:
            return framework_runtime_state_t::error;
        default:
            throw std::invalid_argument (
              "invalid service ClientServer runtime state");
    }
}

struct client_server_location_runtime_t::server_entry_t
{
    channel_capability_snapshot_t capability;
    std::unique_ptr<raw_client_server_server_t> owner;
    std::optional<client_server_server_descriptor_t> published_descriptor;
};

struct client_server_location_runtime_t::client_connection_t
{
    client_server_server_descriptor_t descriptor;
    std::shared_ptr<raw_client_server_client_t> owner;
};

struct client_server_location_runtime_t::client_channel_t
{
    channel_snapshot_t snapshot;
    std::vector<std::uint8_t> routing_id;
    std::map<std::string, client_connection_t> connections;
    smooth_weighted_selector_t selector;
};

client_server_location_runtime_t::client_server_location_runtime_t (
  message_bus_t bus,
  std::vector<channel_snapshot_t> channels,
  location_runtime_t &locations,
  location_repository_t &store,
  location_repository_t &leases,
  service_provider_t &services,
  serializer_registry_t &serializers,
  const handler_registry_t &handlers,
  std::map<std::string, std::string> advertise_hosts) :
    _bus (std::move (bus)),
    _channel_runtime (detail::channel_runtime_t::from (_bus)),
    _channels (std::move (channels)),
    _locations (&locations),
    _store (&store),
    _leases (&leases),
    _services (&services),
    _serializers (&serializers),
    _handlers (&handlers),
    _advertise_hosts (std::move (advertise_hosts))
{
}

client_server_location_runtime_t::~client_server_location_runtime_t () noexcept
{
    stop ();
}

bool client_server_location_runtime_t::empty () const noexcept
{
    return std::none_of (
      _channels.begin (), _channels.end (), [] (const auto &channel) {
          return (channel.server.enabled
                  && !channel.server.bind_endpoints.empty ())
                 || (channel.client.enabled
                     && (channel.client.discovery
                         || !channel.client.connect_endpoints.empty ()));
      });
}

void client_server_location_runtime_t::start ()
{
    if (empty ())
        return;
    const bool publishes_server =
      std::any_of (
        _channels.begin (), _channels.end (), [] (const auto &channel) {
            return channel.server.enabled && channel.server.discovery;
        });
    auto owner = _locations->current_owner_token ();
    if (publishes_server && !owner)
        throw std::runtime_error (
          "ClientServer publication requires an active owner lease");

    _stop.store (false, std::memory_order_release);
    try {
        for (const auto &channel : _channels) {
            if (channel.server.enabled
                && !channel.server.bind_endpoints.empty ())
                start_server (
                  channel,
                  channel.server.discovery ? owner : std::nullopt);
            if (channel.client.enabled
                && (channel.client.discovery
                    || !channel.client.connect_endpoints.empty ()))
                start_client (channel);
        }
        reconcile ();
        _channel_runtime.mark_auto_connect_active ();
        _thread = std::thread ([this] { run (); });
    }
    catch (...) {
        stop ();
        throw;
    }
}

void client_server_location_runtime_t::start_server (
  const channel_snapshot_t &channel,
  const std::optional<location_owner_token_t> &publication_owner)
{
    if (channel.server.bind_endpoints.size () != 1) {
        throw std::invalid_argument (
          "ClientServer server requires one bind endpoint");
    }
    protocol::client_server_server_admission_t admission;
    admission.channel_name = channel.name;
    admission.server_routing_id =
      server_routing_id (channel);
    admission.lifecycle_generation = make_lifecycle_generation ();
    admission.weight = static_cast<std::uint32_t> (
      channel.server.service_weight);
    admission.state = mesh::service_node_state_t::preparing;
    admission.security_identity =
      std::string (default_security_identity);
    admission.effective_max_message_bytes =
      effective_max_message_bytes (channel.server);
    admission.advertised_endpoint =
      channel.server.bind_endpoints.front ();

    const auto advertise =
      _advertise_hosts.find (channel.name);
    auto raw = std::make_unique<raw_client_server_server_t> (
      raw_client_server_server_options_t{
        admission,
        advertise == _advertise_hosts.end ()
          ? std::nullopt
          : std::optional<std::string> (advertise->second)});
    raw->start ();
    auto entry = std::make_unique<server_entry_t> ();
    entry->capability = channel.server;
    entry->owner = std::move (raw);
    if (publication_owner) {
        auto descriptor =
          to_descriptor (
            entry->owner->descriptor (), *publication_owner);
        const auto stored = _store
                              ->update_client_server (
                                descriptor,
                                location_write_intent_t::new_claim)
                              .result ()
                              .value ();
        if (stored.status != location_write_status_t::stored) {
            entry->owner->close ();
            throw std::runtime_error (
              "ClientServer descriptor publication was fenced");
        }
        entry->published_descriptor = std::move (descriptor);
    }
    _servers.emplace (channel.name, std::move (entry));
}

void client_server_location_runtime_t::start_client (
  const channel_snapshot_t &channel)
{
    auto entry = std::make_unique<client_channel_t> ();
    entry->snapshot = channel;
    entry->routing_id = client_routing_id (channel);
    _clients.emplace (channel.name, std::move (entry));
    _channel_runtime.bind_client_server_transport (
      channel.name,
      [this, name = channel.name] (
        std::string packet_name,
        std::string content_type,
        zlink::message_t message,
        std::chrono::milliseconds timeout) {
          return send (name, std::move (packet_name),
                       std::move (content_type),
                       std::move (message), timeout);
      },
      [this, name = channel.name] (
        std::string packet_name,
        std::string content_type,
        zlink::message_t message,
        std::chrono::milliseconds timeout) {
          return request (name, std::move (packet_name),
                          std::move (content_type),
                          std::move (message), timeout);
      });
}

void client_server_location_runtime_t::run ()
{
    auto next_reconcile = std::chrono::steady_clock::now ();
    while (!_stop.load (std::memory_order_acquire)) {
        const auto now = std::chrono::steady_clock::now ();
        if (now >= next_reconcile) {
            try {
                publish_servers ();
                reconcile ();
            }
            catch (...) {
                _locations->record_store_error ();
            }
            next_reconcile =
              now + _locations->options ().polling_interval;
        }
        pump ();
        std::this_thread::sleep_for (pump_interval);
    }
}

void client_server_location_runtime_t::publish_servers ()
{
    const auto owner = _locations->current_owner_token ();
    if (!owner)
        return;
    for (auto &[channel_name, server] : _servers) {
        if (!server->published_descriptor)
            continue;
        const auto weight_override =
          _channel_runtime.server_peer_weight_override (channel_name);
        const auto weight =
          weight_override.value_or (
            server->capability.service_weight);
        const auto state = current_state (*_locations);
        const bool new_owner =
          server->published_descriptor->owner_id != owner->owner_id
          || server->published_descriptor->lease_generation
               != owner->lease_generation;
        if (!new_owner
            && server->published_descriptor->weight == weight
            && server->published_descriptor->state == state)
            continue;

        auto admission = server->owner->descriptor ();
        if (admission.descriptor_revision
            == std::numeric_limits<std::uint64_t>::max ())
            throw std::overflow_error (
              "ClientServer descriptor revision is exhausted");
        ++admission.descriptor_revision;
        admission.weight = static_cast<std::uint32_t> (weight);
        admission.state = client_server_service_state (state);
        server->owner->update_descriptor (admission);
        auto descriptor = to_descriptor (admission, *owner);
        const auto written =
          _store
            ->update_client_server (
              descriptor,
              new_owner ? location_write_intent_t::new_claim
                        : location_write_intent_t::renew)
            .result ()
            .value ();
        if (written.status == location_write_status_t::stored)
            server->published_descriptor = std::move (descriptor);
    }
}

void client_server_location_runtime_t::reconcile ()
{
    for (auto &[_, channel] : _clients)
        reconcile_channel (*channel);
}

void client_server_location_runtime_t::reconcile_channel (
  client_channel_t &channel)
{
    std::map<std::string, client_server_server_descriptor_t> desired;
    location_page_request_t page;
    do {
        const auto listed =
          _store->list_client_servers (
                   channel.snapshot.name, page)
            .result ()
            .value ();
        for (const auto &descriptor : listed.items) {
            if ((descriptor.state
                   == framework_runtime_state_t::stopped)
                || descriptor.state
                     == framework_runtime_state_t::error
                || !owner_is_live (descriptor))
                continue;
            desired.insert_or_assign (
              connection_key (descriptor), descriptor);
        }
        page.continuation_token = listed.continuation_token;
    } while (page.continuation_token);

    for (const auto &endpoint :
         channel.snapshot.client.connect_endpoints) {
        const auto discovered = std::find_if (
          desired.begin (), desired.end (),
          [&] (const auto &entry) {
              return entry.second.endpoint == endpoint;
          });
        if (discovered == desired.end ()) {
            desired.emplace (
              manual_connection_key (endpoint),
              manual_descriptor (channel.snapshot.name, endpoint));
        }
    }
    std::vector<std::shared_ptr<raw_client_server_client_t>> close;
    for (const auto &[key, descriptor] : desired) {
        bool exists = false;
        {
            std::lock_guard lock (_gate);
            const auto found = channel.connections.find (key);
            if (found != channel.connections.end ()) {
                found->second.descriptor = descriptor;
                exists = true;
            } else if (!key.starts_with ("manual|")) {
                const auto manual = channel.connections.find (
                  manual_connection_key (descriptor.endpoint));
                if (manual != channel.connections.end ()) {
                    auto connection = std::move (manual->second);
                    channel.connections.erase (manual);
                    connection.descriptor = descriptor;
                    channel.connections.emplace (
                      key, std::move (connection));
                    exists = true;
                }
            }
        }
        if (exists)
            continue;
        protocol::client_server_client_admission_t admission;
        admission.channel_name = channel.snapshot.name;
        admission.security_identity =
          std::string (default_security_identity);
        admission.effective_max_message_bytes =
          effective_max_message_bytes (channel.snapshot.client);
        auto expected = key.starts_with ("manual|")
                          ? protocol::client_server_server_admission_t{
                              .channel_name =
                                channel.snapshot.name,
                              .security_identity =
                                std::string (
                                  default_security_identity),
                              .effective_max_message_bytes =
                                admission.effective_max_message_bytes,
                              .advertised_endpoint =
                                descriptor.endpoint}
                          : to_admission (
                              descriptor,
                              admission.effective_max_message_bytes);
        auto raw = std::make_shared<raw_client_server_client_t> (
          raw_client_server_client_options_t{
            channel.routing_id,
            admission,
            std::move (expected)});
        raw->start ();
        std::lock_guard lock (_gate);
        channel.connections.emplace (
          key, client_connection_t{descriptor, std::move (raw)});
    }

    {
        std::lock_guard lock (_gate);
        for (auto it = channel.connections.begin ();
             it != channel.connections.end ();) {
            if (desired.contains (it->first)) {
                ++it;
                continue;
            }
            const auto stable =
              stable_key (it->second.descriptor);
            const auto replacement = std::find_if (
              channel.connections.begin (),
              channel.connections.end (),
              [&] (const auto &candidate) {
                  return desired.contains (candidate.first)
                         && stable_key (
                              candidate.second.descriptor)
                              == stable;
              });
            if (replacement != channel.connections.end ()
                && !replacement->second.owner->ready ()) {
                ++it;
                continue;
            }
            close.push_back (it->second.owner);
            it = channel.connections.erase (it);
        }
    }
    for (auto &owner : close)
        owner->close ();
}

void client_server_location_runtime_t::pump ()
{
    const auto now =
      mesh::service_liveness_registry_t::clock_t::now ();
    for (auto &[_, server] : _servers) {
        (void) server->owner->drain_monitor_events (now);
        while (server->owner->pump_one (now)
               != client_server_pump_result_t::no_data) {
        }
        (void) server->owner->tick_liveness (now);
        dispatch_server (*server);
    }
    std::vector<std::shared_ptr<raw_client_server_client_t>> clients;
    {
        std::lock_guard lock (_gate);
        for (auto &[_, channel] : _clients) {
            for (auto &[__, connection] : channel->connections)
                clients.push_back (connection.owner);
        }
    }
    for (auto &client : clients) {
        (void) client->drain_monitor_events (now);
        while (client->pump_one (now)
               != client_server_pump_result_t::no_data) {
        }
        (void) client->tick_liveness (now);
        (void) client->expire_requests (
          foundation::operation_registry_t::clock_t::now ());
    }
    _ready.notify_all ();
}

void client_server_location_runtime_t::dispatch_server (
  server_entry_t &server)
{
    auto &mailbox = server.owner->mailbox ();
    for (;;) {
        auto claim = mailbox.try_claim (
          mesh::service_mailbox_domain_t::application,
          64, 4u * 1024u * 1024u);
        if (!claim)
            return;
        for (const auto &record : claim->records) {
            if (record.parts.size () != 2)
                continue;
            try {
                const auto payload =
                  protocol::decode_application_payload (
                    record.parts[1]);
                const auto message =
                  zlink::message_t::from (payload.payload);
                detail::inbound_message_context_t
                  inbound;
                inbound.message.channel_name = record.owner;
                inbound.message.packet_name =
                  payload.packet_name;
                inbound.message.content_type =
                  payload.content_type;
                if (record.correlation)
                    inbound.message.correlation_id =
                      std::to_string (*record.correlation);
                detail::message_flow_tracer_t flow (
                  _channel_runtime.dispatch_options_ref ());
                flow.trace (message_flow_outcome_t::received, [&] {
                    return message_flow_event_t{
                      message_flow_outcome_t::received,
                      dispatch_error_surface_t::channel,
                      record.request_sequence
                        ? dispatch_message_kind_t::request
                        : dispatch_message_kind_t::send,
                      payload.packet_name,
                      record.owner,
                      std::nullopt,
                      inbound.message.correlation_id,
                      std::nullopt,
                      std::nullopt,
                      std::nullopt,
                      std::nullopt};
                });
                auto scope =
                  zlink::framework::detail::service_scope_t::create (
                    *_services,
                    zlink::framework::detail::service_scope_kind_t::
                      handler_invocation);
                if (record.request_sequence && record.correlation) {
                    auto reply = _channel_runtime.dispatch_request (
                      record.owner, {}, payload.packet_name,
                      scope.provider (), *_serializers, *_handlers,
                      message, inbound);
                    if (reply) {
                        (void) server.owner->reply (
                          record,
                          protocol::application_payload_t{
                            payload.packet_name,
                            std::string (
                              runtime::messaging::envelope_codec_t::
                                default_content_type),
                            reply.value ().to_bytes ()});
                    } else {
                        const framework_exception_t error (
                          reply.error_kind (),
                          reply.error () != nullptr
                            ? reply.error ()->what ()
                            : "ClientServer request handler failed",
                          reply.error () != nullptr
                            && reply.error ()->is_retriable ());
                        const auto failure = wire_failure (error);
                        (void) server.owner->reply (
                          record, failure.terminal_result,
                          failure.failure_code);
                    }
                } else {
                    (void) _channel_runtime.dispatch_send (
                      record.owner, {}, payload.packet_name,
                      scope.provider (), *_serializers, *_handlers,
                      message, inbound);
                }
            }
            catch (const framework_exception_t &error) {
                if (record.request_sequence
                    && record.correlation) {
                    const auto failure = wire_failure (error);
                    (void) server.owner->reply (
                      record, failure.terminal_result,
                      failure.failure_code);
                }
            }
            catch (const std::exception &error) {
                if (record.request_sequence
                    && record.correlation) {
                    const auto failure = wire_failure (
                      framework_exception_t (
                        framework_error_kind_t::request_failed,
                        error.what ()));
                    (void) server.owner->reply (
                      record, failure.terminal_result,
                      failure.failure_code);
                }
            }
            catch (...) {
                if (record.request_sequence
                    && record.correlation) {
                    const auto failure = wire_failure (
                      framework_exception_t (
                        framework_error_kind_t::request_failed,
                        "ClientServer dispatch failed"));
                    (void) server.owner->reply (
                      record, failure.terminal_result,
                      failure.failure_code);
                }
            }
        }
        (void) mailbox.release (*claim);
    }
}

result_t<void> client_server_location_runtime_t::send (
  const std::string &channel_name,
  std::string packet_name,
  std::string content_type,
  zlink::message_t message,
  std::chrono::milliseconds timeout)
{
    const auto effective =
      timeout > std::chrono::milliseconds::zero ()
        ? timeout
        : std::chrono::seconds (1);
    const auto wait = std::min (
      effective,
      std::chrono::duration_cast<std::chrono::milliseconds> (
        maximum_ready_wait));
    auto selected = select_ready (
      channel_name, std::chrono::steady_clock::now () + wait);
    if (!selected)
        return result_t<void>::failure (
          selected.error_kind (),
          selected.error () != nullptr
            ? selected.error ()->what ()
            : "ClientServer has no admitted ready server");
    const auto sent = selected.value ()->send (
      protocol::application_payload_t{
        std::move (packet_name),
        std::move (content_type),
        message.to_bytes ()});
    return sent
             ? result_t<void>::success ()
             : result_t<void>::failure (
                 framework_error_kind_t::route_not_connected,
                 "ClientServer send lost its admitted server");
}

result_t<zlink::message_t>
client_server_location_runtime_t::request (
  const std::string &channel_name,
  std::string packet_name,
  std::string content_type,
  zlink::message_t message,
  std::chrono::milliseconds timeout)
{
    const auto effective =
      timeout > std::chrono::milliseconds::zero ()
        ? timeout
        : std::chrono::seconds (30);
    const auto wait = std::min (
      effective,
      std::chrono::duration_cast<std::chrono::milliseconds> (
        maximum_ready_wait));
    const auto deadline =
      std::chrono::steady_clock::now () + effective;
    auto selected = select_ready (
      channel_name, std::chrono::steady_clock::now () + wait);
    if (!selected)
        return result_t<zlink::message_t>::failure (
          selected.error_kind (),
          selected.error () != nullptr
            ? selected.error ()->what ()
            : "ClientServer has no admitted ready server");
    auto promise = std::make_shared<
      std::promise<result_t<zlink::message_t>>> ();
    auto future = promise->get_future ();
    const auto submitted = selected.value ()->request (
      protocol::application_payload_t{
        std::move (packet_name),
        std::move (content_type),
        message.to_bytes ()},
      effective,
      [promise] (client_server_request_completion_t completion) {
          if (completion.terminal
              != foundation::operation_terminal_t::completed) {
              promise->set_value (
                result_t<zlink::message_t>::failure (
                  framework_error_kind_t::request_failed,
                  "ClientServer request did not complete"));
              return;
          }
          if (completion.reply_header.terminal_result != 0) {
              const auto error =
                runtime::messaging::request_failure_mapper_t{}
                  .reply_header_exception (
                    completion.reply_header.terminal_result,
                    completion.reply_header.failure_code,
                    "ClientServer request");
              promise->set_value (
                zlink::framework::detail::result_access_t::failure<
                  zlink::message_t> (error));
              return;
          }
          try {
              const auto decoded =
                protocol::decode_application_payload (
                  completion.payload);
              promise->set_value (
                result_t<zlink::message_t>::success (
                  zlink::message_t::from (decoded.payload)));
          }
          catch (const std::exception &error) {
              promise->set_value (
                result_t<zlink::message_t>::failure (
                  framework_error_kind_t::request_failed,
                  error.what ()));
          }
      });
    if (!submitted)
        return result_t<zlink::message_t>::failure (
          framework_error_kind_t::route_not_connected,
          "ClientServer request lost its admitted server");
    if (future.wait_until (deadline) != std::future_status::ready)
        return result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_failed,
          "ClientServer request timed out");
    return future.get ();
}

result_t<std::shared_ptr<raw_client_server_client_t>>
client_server_location_runtime_t::select_ready (
  const std::string &channel_name,
  std::chrono::steady_clock::time_point deadline)
{
    std::unique_lock lock (_gate);
    const auto available = [&] {
        const auto found = _clients.find (channel_name);
        if (found == _clients.end ())
            return false;
        return std::any_of (
          found->second->connections.begin (),
          found->second->connections.end (),
          [] (const auto &entry) {
              return entry.second.owner->ready ()
                     && entry.second.descriptor.state
                          == framework_runtime_state_t::serving
                     && entry.second.descriptor.weight > 0;
          });
    };
    if (!_ready.wait_until (lock, deadline, available)) {
        if (_stop.load (std::memory_order_acquire)) {
            return result_t<std::shared_ptr<raw_client_server_client_t>>::failure (
              framework_error_kind_t::runtime_shutdown,
              "ClientServer runtime is stopping");
        }
        const auto found = _clients.find (channel_name);
        if (found == _clients.end ()) {
            return result_t<std::shared_ptr<raw_client_server_client_t>>::failure (
              framework_error_kind_t::request_target_not_found,
              "ClientServer channel is not registered");
        }
        return result_t<std::shared_ptr<raw_client_server_client_t>>::failure (
          framework_error_kind_t::request_target_not_found,
          "ClientServer has no ready target before the bounded wait expired");
    }
    auto &channel = *_clients.at (channel_name);
    std::vector<weighted_candidate_t> candidates;
    for (auto &[key, connection] : channel.connections) {
        if (!connection.owner->ready ()
            || connection.descriptor.state
                 != framework_runtime_state_t::serving
            || connection.descriptor.weight <= 0)
            continue;
        candidates.push_back (
          {key,
           static_cast<std::uint32_t> (
             connection.descriptor.weight)});
    }
    const auto selected = channel.selector.select (candidates);
    if (!selected) {
        return result_t<std::shared_ptr<raw_client_server_client_t>>::failure (
          framework_error_kind_t::request_target_not_found,
          "ClientServer has no selectable target snapshot");
    }
    return result_t<std::shared_ptr<raw_client_server_client_t>>::success (
      channel.connections.at (*selected).owner);
}

void client_server_location_runtime_t::stop () noexcept
{
    const bool was_stopped =
      _stop.exchange (true, std::memory_order_acq_rel);
    _ready.notify_all ();
    for (const auto &[channel_name, _] : _clients)
        _channel_runtime.unbind_client_server_transport (
          channel_name);
    if (_thread.joinable ())
        _thread.join ();
    if (!was_stopped || !_servers.empty () || !_clients.empty ()) {
        stop_clients ();
        stop_servers ();
    }
}

void client_server_location_runtime_t::stop_clients () noexcept
{
    std::vector<std::shared_ptr<raw_client_server_client_t>> clients;
    {
        std::lock_guard lock (_gate);
        for (auto &[_, channel] : _clients) {
            for (auto &[__, connection] : channel->connections)
                clients.push_back (connection.owner);
            channel->connections.clear ();
        }
        _clients.clear ();
    }
    for (auto &client : clients)
        client->close ();
}

void client_server_location_runtime_t::stop_servers () noexcept
{
    for (auto &[_, server] : _servers) {
        if (!server->published_descriptor) {
            server->owner->close ();
            continue;
        }
        try {
            auto admission = server->owner->descriptor ();
            if (admission.state
                != mesh::service_node_state_t::draining) {
                ++admission.descriptor_revision;
                admission.state =
                  mesh::service_node_state_t::draining;
                admission.weight = 0;
                server->owner->update_descriptor (admission);
                auto draining = *server->published_descriptor;
                draining.descriptor_revision =
                  admission.descriptor_revision;
                draining.state =
                  framework_runtime_state_t::draining;
                draining.weight = 0;
                const auto written =
                  _store
                    ->update_client_server (
                      draining,
                      location_write_intent_t::renew)
                    .result ()
                    .value ();
                if (written.status
                    == location_write_status_t::stored)
                    server->published_descriptor = std::move (draining);
            }
            (void) _store
              ->remove_client_server (
                {server->published_descriptor->channel_name,
                 server->published_descriptor->server_rid},
                {server->published_descriptor->owner_id,
                 server->published_descriptor->lease_generation})
              .result ()
              .value ();
        }
        catch (...) {
        }
        server->owner->close ();
    }
    _servers.clear ();
}

std::uint64_t
client_server_location_runtime_t::make_lifecycle_generation ()
{
    static std::atomic_uint64_t counter{1};
    const auto random =
      (static_cast<std::uint64_t> (std::random_device{} ()) << 32u)
      ^ static_cast<std::uint64_t> (std::random_device{} ());
    const auto time = static_cast<std::uint64_t> (
      std::chrono::steady_clock::now ()
        .time_since_epoch ()
        .count ());
    auto value =
      (random ^ time ^ counter.fetch_add (1))
      & static_cast<std::uint64_t> (
          std::numeric_limits<std::int64_t>::max ());
    return value == 0 ? 1 : value;
}

std::uint32_t
client_server_location_runtime_t::effective_max_message_bytes (
  const channel_capability_snapshot_t &capability)
{
    if (!capability.max_message_size
        || capability.max_message_size->bytes () <= 0)
        return default_effective_max_message_bytes;
    return static_cast<std::uint32_t> (
      std::min<std::int64_t> (
        capability.max_message_size->bytes (),
        std::numeric_limits<std::uint32_t>::max ()));
}

std::vector<std::uint8_t>
client_server_location_runtime_t::client_routing_id (
  const channel_snapshot_t &channel)
{
    if (channel.client.routing_id)
        return channel.client.routing_id->to_bytes ();
    return zlink::routing_id_t::from (
             channel.name + ":client:"
             + std::to_string (make_lifecycle_generation ()))
      .to_bytes ();
}

std::vector<std::uint8_t>
client_server_location_runtime_t::server_routing_id (
  const channel_snapshot_t &channel)
{
    if (channel.server.routing_id)
        return channel.server.routing_id->to_bytes ();
    return zlink::routing_id_t::from (
             channel.name + ":server:"
             + std::to_string (make_lifecycle_generation ()))
      .to_bytes ();
}

protocol::client_server_server_admission_t
client_server_location_runtime_t::to_admission (
  const client_server_server_descriptor_t &descriptor,
  std::uint32_t effective_max_message_bytes)
{
    protocol::client_server_server_admission_t admission;
    admission.channel_name = descriptor.channel_name;
    admission.server_routing_id =
      descriptor.server_rid.to_bytes ();
    admission.lifecycle_generation =
      descriptor.lifecycle_generation;
    admission.descriptor_revision =
      descriptor.descriptor_revision;
    admission.weight = static_cast<std::uint32_t> (
      descriptor.weight);
    admission.state =
      client_server_service_state (descriptor.state);
    admission.security_identity =
      descriptor.security_identity;
    admission.effective_max_message_bytes =
      effective_max_message_bytes;
    admission.advertised_endpoint = descriptor.endpoint;
    return admission;
}

client_server_server_descriptor_t
client_server_location_runtime_t::to_descriptor (
  const protocol::client_server_server_admission_t &admission,
  const location_owner_token_t &owner)
{
    return client_server_server_descriptor_t{
      .channel_name = admission.channel_name,
      .server_rid =
        zlink::routing_id_t::from (
          admission.server_routing_id),
      .lifecycle_generation =
        admission.lifecycle_generation,
      .descriptor_revision =
        admission.descriptor_revision,
      .endpoint = admission.advertised_endpoint,
      .weight = static_cast<int> (admission.weight),
      .state = client_server_framework_state (admission.state),
      .security_identity = admission.security_identity,
      .owner_id = owner.owner_id,
      .lease_generation = owner.lease_generation};
}

bool client_server_location_runtime_t::owner_is_live (
  const client_server_server_descriptor_t &descriptor) const
{
    const auto lease =
      _leases->read_owner_lease (descriptor.owner_id)
        .result ()
        .value ();
    const auto *found =
      std::get_if<owner_lease_found_t> (&lease);
    return found != nullptr
           && found->token.owner_id == descriptor.owner_id
           && found->token.lease_generation
                == descriptor.lease_generation
           && found->lease_expires_at > found->store_now;
}

} // namespace zlink::framework::runtime::client_server
