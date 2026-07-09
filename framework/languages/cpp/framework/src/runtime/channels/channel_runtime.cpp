/* SPDX-License-Identifier: MPL-2.0 */

#include "channel_runtime.hpp"

#include <zlink/framework/contracts/configuration/zlink_builder.hpp>
#include <zlink/framework/contracts/spots/spot.hpp>
#include <zlink.hpp>

#include "runtime/channels/channel_outbound_exchange.hpp"
#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/diagnostics/monitoring_runtime.hpp"
#include "runtime/diagnostics/message_flow_tracer.hpp"
#include "runtime/dispatch/offload_executor.hpp"
#include "runtime/messaging/client_call_codec.hpp"
#include "runtime/messaging/envelope_codec.hpp"
#include "runtime/messaging/request_failure_mapper.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <algorithm>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace zlink::framework::detail
{

route_client_state_t::route_client_state_t (std::shared_ptr<channel_runtime_state_t> runtime,
                                            serializer_registry_t &serializers) :
    runtime (std::move (runtime)), serializers (&serializers)
{
    const auto hardware_workers =
      static_cast<std::size_t> (std::max (1u, std::thread::hardware_concurrency ()));
    executor = std::make_shared<zlink::framework::runtime::offload_executor_t> (
      0, hardware_workers, 1024, std::chrono::seconds (30));
}

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

result_t<void> validate_channel_native_reply (const runtime::messaging::message_parts_t &parts)
{
    if (parts.size () != 2) {
        return result_t<void>::failure (
          framework_error_kind_t::request_protocol_error,
          "channel native request returned an invalid reply frame count");
    }
    runtime::messaging::envelope_codec_t envelope;
    auto header = envelope.decode_header (parts);
    if (!header) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        header.error () ? header.error ()->what ()
                                                        : "channel reply header decode failed");
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
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        body.error () ? body.error ()->what ()
                                                      : "channel reply body decode failed");
    }
    return result_t<void>::success ();
}

class pending_operation_controller_t
{
  public:
    struct hook_dispatch_t
    {
        channel_reliability_event_t event;
        retry_hook_t retry_hook;
        dead_letter_hook_t dead_letter_hook;
    };

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

    result_t<hook_dispatch_t> expire (std::uint64_t operation_id)
    {
        const auto found = _state.pending_operations.find (operation_id);
        if (found == _state.pending_operations.end ()) {
            return result_t<hook_dispatch_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "timeout does not match a pending operation");
        }

        auto event = found->second;
        auto hook = _state.dead_letter_hook;
        _state.pending_operations.erase (found);
        decrement_pending ();
        return result_t<hook_dispatch_t>::success (
          hook_dispatch_t{std::move (event), retry_hook_t{}, std::move (hook)});
    }

    result_t<hook_dispatch_t> retry (std::uint64_t operation_id)
    {
        const auto found = _state.pending_operations.find (operation_id);
        if (found == _state.pending_operations.end ()) {
            return result_t<hook_dispatch_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "retry does not match a pending operation");
        }
        return result_t<hook_dispatch_t>::success (
          hook_dispatch_t{found->second, _state.retry_hook, dead_letter_hook_t{}});
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

channel_runtime_t::channel_runtime_t (std::shared_ptr<channel_runtime_state_t> state) :
    _state (std::move (state))
{
}

namespace
{

void drain_zlink_builder_state_runtime (zlink_builder_state_t &state) noexcept
{
    state.stream_runtime.reset ();
    state.route_channels.clear ();
    for (auto &[_, spot_node] : state.spot_nodes) {
        if (spot_node) {
            drain_spot_node_executors (*spot_node);
        }
        if (spot_node && spot_node->worker_executor) {
            spot_node->worker_executor->drain ();
        }
    }
    state.spot_nodes.clear ();
    state.runtime.reset ();
}

} // namespace

zlink_builder_state_t::~zlink_builder_state_t ()
{
    drain_zlink_builder_state_runtime (*this);
}

void drain_zlink_builder_runtime (zlink_builder_t &builder) noexcept
{
    if (!builder._state) {
        return;
    }
    drain_zlink_builder_state_runtime (*builder._state);
}

void bind_zlink_monitoring (zlink_builder_t &builder, const monitoring_builder_t &monitoring)
{
    if (!builder._state) {
        return;
    }
    auto state = monitoring_runtime_t::from (monitoring).state ();
    builder._state->runtime->monitoring = state;
    for (auto &[_, spot_node] : builder._state->spot_nodes) {
        if (spot_node) {
            spot_node->monitoring = state;
        }
    }
}

void apply_dispatch_options (zlink_builder_t &builder, const dispatch_options_t &options)
{
    builder._state->runtime->dispatch = options;
    for (auto &[_, spot_node] : builder._state->spot_nodes) {
        spot_node->dispatch = options;
    }
    builder._state->stream_runtime->dispatch = options;
}

result_t<zlink::message_t> channel_runtime_t::dispatch_request (std::string channel_name,
                                                                std::string topic,
                                                                std::string packet_name,
                                                                service_provider_t &services,
                                                                serializer_registry_t &serializers,
                                                                const handler_registry_t &handlers,
                                                                const zlink::message_t &message,
                                                                std::string_view content_type) const
{
    if (!is_enabled (server_capability (*_state, channel_name))) {
        return result_t<zlink::message_t>::failure (framework_error_kind_t::route_not_connected,
                                                    "channel server capability is not enabled");
    }
    return handlers.invoke (channel_name, topic, packet_name, services, serializers, message,
                            content_type);
}

result_t<void> channel_runtime_t::dispatch_send (std::string channel_name,
                                                 std::string topic,
                                                 std::string packet_name,
                                                 service_provider_t &services,
                                                 serializer_registry_t &serializers,
                                                 const handler_registry_t &handlers,
                                                 const zlink::message_t &message,
                                                 std::string_view content_type) const
{
    auto result = handlers.invoke (channel_name, topic, packet_name, services, serializers, message,
                                   content_type);
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
    result_t<pending_operation_controller_t::hook_dispatch_t> dispatch =
      result_t<pending_operation_controller_t::hook_dispatch_t>::failure (
        framework_error_kind_t::request_failed, "pending operation expire was not attempted");
    {
        std::lock_guard lock (_state->mutex);
        dispatch = pending_operation_controller_t (*_state).expire (operation_id);
    }
    if (!dispatch) {
        return result_t<void>::failure (dispatch.error_kind (),
                                        dispatch.error () ? dispatch.error ()->what ()
                                                          : "pending operation expire failed");
    }
    if (dispatch.value ().dead_letter_hook) {
        dispatch.value ().dead_letter_hook (dispatch.value ().event);
    }
    return result_t<void>::failure (framework_error_kind_t::timeout, "pending operation timed out");
}

result_t<void> channel_runtime_t::retry_pending (std::uint64_t operation_id)
{
    result_t<pending_operation_controller_t::hook_dispatch_t> dispatch =
      result_t<pending_operation_controller_t::hook_dispatch_t>::failure (
        framework_error_kind_t::request_failed, "pending operation retry was not attempted");
    {
        std::lock_guard lock (_state->mutex);
        dispatch = pending_operation_controller_t (*_state).retry (operation_id);
    }
    if (!dispatch) {
        return result_t<void>::failure (dispatch.error_kind (),
                                        dispatch.error () ? dispatch.error ()->what ()
                                                          : "pending operation retry failed");
    }
    if (dispatch.value ().retry_hook) {
        dispatch.value ().retry_hook (dispatch.value ().event);
    }
    return result_t<void>::success ();
}

void channel_runtime_t::close () noexcept
{
    {
        std::lock_guard lock (_state->mutex);
        _state->closed = true;
    }
    close_native_channel_transports (_state);
    drain ();
}

void channel_runtime_t::shutdown () noexcept
{
    std::vector<std::shared_ptr<route_channel_runtime_t>> route_channels;
    {
        std::lock_guard lock (_state->mutex);
        _state->shutdown = true;
        for (auto &[_, route_channel] : _state->route_channels) {
            if (route_channel) {
                route_channels.push_back (route_channel);
            }
        }
    }
    for (auto &route_channel : route_channels) {
        route_channel->stop ();
    }
    close_native_channel_transports (_state);
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

dispatch_options_t channel_runtime_t::dispatch_options () const
{
    return _state->dispatch;
}

void channel_runtime_t::mark_auto_connect_active ()
{
    std::lock_guard lock (_state->mutex);
    _state->auto_connect_active = true;
}

void channel_runtime_t::drain () noexcept
{
    std::lock_guard lock (_state->mutex);
    pending_operation_controller_t (*_state).drain ();
}

void channel_runtime_t::publish_socket_event (const std::string &channel_name,
                                              socket_event_kind_t event,
                                              std::string local_address,
                                              std::string remote_address,
                                              std::uint32_t native_event,
                                              std::uint32_t native_value) const
{
    if (!_state->monitoring) {
        return;
    }
    monitoring_runtime_t (_state->monitoring)
      .publish_socket (socket_event_payload_t{runtime_event_base_t{channel_name}, event,
                                              std::move (local_address), std::move (remote_address),
                                              native_event, native_value});
}

void channel_runtime_t::set_server_peer_weight (const std::string &channel_name,
                                                zlink::peer_weight_t value)
{
    {
        std::lock_guard lock (_state->mutex);
        _state->server_peer_weight_overrides.insert_or_assign (channel_name, value);
    }
    publish_socket_event (channel_name, socket_event_kind_t::peer_admission_changed, {}, {}, 0,
                          value.value ());
}

std::optional<zlink::peer_weight_t>
channel_runtime_t::server_peer_weight_override (const std::string &channel_name) const
{
    std::lock_guard lock (_state->mutex);
    const auto found = _state->server_peer_weight_overrides.find (channel_name);
    if (found == _state->server_peer_weight_overrides.end ()) {
        return std::nullopt;
    }
    return found->second;
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

runtime::messaging::message_parts_t
encode_route_payload_parts (runtime::messaging::envelope_header_t header,
                            std::type_index payload_type,
                            const route_client_t::payload_encoder_t &encode_payload,
                            serializer_registry_t &serializers)
{
    header.content_type = serializers.content_type (payload_type);
    runtime::messaging::envelope_codec_t envelope;
    return envelope.encode_raw_body_parts (
      header, detail::encoded_payload_to_raw (encode_payload (serializers)));
}

} // namespace


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
    snapshot.enabled = true;
    snapshot.bind_endpoints.push_back (std::move (endpoint));
    return *this;
}

capability_builder_t &capability_builder_t::connect (std::string endpoint)
{
    auto &snapshot = capability_snapshot (*_state);
    snapshot.discovery = false;
    snapshot.enabled = true;
    snapshot.connect_endpoints.push_back (std::move (endpoint));
    return *this;
}

capability_builder_t &capability_builder_t::set_routing_id (zlink::routing_id_t routing_id)
{
    auto &snapshot = capability_snapshot (*_state);
    snapshot.enabled = true;
    snapshot.routing_id = std::move (routing_id);
    return *this;
}

capability_builder_t &capability_builder_t::send_high_water_mark (zlink::message_count_t value)
{
    auto &snapshot = capability_snapshot (*_state);
    snapshot.enabled = true;
    snapshot.send_high_water_mark = value;
    return *this;
}

capability_builder_t &capability_builder_t::receive_high_water_mark (zlink::message_count_t value)
{
    auto &snapshot = capability_snapshot (*_state);
    snapshot.enabled = true;
    snapshot.receive_high_water_mark = value;
    return *this;
}

capability_builder_t &capability_builder_t::max_message_size (zlink::byte_size_t value)
{
    auto &snapshot = capability_snapshot (*_state);
    snapshot.enabled = true;
    snapshot.max_message_size = value;
    return *this;
}

capability_builder_t &capability_builder_t::peer_weight (zlink::peer_weight_t value)
{
    auto &snapshot = capability_snapshot (*_state);
    snapshot.enabled = true;
    snapshot.peer_weight = value;
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
    auto builder =
      enable_capability (detail::select_capability (*_state, channel_capability_t::server));
    detail::select_capability (*_state, channel_capability_t::server).discovery = true;
    return builder;
}

capability_builder_t channel_builder_t::enable_client ()
{
    auto builder =
      enable_capability (detail::select_capability (*_state, channel_capability_t::client));
    detail::select_capability (*_state, channel_capability_t::client).discovery = true;
    return builder;
}

capability_builder_t channel_builder_t::enable_publisher ()
{
    auto builder =
      enable_capability (detail::select_capability (*_state, channel_capability_t::publisher));
    detail::select_capability (*_state, channel_capability_t::publisher).discovery = true;
    return builder;
}

capability_builder_t channel_builder_t::enable_subscriber ()
{
    auto builder =
      enable_capability (detail::select_capability (*_state, channel_capability_t::subscriber));
    detail::select_capability (*_state, channel_capability_t::subscriber).discovery = true;
    return builder;
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

void
detail::connect_route_channel_peer (route_channel_builder_t &builder,
                                    zlink::routing_id_t peer_rid,
                                    std::string endpoint)
{
    builder._state->registration.connect (std::move (peer_rid), std::move (endpoint));
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
    _error (framework_error_kind_t::request_failed, ""),
    _reply (std::move (reply)),
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
                               payload_encoder_t encode_payload,
                               std::chrono::milliseconds timeout,
                               const channel_request_call_t::metadata_map_t &metadata)
{
    return detail::channel_outbound_exchange_t (_state).submit_request (
      std::move (channel_name), std::move (packet_name), request_type, std::move (encode_payload),
      timeout, metadata);
}

task_t<zlink::message_t>
message_bus_t::submit_request_message_async (std::string channel_name,
                                             std::string packet_name,
                                             std::type_index request_type,
                                             payload_encoder_t encode_payload,
                                             std::chrono::milliseconds timeout,
                                             channel_request_call_t::metadata_map_t metadata)
{
    auto result =
      submit_request (std::move (channel_name), std::move (packet_name), request_type,
                      std::move (encode_payload), timeout, metadata)
        .message ();
    return task_t<zlink::message_t> (std::move (result));
}

result_t<void> message_bus_t::submit_send (std::string channel_name,
                                           std::string packet_name,
                                           std::type_index message_type,
                                           payload_encoder_t encode_payload,
                                           std::chrono::milliseconds timeout,
                                           const send_call_t::metadata_map_t &metadata)
{
    return detail::channel_outbound_exchange_t (_state).submit_send (
      std::move (channel_name), std::move (packet_name), message_type, std::move (encode_payload),
      timeout, metadata);
}

result_t<void> message_bus_t::submit_publish (std::string channel_name,
                                              std::string topic,
                                              std::string packet_name,
                                              std::type_index event_type,
                                              payload_encoder_t encode_payload,
                                              std::chrono::milliseconds timeout,
                                              const send_call_t::metadata_map_t &metadata)
{
    return detail::channel_outbound_exchange_t (_state).submit_publish (
      std::move (channel_name), std::move (topic), std::move (packet_name), event_type,
      std::move (encode_payload), timeout, metadata);
}

channel_server_socket_runtime_options_t::channel_server_socket_runtime_options_t () = default;

channel_server_socket_runtime_options_t::channel_server_socket_runtime_options_t (
  std::shared_ptr<detail::channel_runtime_state_t> state, std::string channel_name) :
    _state (std::move (state)), _channel_name (std::move (channel_name))
{
}

channel_server_socket_runtime_options_t::~channel_server_socket_runtime_options_t () = default;

channel_server_socket_runtime_options_t::channel_server_socket_runtime_options_t (
  channel_server_socket_runtime_options_t &&) noexcept = default;

channel_server_socket_runtime_options_t &channel_server_socket_runtime_options_t::operator= (
  channel_server_socket_runtime_options_t &&) noexcept = default;

channel_server_socket_runtime_options_t &
channel_server_socket_runtime_options_t::peer_weight (zlink::peer_weight_t value)
{
    detail::channel_runtime_t (_state).set_server_peer_weight (_channel_name, value);
    return *this;
}

client_server_channel_runtime_options_t::client_server_channel_runtime_options_t () = default;

client_server_channel_runtime_options_t::client_server_channel_runtime_options_t (
  std::shared_ptr<detail::channel_runtime_state_t> state, std::string channel_name) :
    _state (std::move (state)), _channel_name (std::move (channel_name))
{
}

client_server_channel_runtime_options_t::~client_server_channel_runtime_options_t () = default;

client_server_channel_runtime_options_t::client_server_channel_runtime_options_t (
  client_server_channel_runtime_options_t &&) noexcept = default;

client_server_channel_runtime_options_t &client_server_channel_runtime_options_t::operator= (
  client_server_channel_runtime_options_t &&) noexcept = default;

channel_server_socket_runtime_options_t
client_server_channel_runtime_options_t::configure_server_socket () const
{
    return channel_server_socket_runtime_options_t (_state, _channel_name);
}

channel_runtime_options_t::channel_runtime_options_t () = default;

channel_runtime_options_t::channel_runtime_options_t (message_bus_t bus) : _state (bus._state)
{
}

channel_runtime_options_t::~channel_runtime_options_t () = default;

channel_runtime_options_t::channel_runtime_options_t (channel_runtime_options_t &&) noexcept =
  default;

channel_runtime_options_t &
channel_runtime_options_t::operator= (channel_runtime_options_t &&) noexcept = default;

client_server_channel_runtime_options_t
channel_runtime_options_t::client_server_channel (std::string channel_name) const
{
    return client_server_channel_runtime_options_t (_state, std::move (channel_name));
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

result_t<void> route_send_call_t::submit_now ()
{
    if (!_submit) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "route send call is not bound to a route client");
    }
    return _submit (_packet_name, _metadata);
}

result_t<void> route_send_call_t::submit ()
{
    return submit_now ();
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

result_t<void>
route_client_t::submit_send_erased (const std::shared_ptr<detail::route_client_state_t> &state,
                                    const std::string &router_channel_id,
                                    const zlink::routing_id_t &target_node_rid,
                                    const std::string &packet_name,
                                    std::type_index message_type,
                                    payload_encoder_t encode_payload,
                                    const route_send_call_t::metadata_map_t &metadata)
{
    if (!state || !state->runtime || state->serializers == nullptr) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "route client is not configured");
    }
    runtime::messaging::message_parts_t parts;
    try {
        detail::channel_runtime_manager_t manager (state->runtime);
        auto &runtime = manager.get_route_channel (router_channel_id);
        runtime::messaging::client_call_codec_t codec;
        auto header = codec.create_envelope (runtime::messaging::message_kind_t::command,
                                             router_channel_id, packet_name);
        header.metadata = metadata;
        detail::message_flow_tracer_t (state->runtime->dispatch)
          .trace (message_flow_outcome_t::sent, [&] {
              return message_flow_event_t{message_flow_outcome_t::sent,
                                          dispatch_error_surface_t::route_mesh_channel,
                                          dispatch_message_kind_t::send,
                                          packet_name,
                                          router_channel_id,
                                          std::nullopt,
                                          header.correlation_id,
                                          target_node_rid.to_string (),
                                          std::nullopt,
                                          std::nullopt,
                                          std::nullopt};
          });
        parts = encode_route_payload_parts (std::move (header), message_type, encode_payload,
                                            *state->serializers);
    }
    catch (const framework_exception_t &error) {
        return result_t<void>::failure (error.kind (), error.what (), error.is_retriable ());
    }
    try {
        detail::channel_runtime_manager_t manager (state->runtime);
        auto &runtime = manager.get_route_channel (router_channel_id);
        return runtime.submit_send_parts (target_node_rid, std::move (parts));
    }
    catch (const framework_exception_t &error) {
        return result_t<void>::failure (error.kind (), error.what (), error.is_retriable ());
    }
    catch (const std::exception &error) {
        return result_t<void>::failure (framework_error_kind_t::request_failed, error.what ());
    }
}

task_t<std::uint64_t>
route_client_t::submit_request_erased (const std::shared_ptr<detail::route_client_state_t> &state,
                                       const std::string &router_channel_id,
                                       const zlink::routing_id_t &target_node_rid,
                                       const std::string &packet_name,
                                       std::type_index request_type,
                                       payload_encoder_t encode_payload,
                                       std::chrono::milliseconds timeout,
                                       const channel_request_call_t::metadata_map_t &metadata)
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
        detail::message_flow_tracer_t (state->runtime->dispatch)
          .trace (message_flow_outcome_t::sent, [&] {
              return message_flow_event_t{message_flow_outcome_t::sent,
                                          dispatch_error_surface_t::route_mesh_channel,
                                          dispatch_message_kind_t::request,
                                          packet_name,
                                          router_channel_id,
                                          std::nullopt,
                                          header.correlation_id,
                                          target_node_rid.to_string (),
                                          std::nullopt,
                                          std::nullopt,
                                          std::nullopt};
          });
        parts = encode_route_payload_parts (std::move (header), request_type, encode_payload,
                                            *state->serializers);
    }
    catch (const framework_exception_t &error) {
        return task_t<std::uint64_t> (
          result_t<std::uint64_t>::failure (error.kind (), error.what (), error.is_retriable ()));
    }
    try {
        detail::channel_runtime_manager_t manager (state->runtime);
        auto &runtime = manager.get_route_channel (router_channel_id);
        return task_t<std::uint64_t> (runtime.submit_request_parts (target_node_rid,
                                                                    std::move (parts)));
    }
    catch (const framework_exception_t &error) {
        return task_t<std::uint64_t> (
          result_t<std::uint64_t>::failure (error.kind (), error.what (), error.is_retriable ()));
    }
    catch (const std::exception &error) {
        return task_t<std::uint64_t> (result_t<std::uint64_t>::failure (
          framework_error_kind_t::request_failed, error.what ()));
    }
}

result_t<void>
route_client_t::submit_spot_send_erased (const std::shared_ptr<detail::route_client_state_t> &state,
                                         const std::string &router_channel_id,
                                         const zlink::routing_id_t &target_node_rid,
                                         const spot_rid_t &target_spot_rid,
                                         const std::string &packet_name,
                                         std::type_index message_type,
                                         payload_encoder_t encode_payload,
                                         const route_send_call_t::metadata_map_t &metadata)
{
    if (!state || !state->runtime || state->serializers == nullptr) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "route client is not configured");
    }
    runtime::messaging::message_parts_t parts;
    const auto spot_rid = zlink::routing_id_t::from (std::string (target_spot_rid.value ()));
    try {
        detail::channel_runtime_manager_t manager (state->runtime);
        auto &runtime = manager.get_route_channel (router_channel_id);
        runtime::messaging::client_call_codec_t codec;
        auto header = codec.create_envelope (runtime::messaging::message_kind_t::command,
                                             router_channel_id, packet_name);
        header.metadata = metadata;
        detail::message_flow_tracer_t (state->runtime->dispatch)
          .trace (message_flow_outcome_t::sent, [&] {
              return message_flow_event_t{message_flow_outcome_t::sent,
                                          dispatch_error_surface_t::spot_route,
                                          dispatch_message_kind_t::send,
                                          packet_name,
                                          router_channel_id,
                                          std::nullopt,
                                          header.correlation_id,
                                          target_node_rid.to_string (),
                                          spot_rid.to_string (),
                                          std::nullopt,
                                          std::nullopt};
          });
        parts = encode_route_payload_parts (std::move (header), message_type, encode_payload,
                                            *state->serializers);
    }
    catch (const framework_exception_t &error) {
        return result_t<void>::failure (error.kind (), error.what (), error.is_retriable ());
    }
    try {
        detail::channel_runtime_manager_t manager (state->runtime);
        auto &runtime = manager.get_route_channel (router_channel_id);
        return runtime.submit_spot_send_parts (target_node_rid, spot_rid, std::move (parts));
    }
    catch (const framework_exception_t &error) {
        return result_t<void>::failure (error.kind (), error.what (), error.is_retriable ());
    }
    catch (const std::exception &error) {
        return result_t<void>::failure (framework_error_kind_t::request_failed, error.what ());
    }
}

task_t<std::uint64_t> route_client_t::submit_spot_request_erased (
  const std::shared_ptr<detail::route_client_state_t> &state,
  const std::string &router_channel_id,
  const zlink::routing_id_t &target_node_rid,
  const spot_rid_t &target_spot_rid,
  const std::string &packet_name,
  std::type_index request_type,
  payload_encoder_t encode_payload,
  std::chrono::milliseconds timeout,
  const channel_request_call_t::metadata_map_t &metadata)
{
    if (!state || !state->runtime || state->serializers == nullptr) {
        return task_t<std::uint64_t> (result_t<std::uint64_t>::failure (
          framework_error_kind_t::request_protocol_error, "route client is not configured"));
    }
    runtime::messaging::message_parts_t parts;
    const auto spot_rid = zlink::routing_id_t::from (std::string (target_spot_rid.value ()));
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
        detail::message_flow_tracer_t (state->runtime->dispatch)
          .trace (message_flow_outcome_t::sent, [&] {
              return message_flow_event_t{message_flow_outcome_t::sent,
                                          dispatch_error_surface_t::spot_route,
                                          dispatch_message_kind_t::request,
                                          packet_name,
                                          router_channel_id,
                                          std::nullopt,
                                          header.correlation_id,
                                          target_node_rid.to_string (),
                                          spot_rid.to_string (),
                                          std::nullopt,
                                          std::nullopt};
          });
        parts = encode_route_payload_parts (std::move (header), request_type, encode_payload,
                                            *state->serializers);
    }
    catch (const framework_exception_t &error) {
        return task_t<std::uint64_t> (
          result_t<std::uint64_t>::failure (error.kind (), error.what (), error.is_retriable ()));
    }
    try {
        detail::channel_runtime_manager_t manager (state->runtime);
        auto &runtime = manager.get_route_channel (router_channel_id);
        return task_t<std::uint64_t> (
          runtime.request_to_spot_parts (target_node_rid, spot_rid, std::move (parts)));
    }
    catch (const framework_exception_t &error) {
        return task_t<std::uint64_t> (
          result_t<std::uint64_t>::failure (error.kind (), error.what (), error.is_retriable ()));
    }
    catch (const std::exception &error) {
        return task_t<std::uint64_t> (result_t<std::uint64_t>::failure (
          framework_error_kind_t::request_failed, error.what ()));
    }
}

task_t<zlink::message_t> route_client_t::submit_request_reply_message_erased (
  const std::shared_ptr<detail::route_client_state_t> &state,
  std::string router_channel_id,
  zlink::routing_id_t target_node_rid,
  std::string packet_name,
  std::type_index request_type,
  payload_encoder_t encode_payload,
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
        detail::message_flow_tracer_t (state->runtime->dispatch)
          .trace (message_flow_outcome_t::sent, [&] {
              return message_flow_event_t{message_flow_outcome_t::sent,
                                          dispatch_error_surface_t::route_mesh_channel,
                                          dispatch_message_kind_t::request,
                                          packet_name,
                                          router_channel_id,
                                          std::nullopt,
                                          header.correlation_id,
                                          target_node_rid.to_string (),
                                          std::nullopt,
                                          std::nullopt,
                                          std::nullopt};
          });
        parts = encode_route_payload_parts (std::move (header), request_type, encode_payload,
                                            *state->serializers);
    }
    catch (const framework_exception_t &error) {
        return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
          error.kind (), error.what (), error.is_retriable ()));
    }
    auto source = std::make_shared<detail::task_completion_source_t<zlink::message_t>> ();
    auto output = source->task ();
    auto runtime_state = state->runtime;
    auto executor = state->executor;
    if (!executor
        || !executor->try_submit ([runtime_state, source,
                                   router_channel_id = std::move (router_channel_id),
                                   target_node_rid = std::move (target_node_rid),
                                   packet_name = std::move (packet_name), parts = std::move (parts),
                                   effective_timeout] () mutable {
              try {
                  detail::channel_runtime_manager_t manager (runtime_state);
                  auto &runtime = manager.get_route_channel (router_channel_id);
                  runtime::messaging::envelope_codec_t envelope;
                  auto reply = runtime.request_reply_parts (target_node_rid, std::move (parts),
                                                            effective_timeout);
                  if (!reply) {
                      source->complete (result_t<zlink::message_t>::failure (
                        reply.error_kind (),
                        reply.error () ? reply.error ()->what () : "route request failed"));
                      return;
                  }
                  auto reply_header = envelope.decode_header (reply.value ());
                  if (!reply_header) {
                      source->complete (result_t<zlink::message_t>::failure (
                        reply_header.error_kind (),
                        reply_header.error () ? reply_header.error ()->what ()
                                              : "route reply header decode failed"));
                      return;
                  }
                  if (reply_header.value ().kind == runtime::messaging::message_kind_t::error) {
                      source->complete (result_t<zlink::message_t>::failure (
                        framework_error_kind_t::request_failed,
                        reply_header.value ().error_message.value_or ("route request failed")));
                      return;
                  }
                  auto body = envelope.decode_body (reply.value ());
                  if (!body) {
                      source->complete (result_t<zlink::message_t>::failure (
                        body.error_kind (),
                        body.error () ? body.error ()->what () : "route reply body decode failed"));
                      return;
                  }
                  detail::message_flow_tracer_t (runtime_state->dispatch)
                    .trace (message_flow_outcome_t::reply_received, [&] {
                        return message_flow_event_t{message_flow_outcome_t::reply_received,
                                                    dispatch_error_surface_t::route_mesh_channel,
                                                    dispatch_message_kind_t::response,
                                                    packet_name,
                                                    router_channel_id,
                                                    std::nullopt,
                                                    reply_header.value ().correlation_id,
                                                    target_node_rid.to_string (),
                                                    std::nullopt,
                                                    std::nullopt,
                                                    std::nullopt};
                    });
                  source->complete (result_t<zlink::message_t>::success (body.value ()));
              }
              catch (const framework_exception_t &error) {
                  source->complete (result_t<zlink::message_t>::failure (
                    error.kind (), error.what (), error.is_retriable ()));
              }
              catch (const std::exception &error) {
                  source->complete (result_t<zlink::message_t>::failure (
                    framework_error_kind_t::request_failed, error.what ()));
              }
              catch (...) {
                  source->complete (result_t<zlink::message_t>::failure (
                    framework_error_kind_t::request_failed, "route request failed"));
              }
          })) {
        source->complete (result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_failed, "route client executor is stopped"));
    }
    return output;
}

task_t<zlink::message_t> route_client_t::submit_spot_request_reply_message_erased (
  const std::shared_ptr<detail::route_client_state_t> &state,
  std::string router_channel_id,
  zlink::routing_id_t target_node_rid,
  spot_rid_t target_spot_rid,
  std::string packet_name,
  std::type_index request_type,
  payload_encoder_t encode_payload,
  std::chrono::milliseconds timeout,
  std::map<std::string, std::string> metadata)
{
    if (!state || !state->runtime || state->serializers == nullptr) {
        return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_protocol_error, "route client is not configured"));
    }
    runtime::messaging::message_parts_t parts;
    auto effective_timeout = timeout;
    const auto spot_rid = zlink::routing_id_t::from (std::string (target_spot_rid.value ()));
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
        detail::message_flow_tracer_t (state->runtime->dispatch)
          .trace (message_flow_outcome_t::sent, [&] {
              return message_flow_event_t{message_flow_outcome_t::sent,
                                          dispatch_error_surface_t::spot_route,
                                          dispatch_message_kind_t::request,
                                          packet_name,
                                          router_channel_id,
                                          std::nullopt,
                                          header.correlation_id,
                                          target_node_rid.to_string (),
                                          spot_rid.to_string (),
                                          std::nullopt,
                                          std::nullopt};
          });
        parts = encode_route_payload_parts (std::move (header), request_type, encode_payload,
                                            *state->serializers);
    }
    catch (const framework_exception_t &error) {
        return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
          error.kind (), error.what (), error.is_retriable ()));
    }
    auto source = std::make_shared<detail::task_completion_source_t<zlink::message_t>> ();
    auto output = source->task ();
    auto runtime_state = state->runtime;
    auto executor = state->executor;
    if (!executor
        || !executor->try_submit ([runtime_state, source,
                                   router_channel_id = std::move (router_channel_id),
                                   target_node_rid = std::move (target_node_rid),
                                   spot_rid = std::move (spot_rid),
                                   packet_name = std::move (packet_name), parts = std::move (parts),
                                   effective_timeout] () mutable {
              try {
                  detail::channel_runtime_manager_t manager (runtime_state);
                  auto &runtime = manager.get_route_channel (router_channel_id);
                  runtime::messaging::envelope_codec_t envelope;
                  auto reply = runtime.request_reply_spot_parts (
                    target_node_rid, spot_rid, std::move (parts), effective_timeout);
                  if (!reply) {
                      source->complete (result_t<zlink::message_t>::failure (
                        reply.error_kind (),
                        reply.error () ? reply.error ()->what () : "route spot request failed"));
                      return;
                  }
                  auto reply_header = envelope.decode_header (reply.value ());
                  if (!reply_header) {
                      source->complete (result_t<zlink::message_t>::failure (
                        reply_header.error_kind (),
                        reply_header.error () ? reply_header.error ()->what ()
                                              : "route spot reply header decode failed"));
                      return;
                  }
                  if (reply_header.value ().kind == runtime::messaging::message_kind_t::error) {
                      source->complete (result_t<zlink::message_t>::failure (
                        framework_error_kind_t::request_failed,
                        reply_header.value ().error_message.value_or (
                          "route spot request failed")));
                      return;
                  }
                  auto body = envelope.decode_body (reply.value ());
                  if (!body) {
                      source->complete (result_t<zlink::message_t>::failure (
                        body.error_kind (),
                        body.error () ? body.error ()->what ()
                                      : "route spot reply body decode failed"));
                      return;
                  }
                  detail::message_flow_tracer_t (runtime_state->dispatch)
                    .trace (message_flow_outcome_t::reply_received, [&] {
                        return message_flow_event_t{message_flow_outcome_t::reply_received,
                                                    dispatch_error_surface_t::spot_route,
                                                    dispatch_message_kind_t::response,
                                                    packet_name,
                                                    router_channel_id,
                                                    std::nullopt,
                                                    reply_header.value ().correlation_id,
                                                    target_node_rid.to_string (),
                                                    spot_rid.to_string (),
                                                    std::nullopt,
                                                    std::nullopt};
                    });
                  source->complete (result_t<zlink::message_t>::success (body.value ()));
              }
              catch (const framework_exception_t &error) {
                  source->complete (result_t<zlink::message_t>::failure (
                    error.kind (), error.what (), error.is_retriable ()));
              }
              catch (const std::exception &error) {
                  source->complete (result_t<zlink::message_t>::failure (
                    framework_error_kind_t::request_failed, error.what ()));
              }
              catch (...) {
                  source->complete (result_t<zlink::message_t>::failure (
                    framework_error_kind_t::request_failed, "route spot request failed"));
              }
          })) {
        source->complete (result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_failed, "route client executor is stopped"));
    }
    return output;
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
    auto [entry, _] =
      _state->runtime->channels.insert_or_assign (state->snapshot.name, state->snapshot);
    state->target = &entry->second;
    return channel_builder_t (state);
}

route_channel_builder_t zlink_builder_t::route_channel (std::string route_channel_name)
{
    if (route_channel_name.empty ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "route channel name is required");
    }
    if (auto found = _state->route_channels.find (route_channel_name);
        found != _state->route_channels.end ()) {
        return route_channel_builder_t (found->second);
    }
    auto state = std::make_shared<detail::route_channel_builder_state_t> (route_channel_name);
    _state->route_channels[route_channel_name] = state;
    return route_channel_builder_t (state);
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

std::vector<std::string> zlink_builder_t::route_channels () const
{
    std::vector<std::string> result;
    result.reserve (_state->route_channels.size ());
    for (const auto &[name, _] : _state->route_channels) {
        result.push_back (name);
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
