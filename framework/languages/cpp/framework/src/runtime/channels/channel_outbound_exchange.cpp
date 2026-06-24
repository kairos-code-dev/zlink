/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/channel_outbound_exchange.hpp"

#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/channels/channel_socket_options.hpp"
#include "runtime/diagnostics/message_flow_tracer.hpp"
#include "runtime/messaging/client_call_codec.hpp"
#include "runtime/messaging/envelope_codec.hpp"
#include "runtime/messaging/request_failure_mapper.hpp"
#include "runtime/registry/discovery_registry_connection.hpp"

#include <zlink.hpp>

#include <algorithm>
#include <cerrno>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace zlink::framework::detail
{

namespace
{

const channel_capability_snapshot_t *client_capability (const channel_runtime_state_t &state,
                                                        const std::string &channel_name)
{
    const auto found = state.channels.find (channel_name);
    if (found == state.channels.end ()) {
        return nullptr;
    }
    return &found->second.client;
}

const channel_capability_snapshot_t *publisher_capability (const channel_runtime_state_t &state,
                                                           const std::string &channel_name)
{
    const auto found = state.channels.find (channel_name);
    if (found == state.channels.end ()) {
        return nullptr;
    }
    return &found->second.publisher;
}

bool has_connection (const channel_capability_snapshot_t *capability)
{
    return capability != nullptr && capability->enabled
           && (capability->discovery || !capability->bind_endpoints.empty ()
               || !capability->connect_endpoints.empty ());
}

framework_exception_t map_native_request_exception (const std::exception &error)
{
    if (const auto *request_error = dynamic_cast<const zlink::request_error_t *> (&error);
        request_error != nullptr) {
        if (request_error->result () == zlink::request_result_t::timed_out) {
            return framework_exception_t (framework_error_kind_t::timeout,
                                          "channel request timed out");
        }
        return framework_exception_t (framework_error_kind_t::request_failed,
                                      request_error->what ());
    }
    if (const auto *recv_error = dynamic_cast<const zlink::recv_error_t *> (&error);
        recv_error != nullptr) {
        if (recv_error->result () == zlink::recv_result_t::no_data
            || recv_error->internal_errno () == EAGAIN) {
            return framework_exception_t (framework_error_kind_t::timeout,
                                          "channel request timed out");
        }
        return framework_exception_t (framework_error_kind_t::request_failed, recv_error->what ());
    }
    if (const auto *submit_error = dynamic_cast<const zlink::submit_error_t *> (&error);
        submit_error != nullptr) {
        if (submit_error->result () == zlink::submit_result_t::backpressured
            && submit_error->internal_errno () == EAGAIN) {
            return framework_exception_t (framework_error_kind_t::timeout,
                                          "channel request timed out");
        }
        return framework_exception_t (framework_error_kind_t::request_failed,
                                      submit_error->what ());
    }
    return framework_exception_t (framework_error_kind_t::request_failed, error.what ());
}

} // namespace

class channel_native_publisher_t
{
  public:
    explicit channel_native_publisher_t (const channel_capability_snapshot_t &publisher) :
        _socket (_context)
    {
        apply_common_channel_socket_options (_socket, publisher);
        for (const auto &endpoint : publisher.bind_endpoints) {
            _socket.bind (endpoint);
        }
        for (const auto &endpoint : publisher.connect_endpoints) {
            _socket.connect (endpoint);
        }
    }

    result_t<void> publish (const std::string &topic,
                            const runtime::messaging::message_parts_t &parts)
    {
        std::lock_guard lock (_mutex);
        drain_subscription_events ();
        zlink::message_t publish_header = zlink::message_t::from (parts[0].to_string ());
        zlink::message_t publish_body = zlink::message_t::from (parts[1].to_string ());
        const bool sent =
          _socket.publish (topic).message (publish_header).message (publish_body).submit ();
        if (!sent) {
            return result_t<void>::failure (framework_error_kind_t::request_failed,
                                            "channel native publish failed");
        }
        return result_t<void>::success ();
    }

  private:
    void drain_subscription_events ()
    {
        const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (200);
        while (std::chrono::steady_clock::now () < deadline) {
            zlink::subscription_event_t subscription;
            (void) _socket.receive_subscription_event (subscription, zlink::recv_flags_t::dontwait);
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
    }

    zlink::context_t _context;
    zlink::xpub_socket_t _socket;
    std::mutex _mutex;
};

channel_outbound_exchange_t::channel_outbound_exchange_t (
  std::shared_ptr<channel_runtime_state_t> state) :
    _state (std::move (state))
{
}

message_bus_t::erased_request_result_t
channel_outbound_exchange_t::submit_request (std::string channel_name,
                                             std::string packet_name,
                                             std::type_index request_type,
                                             const void *request,
                                             std::chrono::milliseconds timeout,
                                             const channel_request_call_t::metadata_map_t &metadata)
{
    channel_runtime_t runtime (_state);
    const auto *client = client_capability (*_state, channel_name);
    const auto call_packet_name = std::move (packet_name);
    {
        std::lock_guard lock (_state->mutex);
        _state->outbound_calls.push_back (
          {"request", channel_name, "", call_packet_name, timeout, metadata});
    }
    auto reservation = runtime.reserve_outbound_request (channel_name);
    if (!reservation) {
        return message_bus_t::erased_request_result_t (framework_exception_t (
          reservation.error_kind (),
          reservation.error () ? reservation.error ()->what () : "channel request failed"));
    }
    const bool client_uses_discovery =
      client != nullptr && client->discovery && !_state->discovery.registry_endpoints.empty ();
    if (_state->serializers != nullptr && client != nullptr) {
        try {
            runtime::messaging::client_call_codec_t codec;
            auto header = codec.create_envelope (runtime::messaging::message_kind_t::request,
                                                 channel_name, call_packet_name, timeout);
            header.metadata = metadata;
            detail::message_flow_tracer_t (_state->dispatch)
              .trace (message_flow_phase_t::sent, [&] {
                  return message_flow_event_t{message_flow_phase_t::sent,
                                              dispatch_error_surface_t::channel,
                                              dispatch_message_kind_t::request,
                                              call_packet_name,
                                              channel_name,
                                              std::nullopt,
                                              header.correlation_id,
                                              std::nullopt,
                                              std::nullopt,
                                              std::nullopt,
                                              std::nullopt};
              });
            runtime::messaging::envelope_codec_t envelope;
            auto parts =
              envelope.encode_parts (header, request_type, request, *_state->serializers);

            detail::channel_runtime_manager_t manager (_state);
            std::vector<std::string> endpoints;
            {
                std::lock_guard lock (_state->mutex);
                auto &bundle = manager.get_or_create_client_bundle (channel_name);
                endpoints = bundle.manual_connections_from_next ();
            }
            if (endpoints.empty ()) {
                if (!client_uses_discovery) {
                    (void) runtime.cancel_outbound_request (reservation.value ());
                    return message_bus_t::erased_request_result_t (framework_exception_t (
                      framework_error_kind_t::disconnected, "channel client has no endpoint"));
                }
            }

            std::optional<framework_exception_t> last_attempt_error;
            runtime::messaging::message_parts_t reply_parts;
            bool received_reply = false;
            const auto deadline = timeout > std::chrono::milliseconds::zero ()
                                    ? std::chrono::steady_clock::now () + timeout
                                    : std::chrono::steady_clock::time_point::max ();
            auto remaining_timeout = [&] {
                if (timeout <= std::chrono::milliseconds::zero ()) {
                    return timeout;
                }
                const auto now = std::chrono::steady_clock::now ();
                if (now >= deadline) {
                    return std::chrono::milliseconds::zero ();
                }
                return std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now);
            };
            const std::size_t attempt_count =
              client_uses_discovery && endpoints.empty () ? 1 : endpoints.size ();
            for (std::size_t attempt = 0; attempt < attempt_count; ++attempt) {
                try {
                    const auto connect_timeout = remaining_timeout ();
                    if (timeout > std::chrono::milliseconds::zero ()
                        && connect_timeout <= std::chrono::milliseconds::zero ()) {
                        throw framework_exception_t (framework_error_kind_t::timeout,
                                                     "channel request timed out");
                    }
                    zlink::message_t request_header =
                      zlink::message_t::from (parts[0].to_string ());
                    zlink::message_t request_body = zlink::message_t::from (parts[1].to_string ());
                    zlink::context_t context;
                    zlink::dealer_socket_t dealer (context);
                    apply_weighted_channel_socket_options (dealer, *client);
                    dealer.channel_name (channel_name);
                    dealer.options ().immediate (true);
                    dealer.options ().connect_timeout (connect_timeout);
                    dealer.options ().request_timeout (connect_timeout);
                    std::unique_ptr<zlink::service::discovery_t> discovery;
                    if (client_uses_discovery && endpoints.empty ()) {
                        discovery = std::make_unique<zlink::service::discovery_t> (
                          context, zlink::auto_connect_type::client_server, channel_name);
                        for (const auto &registry_endpoint : _state->discovery.registry_endpoints) {
                            connect_registry_with_retry (*discovery, registry_endpoint);
                        }
                        dealer.attach_discovery (*discovery);
                    } else {
                        dealer.connect (endpoints[attempt]);
                    }
                    if (timeout > std::chrono::milliseconds::zero ()) {
                        const auto wait_for_connect =
                          std::min (std::chrono::milliseconds (200), remaining_timeout ());
                        if (wait_for_connect > std::chrono::milliseconds::zero ()) {
                            std::this_thread::sleep_for (wait_for_connect);
                        }
                    } else {
                        std::this_thread::sleep_for (std::chrono::milliseconds (200));
                    }
                    const auto request_timeout = remaining_timeout ();
                    if (timeout > std::chrono::milliseconds::zero ()
                        && request_timeout <= std::chrono::milliseconds::zero ()) {
                        throw framework_exception_t (framework_error_kind_t::timeout,
                                                     "channel request timed out");
                    }
                    auto pending_reply = dealer.request ()
                                           .message (request_header)
                                           .message (request_body)
                                           .timeout (request_timeout)
                                           .async ();
                    auto native_reply = pending_reply.get ();
                    if (timeout > std::chrono::milliseconds::zero ()
                        && std::chrono::steady_clock::now () >= deadline) {
                        throw framework_exception_t (framework_error_kind_t::timeout,
                                                     "channel request timed out");
                    }
                    if (native_reply.size () != 2) {
                        throw framework_exception_t (
                          framework_error_kind_t::request_protocol_error,
                          "channel native request returned an invalid reply frame count");
                    }
                    auto candidate_reply_parts =
                      runtime::messaging::message_parts_t (std::move (native_reply));
                    auto validation = validate_channel_native_reply (candidate_reply_parts);
                    if (!validation) {
                        throw framework_exception_t (validation.error_kind (),
                                                     validation.error ()
                                                       ? validation.error ()->what ()
                                                       : "channel reply decode failed");
                    }
                    reply_parts = std::move (candidate_reply_parts);
                    received_reply = true;
                    break;
                }
                catch (const framework_exception_t &error) {
                    last_attempt_error = error;
                }
                catch (const std::exception &error) {
                    last_attempt_error = map_native_request_exception (error);
                }
                catch (...) {
                    last_attempt_error = framework_exception_t (
                      framework_error_kind_t::request_failed, "channel native request failed");
                }
            }
            if (!received_reply) {
                (void) runtime.cancel_outbound_request (reservation.value ());
                if (last_attempt_error) {
                    return message_bus_t::erased_request_result_t (*last_attempt_error);
                }
                return message_bus_t::erased_request_result_t (framework_exception_t (
                  framework_error_kind_t::request_failed, "channel native request failed"));
            }
            auto completion = runtime.complete_outbound_reply (reservation.value ());
            if (!completion) {
                return message_bus_t::erased_request_result_t (framework_exception_t (
                  completion.error_kind (),
                  completion.error () ? completion.error ()->what () : "channel request failed"));
            }

            auto reply_header = envelope.decode_header (reply_parts);
            if (!reply_header) {
                return message_bus_t::erased_request_result_t (framework_exception_t (
                  reply_header.error_kind (), reply_header.error ()
                                                ? reply_header.error ()->what ()
                                                : "channel reply header decode failed"));
            }
            if (reply_header.value ().kind == runtime::messaging::message_kind_t::error) {
                runtime::messaging::request_failure_mapper_t failure_mapper;
                return message_bus_t::erased_request_result_t (
                  failure_mapper.error_header_exception (
                    reply_header.value ().error_code.value_or ("request_failed"),
                    reply_header.value ().error_message.value_or ("channel request failed"),
                    "channel request"));
            }
            auto body = envelope.decode_body (reply_parts);
            if (!body) {
                return message_bus_t::erased_request_result_t (framework_exception_t (
                  body.error_kind (),
                  body.error () ? body.error ()->what () : "channel reply body decode failed"));
            }
            detail::message_flow_tracer_t (_state->dispatch)
              .trace (message_flow_phase_t::reply_received, [&] {
                  return message_flow_event_t{message_flow_phase_t::reply_received,
                                              dispatch_error_surface_t::channel,
                                              dispatch_message_kind_t::response,
                                              call_packet_name,
                                              channel_name,
                                              std::nullopt,
                                              header.correlation_id,
                                              std::nullopt,
                                              std::nullopt,
                                              std::nullopt,
                                              std::nullopt};
              });
            return message_bus_t::erased_request_result_t (body.value (), *_state->serializers);
        }
        catch (const framework_exception_t &error) {
            (void) runtime.cancel_outbound_request (reservation.value ());
            return message_bus_t::erased_request_result_t (error);
        }
        catch (const std::exception &error) {
            (void) runtime.cancel_outbound_request (reservation.value ());
            return message_bus_t::erased_request_result_t (map_native_request_exception (error));
        }
        catch (...) {
            (void) runtime.cancel_outbound_request (reservation.value ());
            return message_bus_t::erased_request_result_t (framework_exception_t (
              framework_error_kind_t::request_failed, "channel native request failed"));
        }
    }
    (void) runtime.cancel_outbound_request (reservation.value ());
    return message_bus_t::erased_request_result_t (framework_exception_t (
      framework_error_kind_t::timeout, "channel request reply was not completed by a backend"));
}

result_t<void>
channel_outbound_exchange_t::submit_send (std::string channel_name,
                                          std::string packet_name,
                                          std::type_index message_type,
                                          const void *message,
                                          std::chrono::milliseconds timeout,
                                          const send_call_t::metadata_map_t &metadata)
{
    const auto call_packet_name = std::move (packet_name);
    {
        std::lock_guard lock (_state->mutex);
        _state->outbound_calls.push_back (
          {"send", channel_name, "", call_packet_name, timeout, metadata});
    }
    const auto *client = client_capability (*_state, channel_name);
    if (!has_connection (client)) {
        return result_t<void>::failure (framework_error_kind_t::disconnected,
                                        "channel client is not connected");
    }
    const bool client_uses_discovery =
      client != nullptr && client->discovery && !_state->discovery.registry_endpoints.empty ();
    const bool client_has_manual_endpoint =
      client != nullptr
      && (!client->connect_endpoints.empty () || !client->bind_endpoints.empty ());
    if (_state->serializers != nullptr && client != nullptr
        && (client_uses_discovery || client_has_manual_endpoint)) {
        channel_runtime_t runtime (_state);
        const auto pending_send = runtime.queue_pending_send (channel_name);
        if (!pending_send) {
            return result_t<void>::failure (
              pending_send.error_kind (),
              pending_send.error () ? pending_send.error ()->what () : "channel send was rejected");
        }
        const auto fail_pending_send = [&runtime, operation_id = pending_send.value ()] {
            (void) runtime.retry_pending (operation_id);
            (void) runtime.expire_pending (operation_id);
        };
        try {
            runtime::messaging::client_call_codec_t codec;
            auto header = codec.create_envelope (runtime::messaging::message_kind_t::command,
                                                 channel_name, call_packet_name, timeout);
            header.metadata = metadata;
            detail::message_flow_tracer_t (_state->dispatch)
              .trace (message_flow_phase_t::sent, [&] {
                  return message_flow_event_t{message_flow_phase_t::sent,
                                              dispatch_error_surface_t::channel,
                                              dispatch_message_kind_t::send,
                                              call_packet_name,
                                              channel_name,
                                              std::nullopt,
                                              header.correlation_id,
                                              std::nullopt,
                                              std::nullopt,
                                              std::nullopt,
                                              std::nullopt};
              });
            runtime::messaging::envelope_codec_t envelope;
            auto parts =
              envelope.encode_parts (header, message_type, message, *_state->serializers);

            zlink::context_t context;
            zlink::dealer_socket_t dealer (context);
            apply_weighted_channel_socket_options (dealer, *client);
            dealer.channel_name (channel_name);
            dealer.options ().immediate (true);
            dealer.options ().connect_timeout (timeout);
            detail::channel_runtime_manager_t manager (_state);
            std::vector<std::string> endpoints;
            {
                std::lock_guard lock (_state->mutex);
                auto &bundle = manager.get_or_create_client_bundle (channel_name);
                endpoints = bundle.manual_connections_from_next ();
            }
            if (endpoints.empty ()) {
                if (!client_uses_discovery) {
                    fail_pending_send ();
                    return result_t<void>::failure (framework_error_kind_t::disconnected,
                                                    "channel client has no endpoint");
                }
            }
            std::unique_ptr<zlink::service::discovery_t> discovery;
            if (client_uses_discovery && endpoints.empty ()) {
                discovery = std::make_unique<zlink::service::discovery_t> (
                  context, zlink::auto_connect_type::client_server, channel_name);
                for (const auto &registry_endpoint : _state->discovery.registry_endpoints) {
                    connect_registry_with_retry (*discovery, registry_endpoint);
                }
                dealer.attach_discovery (*discovery);
            } else {
                for (const auto &endpoint : endpoints) {
                    dealer.connect (endpoint);
                }
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (200));

            zlink::message_t send_header = zlink::message_t::from (parts[0].to_string ());
            zlink::message_t send_body = zlink::message_t::from (parts[1].to_string ());
            const bool sent = dealer.send ().message (send_header).message (send_body).submit ();
            if (!sent) {
                fail_pending_send ();
                return result_t<void>::failure (framework_error_kind_t::request_failed,
                                                "channel native send failed");
            }
            auto completed = runtime.mark_send_ready (pending_send.value ());
            if (!completed) {
                return completed;
            }
            return result_t<void>::success ();
        }
        catch (const framework_exception_t &error) {
            fail_pending_send ();
            return result_t<void>::failure (error.kind (), error.what ());
        }
        catch (const std::exception &error) {
            fail_pending_send ();
            return result_t<void>::failure (framework_error_kind_t::request_failed, error.what ());
        }
        catch (...) {
            fail_pending_send ();
            return result_t<void>::failure (framework_error_kind_t::request_failed,
                                            "channel native send failed");
        }
    }
    return result_t<void>::success ();
}

result_t<void>
channel_outbound_exchange_t::submit_publish (std::string channel_name,
                                             std::string topic,
                                             std::string packet_name,
                                             std::type_index event_type,
                                             const void *event,
                                             std::chrono::milliseconds timeout,
                                             const send_call_t::metadata_map_t &metadata)
{
    const auto call_packet_name = std::move (packet_name);
    {
        std::lock_guard lock (_state->mutex);
        _state->outbound_calls.push_back (
          {"publish", channel_name, topic, call_packet_name, timeout, metadata});
    }
    const auto *publisher = publisher_capability (*_state, channel_name);
    if (!has_connection (publisher)) {
        return result_t<void>::failure (framework_error_kind_t::disconnected,
                                        "channel publisher is not connected");
    }
    if (_state->serializers != nullptr && publisher != nullptr
        && (!publisher->bind_endpoints.empty () || !publisher->connect_endpoints.empty ())) {
        try {
            runtime::messaging::client_call_codec_t codec;
            auto header = codec.create_envelope (runtime::messaging::message_kind_t::publish,
                                                 channel_name, call_packet_name, timeout, topic);
            header.metadata = metadata;
            detail::message_flow_tracer_t (_state->dispatch)
              .trace (message_flow_phase_t::sent, [&] {
                  return message_flow_event_t{message_flow_phase_t::sent,
                                              dispatch_error_surface_t::channel,
                                              dispatch_message_kind_t::publish,
                                              call_packet_name,
                                              channel_name,
                                              topic,
                                              header.correlation_id,
                                              std::nullopt,
                                              std::nullopt,
                                              std::nullopt,
                                              std::nullopt};
              });
            runtime::messaging::envelope_codec_t envelope;
            auto parts = envelope.encode_parts (header, event_type, event, *_state->serializers);
            std::shared_ptr<detail::channel_native_publisher_t> native_publisher;
            {
                std::lock_guard lock (_state->mutex);
                auto &stored = _state->native_publishers[channel_name];
                if (!stored) {
                    stored = std::make_shared<detail::channel_native_publisher_t> (*publisher);
                }
                native_publisher = stored;
            }
            auto published = native_publisher->publish (topic, parts);
            if (!published) {
                return published;
            }
        }
        catch (const framework_exception_t &error) {
            return result_t<void>::failure (error.kind (), error.what ());
        }
        catch (const std::exception &error) {
            return result_t<void>::failure (framework_error_kind_t::request_failed, error.what ());
        }
        catch (...) {
            return result_t<void>::failure (framework_error_kind_t::request_failed,
                                            "channel native publish failed");
        }
    }
    return result_t<void>::success ();
}

} // namespace zlink::framework::detail
