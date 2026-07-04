/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/channel_outbound_exchange.hpp"

#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/channels/channel_socket_options.hpp"
#include "runtime/diagnostics/message_flow_tracer.hpp"
#include "runtime/messaging/client_call_codec.hpp"
#include "runtime/messaging/envelope_codec.hpp"
#include "runtime/messaging/request_failure_mapper.hpp"

#include <zlink.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <utility>

namespace zlink::framework::detail
{

namespace
{

constexpr auto channel_submit_retry_interval = std::chrono::milliseconds (50);

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

bool can_wait_for_client_endpoint (const std::shared_ptr<channel_runtime_state_t> &state,
                                   const channel_capability_snapshot_t *capability)
{
    if (capability == nullptr || !capability->enabled) {
        return false;
    }
    if (!capability->bind_endpoints.empty () || !capability->connect_endpoints.empty ()) {
        return true;
    }
    if (!capability->discovery) {
        return false;
    }
    std::lock_guard lock (state->mutex);
    return state->auto_connect_active;
}

runtime::messaging::message_parts_t
encode_channel_payload_parts (runtime::messaging::envelope_header_t header,
                              std::type_index payload_type,
                              const message_bus_t::payload_encoder_t &encode_payload,
                              serializer_registry_t &serializers)
{
    header.content_type = serializers.content_type (payload_type);
    runtime::messaging::envelope_codec_t envelope;
    return envelope.encode_raw_body_parts (
      header, detail::encoded_payload_to_raw (encode_payload (serializers)));
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

std::chrono::steady_clock::time_point
submit_deadline (std::chrono::milliseconds timeout)
{
    return timeout > std::chrono::milliseconds::zero ()
             ? std::chrono::steady_clock::now () + timeout
             : std::chrono::steady_clock::time_point::max ();
}

std::chrono::milliseconds
remaining_submit_timeout (std::chrono::steady_clock::time_point deadline)
{
    if (deadline == std::chrono::steady_clock::time_point::max ()) {
        return std::chrono::milliseconds::zero ();
    }
    const auto now = std::chrono::steady_clock::now ();
    if (now >= deadline) {
        return std::chrono::milliseconds::zero ();
    }
    return std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now);
}

bool submit_deadline_expired (std::chrono::steady_clock::time_point deadline)
{
    return deadline != std::chrono::steady_clock::time_point::max ()
           && std::chrono::steady_clock::now () >= deadline;
}

bool is_retriable_channel_submit_errno (int error) noexcept
{
    return error == EHOSTUNREACH || error == ENETUNREACH || error == ECONNREFUSED
           || error == ENOTCONN || error == EAGAIN;
}

void sleep_until_next_submit_retry (std::chrono::steady_clock::time_point deadline)
{
    if (deadline == std::chrono::steady_clock::time_point::max ()) {
        std::this_thread::sleep_for (channel_submit_retry_interval);
        return;
    }
    const auto remaining = remaining_submit_timeout (deadline);
    if (remaining > std::chrono::milliseconds::zero ()) {
        std::this_thread::sleep_for (std::min (channel_submit_retry_interval, remaining));
    }
}

std::chrono::milliseconds
resolve_channel_wait_timeout (const std::shared_ptr<channel_runtime_state_t> &state,
                              const std::string &channel_name,
                              std::chrono::milliseconds timeout)
{
    if (timeout > std::chrono::milliseconds::zero ()) {
        return timeout;
    }
    std::lock_guard lock (state->mutex);
    const auto found = state->channels.find (channel_name);
    if (found != state->channels.end () && found->second.default_request_timeout) {
        return *found->second.default_request_timeout;
    }
    return state->default_request_timeout;
}

std::function<std::vector<std::string> ()>
make_client_endpoint_provider (std::shared_ptr<channel_runtime_state_t> state,
                               std::string channel_name)
{
    return [state = std::move (state), channel_name = std::move (channel_name)] {
        std::lock_guard lock (state->mutex);
        detail::channel_runtime_manager_t manager (state);
        auto &bundle = manager.get_or_create_client_bundle (channel_name);
        return bundle.connections_from_next ();
    };
}

} // namespace

class channel_native_client_t
{
  public:
    using endpoint_provider_t = std::function<std::vector<std::string> ()>;

    channel_native_client_t (std::string channel_name,
                             const channel_capability_snapshot_t &client) :
        _channel_name (std::move (channel_name)), _client (client)
    {
        initialize_transport ();
    }

    result_t<runtime::messaging::message_parts_t>
    request (const runtime::messaging::message_parts_t &parts,
             const endpoint_provider_t &endpoints,
             std::chrono::milliseconds timeout)
    {
        if (_closed.load (std::memory_order_acquire)) {
            return result_t<runtime::messaging::message_parts_t>::failure (
              framework_error_kind_t::shutdown, "channel native client is closed");
        }
        std::lock_guard lock (_mutex);
        const auto deadline = submit_deadline (timeout);

        for (;;) {
            try {
                if (_closed.load (std::memory_order_acquire)) {
                    return result_t<runtime::messaging::message_parts_t>::failure (
                      framework_error_kind_t::shutdown, "channel native client is closed");
                }
                if (submit_deadline_expired (deadline)) {
                    return result_t<runtime::messaging::message_parts_t>::failure (
                      framework_error_kind_t::timeout, "channel request timed out");
                }
                const auto current_endpoints = endpoints ();
                if (current_endpoints.empty ()) {
                    sleep_until_next_submit_retry (deadline);
                    continue;
                }
                sync_connections (current_endpoints);
                const auto request_timeout = remaining_submit_timeout (deadline);
                zlink::message_t request_header = parts[0];
                zlink::message_t request_body = parts[1];
                auto native_request =
                  std::make_unique<zlink::async_result_t<std::vector<zlink::message_t>>> (
                    _socket->request ()
                      .message (request_header)
                      .message (request_body)
                      .timeout (request_timeout)
                      .async ());
                while (native_request->wait_for (std::min (channel_submit_retry_interval,
                                                           remaining_submit_timeout (deadline)))
                       != std::future_status::ready) {
                    if (_closed.load (std::memory_order_acquire)) {
                        abandon_pending_request (native_request);
                        return result_t<runtime::messaging::message_parts_t>::failure (
                          framework_error_kind_t::shutdown, "channel native client is closed");
                    }
                    if (submit_deadline_expired (deadline)) {
                        abandon_pending_request (native_request);
                        return result_t<runtime::messaging::message_parts_t>::failure (
                          framework_error_kind_t::timeout, "channel request timed out");
                    }
                }
                auto native_reply = native_request->get ();
                return result_t<runtime::messaging::message_parts_t>::success (
                  runtime::messaging::message_parts_t (std::move (native_reply)));
            }
            catch (const zlink::submit_error_t &error) {
                if (is_retriable_channel_submit_errno (error.internal_errno ())) {
                    if (submit_deadline_expired (deadline)) {
                        return result_t<runtime::messaging::message_parts_t>::failure (
                          framework_error_kind_t::timeout, "channel request timed out");
                    }
                    sleep_until_next_submit_retry (deadline);
                    continue;
                }
                const auto mapped = map_native_request_exception (error);
                return result_t<runtime::messaging::message_parts_t>::failure (
                  mapped.kind (), mapped.what (), mapped.is_retriable ());
            }
            catch (const std::exception &error) {
                const auto mapped = map_native_request_exception (error);
                return result_t<runtime::messaging::message_parts_t>::failure (
                  mapped.kind (), mapped.what (), mapped.is_retriable ());
            }
            catch (...) {
                return result_t<runtime::messaging::message_parts_t>::failure (
                  framework_error_kind_t::request_failed, "channel native request failed");
            }
        }
    }

    result_t<void> send (const runtime::messaging::message_parts_t &parts,
                         const endpoint_provider_t &endpoints,
                         std::chrono::milliseconds timeout)
    {
        if (_closed.load (std::memory_order_acquire)) {
            return result_t<void>::failure (framework_error_kind_t::shutdown,
                                            "channel native client is closed");
        }
        std::lock_guard lock (_mutex);
        const auto deadline = submit_deadline (timeout);
        for (;;) {
            if (_closed.load (std::memory_order_acquire)) {
                return result_t<void>::failure (framework_error_kind_t::shutdown,
                                                "channel native client is closed");
            }
            if (submit_deadline_expired (deadline)) {
                return result_t<void>::failure (framework_error_kind_t::timeout,
                                                "channel send timed out");
            }
            const auto current_endpoints = endpoints ();
            if (current_endpoints.empty ()) {
                sleep_until_next_submit_retry (deadline);
                continue;
            }
            sync_connections (current_endpoints);
            try {
                zlink::message_t send_header = parts[0];
                zlink::message_t send_body = parts[1];
                const bool sent =
                  _socket->send ().message (send_header).message (send_body).submit ();
                if (sent) {
                    return result_t<void>::success ();
                }
            }
            catch (const zlink::submit_error_t &error) {
                if (is_retriable_channel_submit_errno (error.internal_errno ())) {
                    if (submit_deadline_expired (deadline)) {
                        return result_t<void>::failure (framework_error_kind_t::timeout,
                                                        "channel send timed out");
                    }
                } else {
                    const auto mapped = map_native_request_exception (error);
                    return result_t<void>::failure (mapped.kind (), mapped.what (),
                                                   mapped.is_retriable ());
                }
            }
            catch (const std::exception &error) {
                return result_t<void>::failure (framework_error_kind_t::request_failed,
                                                error.what ());
            }
            catch (...) {
                return result_t<void>::failure (framework_error_kind_t::request_failed,
                                                "channel native send failed");
            }
            sleep_until_next_submit_retry (deadline);
        }
    }

    void close () noexcept
    {
        _closed.store (true, std::memory_order_release);
            try {
                if (_context) {
                    _context->shutdown ();
                }
            }
            catch (...) {
            }
    }

  private:
    void sync_connections (const std::vector<std::string> &endpoints)
    {
        if (!_socket) {
            initialize_transport ();
        }
        std::set<std::string> desired;
        for (const auto &endpoint : endpoints) {
            if (!endpoint.empty ()) {
                desired.insert (endpoint);
            }
        }
        for (auto it = _connected.begin (); it != _connected.end ();) {
            if (desired.find (*it) != desired.end ()) {
                ++it;
                continue;
            }
            try {
                _socket->disconnect (*it);
            }
            catch (...) {
            }
            it = _connected.erase (it);
        }
        for (const auto &endpoint : desired) {
            if (_connected.insert (endpoint).second) {
                _socket->connect (endpoint);
            }
        }
    }

    void initialize_transport ()
    {
        _context = std::make_unique<zlink::context_t> ();
        _socket = std::make_unique<zlink::dealer_socket_t> (*_context);
        apply_weighted_channel_socket_options (*_socket, _client);
        if (_client.routing_id) {
            _socket->set_routing_id (*_client.routing_id);
        }
        _socket->channel_name (_channel_name);
        _socket->options ().immediate (true);
        _connected.clear ();
    }

    void abandon_pending_request (
      std::unique_ptr<zlink::async_result_t<std::vector<zlink::message_t>>> &native_request)
    {
        if (_socket) {
            try {
                _socket->options ().linger (std::chrono::milliseconds (0));
            }
            catch (...) {
            }
        }
        if (_context) {
            try {
                _context->options ().blocky (false);
            }
            catch (...) {
            }
            try {
                _context->shutdown ();
            }
            catch (...) {
            }
        }
        if (native_request
            && native_request->wait_for (std::chrono::milliseconds (100))
                 == std::future_status::ready) {
            try {
                (void) native_request->get ();
            }
            catch (...) {
            }
            native_request.reset ();
            _socket.reset ();
            _context.reset ();
        } else {
            (void) _socket.release ();
            (void) _context.release ();
            (void) native_request.release ();
        }
        initialize_transport ();
    }

    std::string _channel_name;
    channel_capability_snapshot_t _client;
    std::unique_ptr<zlink::context_t> _context;
    std::unique_ptr<zlink::dealer_socket_t> _socket;
    std::mutex _mutex;
    std::set<std::string> _connected;
    std::atomic_bool _closed{false};
};

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
        if (_closed.load (std::memory_order_acquire)) {
            return result_t<void>::failure (framework_error_kind_t::shutdown,
                                            "channel native publisher is closed");
        }
        std::lock_guard lock (_mutex);
        drain_subscription_events ();
        zlink::message_t publish_header = parts[0];
        zlink::message_t publish_body = parts[1];
        const bool sent =
          _socket.publish (topic).message (publish_header).message (publish_body).submit ();
        if (!sent) {
            return result_t<void>::failure (framework_error_kind_t::request_failed,
                                            "channel native publish failed");
        }
        return result_t<void>::success ();
    }

    void close () noexcept
    {
        _closed.store (true, std::memory_order_release);
        try {
            _context.shutdown ();
        }
        catch (...) {
        }
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
    std::atomic_bool _closed{false};
};

void close_native_channel_transports (
  const std::shared_ptr<channel_runtime_state_t> &state) noexcept
{
    std::vector<std::shared_ptr<channel_native_client_t>> clients;
    std::vector<std::shared_ptr<channel_native_publisher_t>> publishers;
    {
        std::lock_guard lock (state->mutex);
        for (auto &[_, client] : state->native_clients) {
            if (client) {
                clients.push_back (client);
            }
        }
        for (auto &[_, publisher] : state->native_publishers) {
            if (publisher) {
                publishers.push_back (publisher);
            }
        }
        state->native_clients.clear ();
        state->native_publishers.clear ();
    }
    for (auto &client : clients) {
        client->close ();
    }
    for (auto &publisher : publishers) {
        publisher->close ();
    }
}

channel_outbound_exchange_t::channel_outbound_exchange_t (
  std::shared_ptr<channel_runtime_state_t> state) :
    _state (std::move (state))
{
}

message_bus_t::erased_request_result_t
channel_outbound_exchange_t::submit_request (std::string channel_name,
                                             std::string packet_name,
                                             std::type_index request_type,
                                             message_bus_t::payload_encoder_t encode_payload,
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
    if (!can_wait_for_client_endpoint (_state, client)) {
        (void) runtime.cancel_outbound_request (reservation.value ());
        return message_bus_t::erased_request_result_t (framework_exception_t (
          framework_error_kind_t::disconnected, "channel client is not connected"));
    }
    if (_state->serializers != nullptr && client != nullptr) {
        try {
            runtime::messaging::client_call_codec_t codec;
            const auto effective_timeout =
              resolve_channel_wait_timeout (_state, channel_name, timeout);
            auto header = codec.create_envelope (runtime::messaging::message_kind_t::request,
                                                 channel_name, call_packet_name,
                                                 effective_timeout);
            header.metadata = metadata;
            detail::message_flow_tracer_t (_state->dispatch)
              .trace (message_flow_outcome_t::sent, [&] {
                  return message_flow_event_t{message_flow_outcome_t::sent,
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
            auto parts = encode_channel_payload_parts (header, request_type, encode_payload,
                                                       *_state->serializers);

            std::shared_ptr<channel_native_client_t> native_client;
            {
                std::lock_guard lock (_state->mutex);
                auto &slot = _state->native_clients[channel_name];
                if (!slot) {
                    slot = std::make_shared<channel_native_client_t> (channel_name, *client);
                }
                native_client = slot;
            }
            auto endpoints = make_client_endpoint_provider (_state, channel_name);

            auto native_reply = native_client->request (parts, endpoints, effective_timeout);
            if (!native_reply) {
                (void) runtime.cancel_outbound_request (reservation.value ());
                return message_bus_t::erased_request_result_t (framework_exception_t (
                  native_reply.error_kind (),
                  native_reply.error () ? native_reply.error ()->what ()
                                        : "channel native request failed",
                  native_reply.error () && native_reply.error ()->is_retriable ()));
            }
            auto validation = validate_channel_native_reply (native_reply.value ());
            if (!validation) {
                (void) runtime.cancel_outbound_request (reservation.value ());
                return message_bus_t::erased_request_result_t (framework_exception_t (
                  validation.error_kind (), validation.error () ? validation.error ()->what ()
                                                                : "channel reply decode failed"));
            }
            auto completion = runtime.complete_outbound_reply (reservation.value ());
            if (!completion) {
                return message_bus_t::erased_request_result_t (framework_exception_t (
                  completion.error_kind (),
                  completion.error () ? completion.error ()->what () : "channel request failed"));
            }

            auto reply_header = envelope.decode_header (native_reply.value ());
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
            auto body = envelope.decode_body (native_reply.value ());
            if (!body) {
                return message_bus_t::erased_request_result_t (framework_exception_t (
                  body.error_kind (),
                  body.error () ? body.error ()->what () : "channel reply body decode failed"));
            }
            detail::message_flow_tracer_t (_state->dispatch)
              .trace (message_flow_outcome_t::reply_received, [&] {
                  return message_flow_event_t{message_flow_outcome_t::reply_received,
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
                                          message_bus_t::payload_encoder_t encode_payload,
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
    if (!can_wait_for_client_endpoint (_state, client)) {
        return result_t<void>::failure (framework_error_kind_t::disconnected,
                                        "channel client is not connected");
    }
    if (_state->serializers != nullptr && client != nullptr) {
        try {
            runtime::messaging::client_call_codec_t codec;
            auto header = codec.create_envelope (runtime::messaging::message_kind_t::command,
                                                 channel_name, call_packet_name, timeout);
            header.metadata = metadata;
            detail::message_flow_tracer_t (_state->dispatch)
              .trace (message_flow_outcome_t::sent, [&] {
                  return message_flow_event_t{message_flow_outcome_t::sent,
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
            auto parts = encode_channel_payload_parts (header, message_type, encode_payload,
                                                       *_state->serializers);
            std::shared_ptr<channel_native_client_t> native_client;
            {
                std::lock_guard lock (_state->mutex);
                auto &slot = _state->native_clients[channel_name];
                if (!slot) {
                    slot = std::make_shared<channel_native_client_t> (channel_name, *client);
                }
                native_client = slot;
            }
            auto endpoints = make_client_endpoint_provider (_state, channel_name);
            const auto effective_timeout =
              resolve_channel_wait_timeout (_state, channel_name, timeout);
            return native_client->send (parts, endpoints, effective_timeout);
        }
        catch (const framework_exception_t &error) {
            return result_t<void>::failure (error.kind (), error.what ());
        }
        catch (const std::exception &error) {
            return result_t<void>::failure (framework_error_kind_t::request_failed, error.what ());
        }
        catch (...) {
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
                                             message_bus_t::payload_encoder_t encode_payload,
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
              .trace (message_flow_outcome_t::sent, [&] {
                  return message_flow_event_t{message_flow_outcome_t::sent,
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
            auto parts = encode_channel_payload_parts (header, event_type, encode_payload,
                                                       *_state->serializers);
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
