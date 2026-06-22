/* SPDX-License-Identifier: MPL-2.0 */

#include "channel_runtime.hpp"

#include <zlink/framework/contracts/configuration/zlink_builder.hpp>
#include <zlink.hpp>

#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/diagnostics/message_flow_tracer.hpp"
#include "runtime/dispatch/coroutine_executor.hpp"
#include "runtime/messaging/client_call_codec.hpp"
#include "runtime/messaging/envelope_codec.hpp"
#include "runtime/messaging/request_failure_mapper.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <cerrno>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace zlink::framework::detail
{

channel_capability_snapshot_t &select_capability (channel_builder_state_t &state,
                                                  channel_capability_t kind)
{
    auto &snapshot = state.target == nullptr ? state.snapshot : *state.target;
    switch (kind) {
        case channel_capability_t::server:
            return snapshot.server;
        case channel_capability_t::client:
            return snapshot.client;
        case channel_capability_t::publisher:
            return snapshot.publisher;
        case channel_capability_t::subscriber:
            return snapshot.subscriber;
    }
    return snapshot.client;
}

const channel_capability_snapshot_t *server_capability (const channel_runtime_state_t &state,
                                                        const std::string &channel_name)
{
    const auto found = state.channels.find (channel_name);
    if (found == state.channels.end ()) {
        return nullptr;
    }
    return &found->second.server;
}

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

bool is_enabled (const channel_capability_snapshot_t *capability)
{
    return capability != nullptr && capability->enabled;
}

bool has_connection (const channel_capability_snapshot_t *capability)
{
    return capability != nullptr && capability->enabled
           && (capability->discovery || !capability->bind_endpoints.empty ()
               || !capability->connect_endpoints.empty ());
}

void connect_registry_with_retry (zlink::service::discovery_t &discovery,
                                  const std::string &endpoint)
{
    std::exception_ptr last_error;
    for (int attempt = 0; attempt < 100; ++attempt) {
        try {
            discovery.connect_registry (endpoint);
            return;
        }
        catch (...) {
            last_error = std::current_exception ();
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
    }
    if (last_error) {
        std::rethrow_exception (last_error);
    }
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

result_t<void> validate_channel_native_reply (
  const runtime::messaging::message_parts_t &parts)
{
    if (parts.size () != 2) {
        return result_t<void>::failure (
          framework_error_kind_t::request_protocol_error,
          "channel native request returned an invalid reply frame count");
    }
    runtime::messaging::envelope_codec_t envelope;
    auto header = envelope.decode_header (parts);
    if (!header) {
        return result_t<void>::failure (
          framework_error_kind_t::request_protocol_error,
          header.error () ? header.error ()->what () : "channel reply header decode failed");
    }
    if (header.value ().kind == runtime::messaging::message_kind_t::error) {
        return result_t<void>::success ();
    }
    if (header.value ().kind != runtime::messaging::message_kind_t::response) {
        return result_t<void>::failure (
          framework_error_kind_t::request_protocol_error,
          "channel native request returned an invalid reply message kind");
    }
    auto body = envelope.decode_body (parts);
    if (!body) {
        return result_t<void>::failure (
          framework_error_kind_t::request_protocol_error,
          body.error () ? body.error ()->what () : "channel reply body decode failed");
    }
    return result_t<void>::success ();
}

class channel_native_publisher_t
{
  public:
    explicit channel_native_publisher_t (const channel_capability_snapshot_t &publisher) :
        _socket (_context)
    {
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
        const auto deadline =
          std::chrono::steady_clock::now () + std::chrono::milliseconds (200);
        while (std::chrono::steady_clock::now () < deadline) {
            zlink::subscription_event_t subscription;
            (void) _socket.receive_subscription_event (subscription,
                                                       zlink::recv_flags_t::dontwait);
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
    }

    zlink::context_t _context;
    zlink::xpub_socket_t _socket;
    std::mutex _mutex;
};

class pending_operation_controller_t
{
  public:
    explicit pending_operation_controller_t (channel_runtime_state_t &state) : _state (state) {}

    result_t<std::uint64_t> reserve_request (std::string channel_name)
    {
        if (auto admission = ensure_admission (); !admission) {
            return result_t<std::uint64_t>::failure (
              admission.error_kind (),
              admission.error () ? admission.error ()->what () : "channel request was rejected");
        }

        const auto request_seq = _state.pending_requests.next_request_seq ();
        _state.pending_requests.register_request (request_seq, std::move (channel_name));
        ++_state.pending;
        return result_t<std::uint64_t>::success (request_seq);
    }

    result_t<std::uint64_t> queue_send (std::string channel_name, std::string idempotency_key)
    {
        if (auto admission = ensure_admission (); !admission) {
            return result_t<std::uint64_t>::failure (
              admission.error_kind (),
              admission.error () ? admission.error ()->what () : "channel send was rejected");
        }

        const auto operation_id = _state.pending_requests.next_request_seq ();
        _state.pending_operations.emplace (
          operation_id, channel_reliability_event_t{
                          std::move (channel_name), std::move (idempotency_key),
                          framework_error_kind_t::timeout, "pending operation timed out"});
        ++_state.pending;
        return result_t<std::uint64_t>::success (operation_id);
    }

    result_t<void> complete_request (std::uint64_t request_seq)
    {
        if (!_state.pending_requests.remove (request_seq)) {
            return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                            "reply does not match a pending outbound request");
        }
        decrement_pending ();
        return result_t<void>::success ();
    }

    result_t<void> cancel_request (std::uint64_t request_seq)
    {
        if (_state.pending_requests.remove (request_seq)) {
            decrement_pending ();
        }
        return result_t<void>::success ();
    }

    result_t<void> mark_send_ready (std::uint64_t operation_id)
    {
        const auto found = _state.pending_operations.find (operation_id);
        if (found == _state.pending_operations.end ()) {
            return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                            "send-ready does not match a pending operation");
        }
        _state.pending_operations.erase (found);
        decrement_pending ();
        return result_t<void>::success ();
    }

    result_t<void> expire (std::uint64_t operation_id)
    {
        const auto found = _state.pending_operations.find (operation_id);
        if (found == _state.pending_operations.end ()) {
            return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                            "timeout does not match a pending operation");
        }

        auto event = found->second;
        _state.pending_operations.erase (found);
        decrement_pending ();
        if (_state.dead_letter_hook) {
            _state.dead_letter_hook (event);
        }
        return result_t<void>::failure (framework_error_kind_t::timeout,
                                        "pending operation timed out");
    }

    result_t<void> retry (std::uint64_t operation_id)
    {
        const auto found = _state.pending_operations.find (operation_id);
        if (found == _state.pending_operations.end ()) {
            return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                            "retry does not match a pending operation");
        }
        if (_state.retry_hook) {
            _state.retry_hook (found->second);
        }
        return result_t<void>::success ();
    }

    void drain () noexcept
    {
        _state.pending_requests.clear ();
        _state.pending_operations.clear ();
        _state.pending = 0;
    }

  private:
    result_t<void> ensure_admission () const
    {
        if (_state.shutdown) {
            return result_t<void>::failure (framework_error_kind_t::shutdown,
                                            "channel runtime is shutting down");
        }
        if (_state.closed) {
            return result_t<void>::failure (framework_error_kind_t::closed,
                                            "channel runtime is closed");
        }
        if (_state.pending >= _state.max_pending) {
            return result_t<void>::failure (framework_error_kind_t::request_rejected,
                                            "channel pending queue is full");
        }
        return result_t<void>::success ();
    }

    void decrement_pending () noexcept
    {
        if (_state.pending > 0) {
            --_state.pending;
        }
    }

    channel_runtime_state_t &_state;
};

void ensure_manual_allowed (const channel_capability_snapshot_t &snapshot)
{
    if (snapshot.discovery) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "manual endpoint cannot be mixed with discovery in one capability");
    }
}

void ensure_discovery_allowed (const channel_capability_snapshot_t &snapshot)
{
    if (!snapshot.bind_endpoints.empty () || !snapshot.connect_endpoints.empty ()) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "discovery cannot be mixed with manual endpoints in one capability");
    }
}

channel_runtime_t::channel_runtime_t (std::shared_ptr<channel_runtime_state_t> state) :
    _state (std::move (state))
{
}

void apply_dispatch_options (zlink_builder_t &builder, const dispatch_options_t &options)
{
    builder._state->runtime->dispatch = options;
    for (auto &[_, spot_node] : builder._state->spot_nodes) {
        spot_node->dispatch = options;
    }
    builder._state->stream_runtime->dispatch = options;
}

result_t<zlink::message_t>
channel_runtime_t::dispatch_request (std::string channel_name,
                                     std::string topic,
                                     std::string packet_name,
                                     service_provider_t &services,
                                     serializer_registry_t &serializers,
                                     const handler_registry_t &handlers,
                                     const zlink::message_t &message) const
{
    if (!is_enabled (server_capability (*_state, channel_name))) {
        return result_t<zlink::message_t>::failure (framework_error_kind_t::route_not_connected,
                                                    "channel server capability is not enabled");
    }
    return handlers.invoke (channel_name, topic, packet_name, services, serializers, message);
}

result_t<void> channel_runtime_t::dispatch_send (std::string channel_name,
                                                 std::string topic,
                                                 std::string packet_name,
                                                 service_provider_t &services,
                                                 serializer_registry_t &serializers,
                                                 const handler_registry_t &handlers,
                                                 const zlink::message_t &message) const
{
    auto result =
      dispatch_request (std::move (channel_name), std::move (topic), std::move (packet_name),
                        services, serializers, handlers, message);
    if (!result) {
        return result_t<void>::failure (result.error_kind (), result.error ()
                                                                ? result.error ()->what ()
                                                                : "channel dispatch failed");
    }
    return result_t<void>::success ();
}

result_t<std::uint64_t> channel_runtime_t::reserve_outbound_request (std::string channel_name)
{
    std::lock_guard lock (_state->mutex);
    const auto *client = client_capability (*_state, channel_name);
    if (!has_connection (client)) {
        return result_t<std::uint64_t>::failure (framework_error_kind_t::disconnected,
                                                 "channel client is not connected");
    }
    return pending_operation_controller_t (*_state).reserve_request (std::move (channel_name));
}

result_t<std::uint64_t> channel_runtime_t::queue_pending_send (std::string channel_name,
                                                               std::string idempotency_key)
{
    std::lock_guard lock (_state->mutex);
    return pending_operation_controller_t (*_state).queue_send (std::move (channel_name),
                                                                std::move (idempotency_key));
}

result_t<void> channel_runtime_t::complete_outbound_reply (std::uint64_t request_seq)
{
    std::lock_guard lock (_state->mutex);
    return pending_operation_controller_t (*_state).complete_request (request_seq);
}

result_t<void> channel_runtime_t::cancel_outbound_request (std::uint64_t request_seq)
{
    std::lock_guard lock (_state->mutex);
    return pending_operation_controller_t (*_state).cancel_request (request_seq);
}

result_t<void> channel_runtime_t::mark_send_ready (std::uint64_t operation_id)
{
    std::lock_guard lock (_state->mutex);
    return pending_operation_controller_t (*_state).mark_send_ready (operation_id);
}

result_t<void> channel_runtime_t::expire_pending (std::uint64_t operation_id)
{
    std::lock_guard lock (_state->mutex);
    return pending_operation_controller_t (*_state).expire (operation_id);
}

result_t<void> channel_runtime_t::retry_pending (std::uint64_t operation_id)
{
    std::lock_guard lock (_state->mutex);
    return pending_operation_controller_t (*_state).retry (operation_id);
}

void channel_runtime_t::close () noexcept
{
    _state->closed = true;
    drain ();
}

void channel_runtime_t::shutdown () noexcept
{
    _state->shutdown = true;
    drain ();
}

std::size_t channel_runtime_t::pending_count () const noexcept
{
    std::lock_guard lock (_state->mutex);
    return _state->pending;
}

std::size_t channel_runtime_t::pending_limit () const noexcept
{
    return _state->max_pending;
}

std::vector<channel_runtime_state_t::outbound_call_record_t>
channel_runtime_t::outbound_calls () const
{
    std::lock_guard lock (_state->mutex);
    return _state->outbound_calls;
}

void channel_runtime_t::bind_serializers (serializer_registry_t &serializers) noexcept
{
    _state->serializers = &serializers;
}

void channel_runtime_t::bind_discovery (discovery_snapshot_t discovery) noexcept
{
    _state->discovery = std::move (discovery);
}

dispatch_options_t channel_runtime_t::dispatch_options () const
{
    return _state->dispatch;
}

void channel_runtime_t::drain () noexcept
{
    std::lock_guard lock (_state->mutex);
    pending_operation_controller_t (*_state).drain ();
}

channel_runtime_t channel_runtime_t::from (const message_bus_t &bus)
{
    return channel_runtime_t (bus._state);
}

} // namespace zlink::framework::detail

namespace zlink::framework
{

namespace
{

channel_capability_snapshot_t &capability_snapshot (detail::capability_builder_state_t &state)
{
    return state.target == nullptr ? state.snapshot : *state.target;
}

const channel_capability_snapshot_t &
capability_snapshot (const detail::capability_builder_state_t &state)
{
    return state.target == nullptr ? state.snapshot : *state.target;
}

} // namespace

capability_builder_t::capability_builder_t () :
    _state (std::make_shared<detail::capability_builder_state_t> ())
{
}

capability_builder_t::capability_builder_t (
  std::shared_ptr<detail::capability_builder_state_t> state) :
    _state (std::move (state))
{
}

capability_builder_t::~capability_builder_t () = default;

capability_builder_t::capability_builder_t (capability_builder_t &&) noexcept = default;

capability_builder_t &capability_builder_t::operator= (capability_builder_t &&) noexcept = default;

capability_builder_t &capability_builder_t::bind (std::string endpoint)
{
    auto &snapshot = capability_snapshot (*_state);
    detail::ensure_manual_allowed (snapshot);
    snapshot.enabled = true;
    snapshot.bind_endpoints.push_back (std::move (endpoint));
    return *this;
}

capability_builder_t &capability_builder_t::connect (std::string endpoint)
{
    auto &snapshot = capability_snapshot (*_state);
    detail::ensure_manual_allowed (snapshot);
    snapshot.enabled = true;
    snapshot.connect_endpoints.push_back (std::move (endpoint));
    return *this;
}

capability_builder_t &capability_builder_t::use_discovery ()
{
    auto &snapshot = capability_snapshot (*_state);
    detail::ensure_discovery_allowed (snapshot);
    snapshot.enabled = true;
    snapshot.discovery = true;
    return *this;
}

capability_builder_t &capability_builder_t::set_routing_id (zlink::routing_id_t routing_id)
{
    auto &snapshot = capability_snapshot (*_state);
    snapshot.enabled = true;
    snapshot.routing_id = std::move (routing_id);
    return *this;
}

channel_capability_snapshot_t capability_builder_t::snapshot () const
{
    return capability_snapshot (*_state);
}

channel_builder_t::channel_builder_t () :
    _state (std::make_shared<detail::channel_builder_state_t> (""))
{
}

channel_builder_t::channel_builder_t (std::shared_ptr<detail::channel_builder_state_t> state) :
    _state (std::move (state))
{
}

channel_builder_t::~channel_builder_t () = default;

channel_builder_t::channel_builder_t (channel_builder_t &&) noexcept = default;

channel_builder_t &channel_builder_t::operator= (channel_builder_t &&) noexcept = default;

capability_builder_t channel_builder_t::enable_capability (channel_capability_snapshot_t &target)
{
    auto state = std::make_shared<detail::capability_builder_state_t> ();
    state->target = &target;
    target.enabled = true;
    return capability_builder_t (state);
}

capability_builder_t channel_builder_t::enable_server ()
{
    return enable_capability (detail::select_capability (*_state, channel_capability_t::server));
}

capability_builder_t channel_builder_t::enable_client ()
{
    return enable_capability (detail::select_capability (*_state, channel_capability_t::client));
}

capability_builder_t channel_builder_t::enable_publisher ()
{
    return enable_capability (detail::select_capability (*_state, channel_capability_t::publisher));
}

capability_builder_t channel_builder_t::enable_subscriber ()
{
    return enable_capability (
      detail::select_capability (*_state, channel_capability_t::subscriber));
}

channel_builder_t &channel_builder_t::default_request_timeout (std::chrono::milliseconds timeout)
{
    if (timeout <= std::chrono::milliseconds::zero ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "channel request timeout must be greater than zero");
    }
    _state->snapshot.default_request_timeout = timeout;
    if (_state->target != nullptr) {
        _state->target->default_request_timeout = timeout;
    }
    return *this;
}

channel_snapshot_t channel_builder_t::snapshot () const
{
    return _state->target == nullptr ? _state->snapshot : *_state->target;
}

route_channel_builder_t::route_channel_builder_t ()
{
}

route_channel_builder_t::route_channel_builder_t (
  std::shared_ptr<detail::route_channel_builder_state_t> state) :
    _state (std::move (state))
{
}

route_channel_builder_t::~route_channel_builder_t () = default;

route_channel_builder_t::route_channel_builder_t (route_channel_builder_t &&) noexcept = default;

route_channel_builder_t &
route_channel_builder_t::operator= (route_channel_builder_t &&) noexcept = default;

route_channel_builder_t &route_channel_builder_t::bind (std::string endpoint)
{
    _state->registration.bind (std::move (endpoint));
    return *this;
}

route_channel_builder_t &route_channel_builder_t::set_routing_id (zlink::routing_id_t routing_id)
{
    _state->registration.set_routing_id (std::move (routing_id));
    return *this;
}

route_channel_builder_t &route_channel_builder_t::connect (std::string endpoint)
{
    _state->registration.connect (std::move (endpoint));
    return *this;
}

route_channel_builder_t &
route_channel_builder_t::default_request_timeout (std::chrono::milliseconds timeout)
{
    _state->registration.default_request_timeout (timeout);
    return *this;
}

route_channel_builder_t &route_channel_builder_t::add_handler_group (std::string group_name)
{
    _state->registration.add_handler_group (std::move (group_name));
    return *this;
}

route_channel_builder_t &
route_channel_builder_t::enable_spot_route_egress (std::string target_spot_node_channel_name)
{
    _state->registration.enable_spot_route_egress (std::move (target_spot_node_channel_name));
    return *this;
}

route_channel_builder_t &
route_channel_builder_t::add_handler (route_handler_registration_t registration)
{
    _state->registration.add_handler (std::move (registration));
    return *this;
}

message_bus_t::erased_request_result_t::erased_request_result_t (framework_exception_t error) :
    _error (std::move (error))
{
}

message_bus_t::erased_request_result_t::erased_request_result_t (
  zlink::message_t reply, serializer_registry_t &serializers) :
    _error (framework_error_kind_t::request_failed, ""), _reply (std::move (reply)),
    _serializers (&serializers)
{
}

message_bus_t::message_bus_t () : _state (std::make_shared<detail::channel_runtime_state_t> ())
{
}

message_bus_t::message_bus_t (std::shared_ptr<detail::channel_runtime_state_t> state) :
    _state (std::move (state))
{
}

message_bus_t::~message_bus_t () = default;

message_bus_t::message_bus_t (message_bus_t &&) noexcept = default;

message_bus_t &message_bus_t::operator= (message_bus_t &&) noexcept = default;

std::size_t message_bus_t::pending_count () const noexcept
{
    std::lock_guard lock (_state->mutex);
    return _state->pending;
}

std::size_t message_bus_t::pending_limit () const noexcept
{
    std::lock_guard lock (_state->mutex);
    return _state->max_pending;
}

std::chrono::milliseconds
message_bus_t::default_request_timeout (const std::string &channel_name) const
{
    std::lock_guard lock (_state->mutex);
    const auto found = _state->channels.find (channel_name);
    if (found != _state->channels.end () && found->second.default_request_timeout) {
        return *found->second.default_request_timeout;
    }
    return _state->default_request_timeout;
}

serializer_registry_t *message_bus_t::serializers () const noexcept
{
    return _state ? _state->serializers : nullptr;
}

message_bus_t::erased_request_result_t
message_bus_t::submit_request (std::string channel_name,
                               std::string packet_name,
                               std::type_index request_type,
                               const void *request,
                               std::chrono::milliseconds timeout,
                               const channel_request_call_t::metadata_map_t &metadata)
{
    detail::channel_runtime_t runtime (_state);
    const auto *client = detail::client_capability (*_state, channel_name);
    const auto call_packet_name = std::move (packet_name);
    {
        std::lock_guard lock (_state->mutex);
        _state->outbound_calls.push_back (
          {"request", channel_name, "", call_packet_name, timeout, metadata});
    }
    auto reservation = runtime.reserve_outbound_request (channel_name);
    if (!reservation) {
        return erased_request_result_t (framework_exception_t (
          reservation.error_kind (),
          reservation.error () ? reservation.error ()->what () : "channel request failed"));
    }
    const bool client_uses_discovery =
      client != nullptr && client->discovery && !_state->discovery.registry_endpoints.empty ();
    if (_state->serializers != nullptr && client != nullptr
        && (client_uses_discovery || !client->connect_endpoints.empty ())) {
        try {
            runtime::messaging::client_call_codec_t codec;
            auto header = codec.create_envelope (runtime::messaging::message_kind_t::request,
                                                 channel_name,
                                                 call_packet_name,
                                                 timeout);
            header.metadata = metadata;
            detail::message_flow_tracer_t (runtime.dispatch_options ())
              .trace (message_flow_event_t{message_flow_phase_t::sent,
                                           dispatch_error_surface_t::channel,
                                           dispatch_message_kind_t::request,
                                           call_packet_name,
                                           channel_name,
                                           std::nullopt,
                                           header.correlation_id,
                                           std::nullopt,
                                           std::nullopt,
                                           std::nullopt,
                                           std::nullopt});
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
                    return erased_request_result_t (framework_exception_t (
                      framework_error_kind_t::disconnected,
                      "channel client has no endpoint"));
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
            const std::size_t attempt_count = client_uses_discovery && endpoints.empty ()
                                                ? 1
                                                : endpoints.size ();
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
                    zlink::message_t request_body =
                      zlink::message_t::from (parts[1].to_string ());
                    zlink::context_t context;
                    zlink::dealer_socket_t dealer (context);
                    dealer.channel_name (channel_name);
                    dealer.options ().immediate (true);
                    dealer.options ().connect_timeout (connect_timeout);
                    dealer.options ().request_timeout (connect_timeout);
                    std::unique_ptr<zlink::service::discovery_t> discovery;
                    if (client_uses_discovery && endpoints.empty ()) {
                        discovery = std::make_unique<zlink::service::discovery_t> (
                          context, zlink::auto_connect_type::client_server, channel_name);
                        for (const auto &registry_endpoint : _state->discovery.registry_endpoints) {
                            detail::connect_registry_with_retry (*discovery, registry_endpoint);
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
                    auto pending_reply =
                      dealer.request ()
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
                    auto validation =
                      detail::validate_channel_native_reply (candidate_reply_parts);
                    if (!validation) {
                        throw framework_exception_t (
                          validation.error_kind (),
                          validation.error () ? validation.error ()->what ()
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
                    last_attempt_error = detail::map_native_request_exception (error);
                }
                catch (...) {
                    last_attempt_error = framework_exception_t (
                      framework_error_kind_t::request_failed, "channel native request failed");
                }
            }
            if (!received_reply) {
                (void) runtime.cancel_outbound_request (reservation.value ());
                if (last_attempt_error) {
                    return erased_request_result_t (*last_attempt_error);
                }
                return erased_request_result_t (framework_exception_t (
                  framework_error_kind_t::request_failed, "channel native request failed"));
            }
            auto completion = runtime.complete_outbound_reply (reservation.value ());
            if (!completion) {
                return erased_request_result_t (framework_exception_t (
                  completion.error_kind (),
                  completion.error () ? completion.error ()->what () : "channel request failed"));
            }

            auto reply_header = envelope.decode_header (reply_parts);
            if (!reply_header) {
                return erased_request_result_t (framework_exception_t (
                  reply_header.error_kind (),
                  reply_header.error () ? reply_header.error ()->what ()
                                        : "channel reply header decode failed"));
            }
            if (reply_header.value ().kind == runtime::messaging::message_kind_t::error) {
                runtime::messaging::request_failure_mapper_t failure_mapper;
                return erased_request_result_t (
                  failure_mapper.error_header_exception (
                    reply_header.value ().error_code.value_or ("request_failed"),
                    reply_header.value ().error_message.value_or ("channel request failed"),
                    "channel request"));
            }
            auto body = envelope.decode_body (reply_parts);
            if (!body) {
                return erased_request_result_t (framework_exception_t (
                  body.error_kind (),
                  body.error () ? body.error ()->what () : "channel reply body decode failed"));
            }
            detail::message_flow_tracer_t (runtime.dispatch_options ())
              .trace (message_flow_event_t{message_flow_phase_t::reply_received,
                                           dispatch_error_surface_t::channel,
                                           dispatch_message_kind_t::response,
                                           call_packet_name,
                                           channel_name,
                                           std::nullopt,
                                           header.correlation_id,
                                           std::nullopt,
                                           std::nullopt,
                                           std::nullopt,
                                           std::nullopt});
            return erased_request_result_t (body.value (), *_state->serializers);
        }
        catch (const framework_exception_t &error) {
            (void) runtime.cancel_outbound_request (reservation.value ());
            return erased_request_result_t (error);
        }
        catch (const std::exception &error) {
            (void) runtime.cancel_outbound_request (reservation.value ());
            return erased_request_result_t (detail::map_native_request_exception (error));
        }
        catch (...) {
            (void) runtime.cancel_outbound_request (reservation.value ());
            return erased_request_result_t (framework_exception_t (
              framework_error_kind_t::request_failed, "channel native request failed"));
        }
    }
    (void) runtime.cancel_outbound_request (reservation.value ());
    return erased_request_result_t (
      framework_exception_t (framework_error_kind_t::timeout,
                             "request reply was not completed by the local test runtime"));
}

result_t<void> message_bus_t::submit_send (std::string channel_name,
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
    const auto *client = detail::client_capability (*_state, channel_name);
    if (!detail::has_connection (client)) {
        return result_t<void>::failure (framework_error_kind_t::disconnected,
                                        "channel client is not connected");
    }
    const bool client_uses_discovery =
      client != nullptr && client->discovery && !_state->discovery.registry_endpoints.empty ();
    if (_state->serializers != nullptr && client != nullptr
        && (client_uses_discovery || !client->connect_endpoints.empty ())) {
        try {
            runtime::messaging::client_call_codec_t codec;
            auto header = codec.create_envelope (runtime::messaging::message_kind_t::command,
                                                 channel_name,
                                                 call_packet_name,
                                                 timeout);
            header.metadata = metadata;
            detail::message_flow_tracer_t (_state->dispatch)
              .trace (message_flow_event_t{message_flow_phase_t::sent,
                                           dispatch_error_surface_t::channel,
                                           dispatch_message_kind_t::send,
                                           call_packet_name,
                                           channel_name,
                                           std::nullopt,
                                           header.correlation_id,
                                           std::nullopt,
                                           std::nullopt,
                                           std::nullopt,
                                           std::nullopt});
            runtime::messaging::envelope_codec_t envelope;
            auto parts = envelope.encode_parts (header, message_type, message, *_state->serializers);

            zlink::context_t context;
            zlink::dealer_socket_t dealer (context);
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
                    return result_t<void>::failure (framework_error_kind_t::disconnected,
                                                    "channel client has no endpoint");
                }
            }
            std::unique_ptr<zlink::service::discovery_t> discovery;
            if (client_uses_discovery && endpoints.empty ()) {
                discovery = std::make_unique<zlink::service::discovery_t> (
                  context, zlink::auto_connect_type::client_server, channel_name);
                for (const auto &registry_endpoint : _state->discovery.registry_endpoints) {
                    detail::connect_registry_with_retry (*discovery, registry_endpoint);
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
            const bool sent = dealer.send ()
                                .message (send_header)
                                .message (send_body)
                                .submit ();
            if (!sent) {
                return result_t<void>::failure (framework_error_kind_t::request_failed,
                                                "channel native send failed");
            }
        }
        catch (const framework_exception_t &error) {
            return result_t<void>::failure (error.kind (), error.what ());
        }
        catch (const std::exception &error) {
            return result_t<void>::failure (framework_error_kind_t::request_failed,
                                            error.what ());
        }
        catch (...) {
            return result_t<void>::failure (framework_error_kind_t::request_failed,
                                            "channel native send failed");
        }
    }
    return result_t<void>::success ();
}

result_t<void> message_bus_t::submit_publish (std::string channel_name,
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
    const auto *publisher = detail::publisher_capability (*_state, channel_name);
    if (!detail::has_connection (publisher)) {
        return result_t<void>::failure (framework_error_kind_t::disconnected,
                                        "channel publisher is not connected");
    }
    if (_state->serializers != nullptr && publisher != nullptr
        && (!publisher->bind_endpoints.empty () || !publisher->connect_endpoints.empty ())) {
        try {
            runtime::messaging::client_call_codec_t codec;
            auto header = codec.create_envelope (runtime::messaging::message_kind_t::publish,
                                                 channel_name,
                                                 call_packet_name,
                                                 timeout, topic);
            header.metadata = metadata;
            detail::message_flow_tracer_t (_state->dispatch)
              .trace (message_flow_event_t{message_flow_phase_t::sent,
                                           dispatch_error_surface_t::channel,
                                           dispatch_message_kind_t::publish,
                                           call_packet_name,
                                           channel_name,
                                           topic,
                                           header.correlation_id,
                                           std::nullopt,
                                           std::nullopt,
                                           std::nullopt,
                                           std::nullopt});
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
            return result_t<void>::failure (framework_error_kind_t::request_failed,
                                            error.what ());
        }
        catch (...) {
            return result_t<void>::failure (framework_error_kind_t::request_failed,
                                            "channel native publish failed");
        }
    }
    return result_t<void>::success ();
}

request_client_t::request_client_t (message_bus_t bus, std::string channel_name) :
    _bus (std::move (bus)), _channel_name (std::move (channel_name))
{
}

publisher_t::publisher_t (message_bus_t bus) : _bus (std::move (bus))
{
}

route_send_call_t::route_send_call_t (std::string packet_name, submit_fn_t submit) :
    _packet_name (std::move (packet_name)), _submit (std::move (submit))
{
}

route_send_call_t &route_send_call_t::packet_name (std::string packet_name)
{
    _packet_name = std::move (packet_name);
    return *this;
}

route_send_call_t &route_send_call_t::metadata (std::string key, std::string value)
{
    _metadata[std::move (key)] = std::move (value);
    return *this;
}

task_t<void> route_send_call_t::async ()
{
    if (!_submit) {
        return task_t<void> (result_t<void>::failure (
          framework_error_kind_t::request_protocol_error,
          "route send call is not bound to a route client"));
    }
    return _submit (_packet_name, _metadata);
}

route_request_call_t::route_request_call_t (std::string packet_name, submit_fn_t submit) :
    _packet_name (std::move (packet_name)), _submit (std::move (submit))
{
}

route_request_call_t &route_request_call_t::packet_name (std::string packet_name)
{
    _packet_name = std::move (packet_name);
    return *this;
}

route_request_call_t &route_request_call_t::timeout (std::chrono::milliseconds timeout)
{
    _timeout = timeout;
    return *this;
}

route_request_call_t &route_request_call_t::metadata (std::string key, std::string value)
{
    _metadata[std::move (key)] = std::move (value);
    return *this;
}

task_t<std::uint64_t> route_request_call_t::async ()
{
    if (!_submit) {
        return task_t<std::uint64_t> (result_t<std::uint64_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "route request call is not bound to a route client"));
    }
    return _submit (_packet_name, _timeout, _metadata);
}

route_client_t::route_client_t () = default;

route_client_t::route_client_t (std::shared_ptr<detail::route_client_state_t> state,
                                serializer_registry_t &serializers) :
    _state (std::move (state)), _serializers (&serializers)
{
}

route_client_t::~route_client_t () = default;

route_client_t::route_client_t (route_client_t &&) noexcept = default;

route_client_t &route_client_t::operator= (route_client_t &&) noexcept = default;

task_t<void>
route_client_t::submit_send_erased (const std::shared_ptr<detail::route_client_state_t> &state,
                                    const std::string &router_channel_id,
                                    const zlink::routing_id_t &target_node_rid,
                                    const std::string &packet_name,
                                    std::type_index message_type,
                                    const void *message,
                                    const route_send_call_t::metadata_map_t &metadata)
{
    if (!state || !state->runtime || state->serializers == nullptr) {
        return task_t<void> (result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                                      "route client is not configured"));
    }
    runtime::messaging::message_parts_t parts;
    try {
        detail::channel_runtime_manager_t manager (state->runtime);
        auto &runtime = manager.get_route_channel (router_channel_id);
        runtime::messaging::client_call_codec_t codec;
        auto header = codec.create_envelope (runtime::messaging::message_kind_t::command,
                                             router_channel_id, packet_name);
        header.metadata = metadata;
        runtime::messaging::envelope_codec_t envelope;
        parts = envelope.encode_parts (header, message_type, message, *state->serializers);
    }
    catch (const framework_exception_t &error) {
        return task_t<void> (
          result_t<void>::failure (error.kind (), error.what (), error.is_retriable ()));
    }
    return runtime::handler_coroutine_executor ().submit<void> (
      [state, router_channel_id, target_node_rid,
       parts = std::move (parts)] () mutable -> boost::asio::awaitable<result_t<void>> {
          try {
              detail::channel_runtime_manager_t manager (state->runtime);
              auto &runtime = manager.get_route_channel (router_channel_id);
              co_return runtime.submit_send_parts (target_node_rid, std::move (parts));
          }
          catch (const framework_exception_t &error) {
              co_return result_t<void>::failure (error.kind (), error.what (),
                                                 error.is_retriable ());
          }
      });
}

task_t<std::uint64_t>
route_client_t::submit_request_erased (const std::shared_ptr<detail::route_client_state_t> &state,
                                       const std::string &router_channel_id,
                                       const zlink::routing_id_t &target_node_rid,
                                       const std::string &packet_name,
                                       std::type_index request_type,
                                       const void *request,
                                       std::chrono::milliseconds timeout,
                                       const route_request_call_t::metadata_map_t &metadata)
{
    if (!state || !state->runtime || state->serializers == nullptr) {
        return task_t<std::uint64_t> (result_t<std::uint64_t>::failure (
          framework_error_kind_t::request_protocol_error, "route client is not configured"));
    }
    runtime::messaging::message_parts_t parts;
    try {
        detail::channel_runtime_manager_t manager (state->runtime);
        auto &runtime = manager.get_route_channel (router_channel_id);
        const auto effective_timeout = timeout > std::chrono::milliseconds::zero ()
                                         ? timeout
                                         : runtime.default_request_timeout ();
        runtime::messaging::client_call_codec_t codec;
        auto header = codec.create_envelope (runtime::messaging::message_kind_t::request,
                                             router_channel_id, packet_name, effective_timeout);
        header.metadata = metadata;
        runtime::messaging::envelope_codec_t envelope;
        parts = envelope.encode_parts (header, request_type, request, *state->serializers);
    }
    catch (const framework_exception_t &error) {
        return task_t<std::uint64_t> (
          result_t<std::uint64_t>::failure (error.kind (), error.what (), error.is_retriable ()));
    }
    return runtime::handler_coroutine_executor ().submit<std::uint64_t> (
      [state, router_channel_id, target_node_rid,
       parts = std::move (parts)] () mutable -> boost::asio::awaitable<result_t<std::uint64_t>> {
          try {
              detail::channel_runtime_manager_t manager (state->runtime);
              auto &runtime = manager.get_route_channel (router_channel_id);
              co_return runtime.submit_request_parts (target_node_rid, std::move (parts));
          }
          catch (const framework_exception_t &error) {
              co_return result_t<std::uint64_t>::failure (error.kind (), error.what (),
                                                          error.is_retriable ());
          }
      });
}

task_t<zlink::message_t> route_client_t::submit_request_reply_message_erased (
  const std::shared_ptr<detail::route_client_state_t> &state,
  std::string router_channel_id,
  zlink::routing_id_t target_node_rid,
  std::string packet_name,
  std::type_index request_type,
  const void *request,
  std::chrono::milliseconds timeout,
  std::map<std::string, std::string> metadata)
{
    if (!state || !state->runtime || state->serializers == nullptr) {
        return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_protocol_error, "route client is not configured"));
    }
    runtime::messaging::message_parts_t parts;
    auto effective_timeout = timeout;
    try {
        detail::channel_runtime_manager_t manager (state->runtime);
        auto &runtime = manager.get_route_channel (router_channel_id);
        effective_timeout = timeout > std::chrono::milliseconds::zero ()
                              ? timeout
                              : runtime.default_request_timeout ();
        runtime::messaging::client_call_codec_t codec;
        auto header = codec.create_envelope (runtime::messaging::message_kind_t::request,
                                             router_channel_id, packet_name, effective_timeout);
        header.metadata = std::move (metadata);
        runtime::messaging::envelope_codec_t envelope;
        parts = envelope.encode_parts (header, request_type, request, *state->serializers);
    }
    catch (const framework_exception_t &error) {
        return task_t<zlink::message_t> (
          result_t<zlink::message_t>::failure (error.kind (), error.what (), error.is_retriable ()));
    }
    return runtime::handler_coroutine_executor ().submit<zlink::message_t> (
      [state, router_channel_id = std::move (router_channel_id),
       target_node_rid = std::move (target_node_rid), parts = std::move (parts),
       effective_timeout] () mutable -> boost::asio::awaitable<result_t<zlink::message_t>> {
          try {
              detail::channel_runtime_manager_t manager (state->runtime);
              auto &runtime = manager.get_route_channel (router_channel_id);
              runtime::messaging::envelope_codec_t envelope;
              auto reply =
                runtime.request_reply_parts (target_node_rid, std::move (parts), effective_timeout);
              if (!reply) {
                  co_return result_t<zlink::message_t>::failure (
                    reply.error_kind (),
                    reply.error () ? reply.error ()->what () : "route request failed");
              }
              auto reply_header = envelope.decode_header (reply.value ());
              if (!reply_header) {
                  co_return result_t<zlink::message_t>::failure (
                    reply_header.error_kind (),
                    reply_header.error () ? reply_header.error ()->what ()
                                          : "route reply header decode failed");
              }
              if (reply_header.value ().kind == runtime::messaging::message_kind_t::error) {
                  co_return result_t<zlink::message_t>::failure (
                    framework_error_kind_t::request_failed,
                    reply_header.value ().error_message.value_or ("route request failed"));
              }
              auto body = envelope.decode_body (reply.value ());
              if (!body) {
                  co_return result_t<zlink::message_t>::failure (
                    body.error_kind (),
                    body.error () ? body.error ()->what () : "route reply body decode failed");
              }
              co_return result_t<zlink::message_t>::success (body.value ());
          }
          catch (const framework_exception_t &error) {
              co_return result_t<zlink::message_t>::failure (error.kind (), error.what (),
                                                             error.is_retriable ());
          }
      });
}

zlink_builder_t::zlink_builder_t () : _state (std::make_shared<detail::zlink_builder_state_t> ())
{
}

zlink_builder_t::~zlink_builder_t () = default;

zlink_builder_t::zlink_builder_t (zlink_builder_t &&) noexcept = default;

zlink_builder_t &zlink_builder_t::operator= (zlink_builder_t &&) noexcept = default;

zlink_builder_t &zlink_builder_t::add_node (std::string node_name)
{
    _state->node_name = std::move (node_name);
    return *this;
}

zlink_builder_t &zlink_builder_t::max_pending (std::size_t count)
{
    _state->runtime->max_pending = count;
    return *this;
}

zlink_builder_t &zlink_builder_t::default_request_timeout (std::chrono::milliseconds timeout)
{
    if (timeout <= std::chrono::milliseconds::zero ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "request timeout must be greater than zero");
    }
    _state->runtime->default_request_timeout = timeout;
    return *this;
}

zlink_builder_t &zlink_builder_t::on_retry (retry_hook_t hook)
{
    _state->runtime->retry_hook = std::move (hook);
    return *this;
}

zlink_builder_t &zlink_builder_t::on_dead_letter (dead_letter_hook_t hook)
{
    _state->runtime->dead_letter_hook = std::move (hook);
    return *this;
}

channel_builder_t zlink_builder_t::channel (std::string channel_name)
{
    auto state = std::make_shared<detail::channel_builder_state_t> (std::move (channel_name));
    auto [entry, _] = _state->runtime->channels.insert_or_assign (state->snapshot.name,
                                                                  state->snapshot);
    state->target = &entry->second;
    return channel_builder_t (state);
}

std::vector<channel_snapshot_t> zlink_builder_t::channels () const
{
    std::vector<channel_snapshot_t> result;
    result.reserve (_state->runtime->channels.size ());
    for (const auto &[_, snapshot] : _state->runtime->channels) {
        result.push_back (snapshot);
    }
    return result;
}

message_bus_t zlink_builder_t::message_bus () const
{
    return message_bus_t (_state->runtime);
}

request_client_t zlink_builder_t::request_client (std::string channel_name) const
{
    return request_client_t (message_bus (), std::move (channel_name));
}

publisher_t zlink_builder_t::publisher () const
{
    return publisher_t (message_bus ());
}

route_client_t zlink_builder_t::route_client (serializer_registry_t &serializers) const
{
    detail::channel_runtime_manager_t manager (_state->runtime);
    manager.initialize_route_channels (*this);
    return route_client_t (
      std::make_shared<detail::route_client_state_t> (_state->runtime, serializers), serializers);
}

} // namespace zlink::framework
