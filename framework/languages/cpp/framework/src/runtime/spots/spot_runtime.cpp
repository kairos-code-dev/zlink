/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_runtime.hpp"

#include <zlink/framework/contracts/configuration/zlink_builder.hpp>

#include "runtime/channels/channel_runtime.hpp"
#include "runtime/dispatch/coroutine_executor.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace zlink::framework
{

namespace
{

bool is_blank (const std::string &value)
{
    return std::all_of (value.begin (), value.end (), [] (unsigned char ch) { return std::isspace (ch) != 0; });
}

} // namespace

node_rid_t::node_rid_t (std::string value) : _value (std::move (value))
{
}

node_rid_t node_rid_t::from_string (std::string value)
{
    return node_rid_t (std::move (value));
}

std::string_view node_rid_t::value () const noexcept
{
    return _value;
}

bool node_rid_t::empty () const noexcept
{
    return _value.empty ();
}

spot_rid_t::spot_rid_t (std::string value) : _value (std::move (value))
{
}

spot_rid_t spot_rid_t::from_string (std::string value)
{
    return spot_rid_t (std::move (value));
}

std::string_view spot_rid_t::value () const noexcept
{
    return _value;
}

bool spot_rid_t::empty () const noexcept
{
    return _value.empty ();
}

spot_context_t::erased_request_call_t::erased_request_call_t (framework_exception_t error) : _error (std::move (error))
{
}

spot_context_t::spot_context_t () : _state (std::make_shared<detail::spot_context_state_t> ())
{
}

spot_context_t::spot_context_t (std::shared_ptr<detail::spot_context_state_t> state) : _state (std::move (state))
{
}

spot_context_t::~spot_context_t () = default;
spot_context_t::spot_context_t (spot_context_t &&) noexcept = default;
spot_context_t &spot_context_t::operator= (spot_context_t &&) noexcept = default;

node_rid_t spot_context_t::node_rid () const
{
    return _state->node_rid;
}

spot_rid_t spot_context_t::spot_rid () const
{
    return _state->spot_rid;
}

std::string spot_context_t::spot_name () const
{
    return _state->spot_name;
}

spot_handler_registry_t spot_context_t::handlers ()
{
    return spot_handler_registry_t (_state);
}

send_call_t spot_context_t::publish_erased (std::string topic)
{
    _state->ordering_log.push_back ("publish:" + topic);
    return send_call_t (result_t<void>::success ());
}

send_call_t spot_context_t::send_to_erased (node_rid_t node_rid, spot_rid_t spot_rid)
{
    if (node_rid.empty () || spot_rid.empty ()) {
        return send_call_t (
          result_t<void>::failure (framework_error_kind_t::spot_route_not_found, "target spot route is empty"));
    }
    _state->ordering_log.push_back ("send_to:" + std::string (spot_rid.value ()));
    return send_call_t (result_t<void>::success ());
}

spot_context_t::erased_request_call_t spot_context_t::request_to_erased (node_rid_t node_rid, spot_rid_t spot_rid)
{
    if (node_rid.empty () || spot_rid.empty ()) {
        return erased_request_call_t (
          framework_exception_t (framework_error_kind_t::spot_route_not_found, "target spot route is empty"));
    }
    _state->ordering_log.push_back ("request_to:" + std::string (spot_rid.value ()));
    return erased_request_call_t (framework_exception_t (
      framework_error_kind_t::timeout, "spot-to-spot reply was not completed by the local test runtime"));
}

spot_context_t &spot_context_t::register_packet_erased (std::string packet_name, std::type_index payload_type)
{
    const auto duplicate =
      std::any_of (_state->packets.begin (), _state->packets.end (),
                   [&] (const spot_packet_descriptor_t &descriptor) { return descriptor.packet_name == packet_name; });
    if (duplicate) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "duplicate spot packet registration");
    }
    _state->packets.push_back (spot_packet_descriptor_t{std::move (packet_name), payload_type});
    return *this;
}

std::vector<spot_packet_descriptor_t> spot_context_t::packet_registry () const
{
    return _state->packets;
}

spot_handler_registry_t::spot_handler_registry_t () : _state (std::make_shared<detail::spot_context_state_t> ())
{
}

spot_handler_registry_t::spot_handler_registry_t (std::shared_ptr<detail::spot_context_state_t> state) :
    _state (std::move (state))
{
}

spot_handler_registry_t::~spot_handler_registry_t () = default;
spot_handler_registry_t::spot_handler_registry_t (spot_handler_registry_t &&) noexcept = default;
spot_handler_registry_t &spot_handler_registry_t::operator= (spot_handler_registry_t &&) noexcept = default;

spot_handler_registry_t &spot_handler_registry_t::add_handler_erased (spot_handler_kind_t kind,
                                                                      std::string packet_name,
                                                                      std::string topic,
                                                                      std::type_index handler_type,
                                                                      std::type_index payload_type,
                                                                      std::type_index actor_type,
                                                                      std::type_index reply_type,
                                                                      invoker_t invoker)
{
    const auto duplicate = std::any_of (_state->handlers.begin (), _state->handlers.end (),
                                        [&] (const spot_handler_descriptor_t &descriptor) {
                                            return descriptor.kind == kind && descriptor.packet_name == packet_name
                                                   && descriptor.topic == topic && descriptor.actor_type == actor_type;
                                        });
    if (duplicate) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "duplicate spot handler registration");
    }

    _state->handlers.push_back (spot_handler_descriptor_t{kind, std::move (packet_name), std::move (topic),
                                                          handler_type, payload_type, actor_type, reply_type});
    _state->handler_invokers.push_back (std::move (invoker));
    return *this;
}

std::vector<spot_handler_descriptor_t> spot_handler_registry_t::descriptors () const
{
    return _state->handlers;
}

task_t<zlink::message_t> spot_handler_registry_t::invoke_erased (spot_handler_kind_t kind,
                                                                 std::string_view packet_name,
                                                                 std::string_view topic,
                                                                 std::type_index actor_type,
                                                                 void *spot,
                                                                 void *actor,
                                                                 service_provider_t &services,
                                                                 serializer_registry_t &serializers,
                                                                 const zlink::message_t &message,
                                                                 const spot_actor_change_result_t *change_result,
                                                                 spot_actor_message_metadata_t metadata) const
{
    for (std::size_t index = 0; index < _state->handlers.size (); ++index) {
        const auto &descriptor = _state->handlers[index];
        if (descriptor.kind == kind && descriptor.packet_name == packet_name && descriptor.topic == topic
            && descriptor.actor_type == actor_type) {
            auto owned_message = message;
            const auto handler_index = index;
            return runtime::handler_coroutine_executor ().submit<zlink::message_t> (
              [this, handler_index, spot, actor, &services, &serializers, owned_message = std::move (owned_message),
               metadata = std::move (metadata),
               change_result] () -> boost::asio::awaitable<result_t<zlink::message_t>> {
                  try {
                      auto handler_task = _state->handler_invokers[handler_index](
                        spot, actor, services, serializers, owned_message, metadata, change_result);
                      co_return result_t<zlink::message_t>::success (
                        (co_await runtime::await_task_result (std::move (handler_task))).value ());
                  }
                  catch (const framework_exception_t &error) {
                      co_return result_t<zlink::message_t>::failure (error.kind (), error.what (),
                                                                     error.is_retriable ());
                  }
                  catch (...) {
                      co_return result_t<zlink::message_t>::failure (framework_error_kind_t::request_failed,
                                                                     "spot handler threw an exception");
                  }
              });
        }
    }
    return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (framework_error_kind_t::handler_not_found,
                                                                          "spot handler is not registered"));
}

spot_node_builder_t::spot_node_builder_t () : _state (std::make_shared<detail::spot_node_builder_state_t> (""))
{
}

spot_node_builder_t::spot_node_builder_t (std::shared_ptr<detail::spot_node_builder_state_t> state) :
    _state (std::move (state))
{
}

spot_node_builder_t::~spot_node_builder_t () = default;
spot_node_builder_t::spot_node_builder_t (spot_node_builder_t &&) noexcept = default;
spot_node_builder_t &spot_node_builder_t::operator= (spot_node_builder_t &&) noexcept = default;

spot_node_builder_t &spot_node_builder_t::bind (std::string endpoint)
{
    _state->snapshot.bind_endpoint = std::move (endpoint);
    return *this;
}

spot_node_builder_t &spot_node_builder_t::enable_router (std::string endpoint)
{
    _state->snapshot.router_bind_endpoint = endpoint;
    if (_state->snapshot.bind_endpoint.empty ()) {
        _state->snapshot.bind_endpoint = std::move (endpoint);
    }
    return *this;
}

spot_node_builder_t &spot_node_builder_t::enable_router (std::string endpoint, zlink::routing_id_t routing_id)
{
    enable_router (std::move (endpoint));
    _state->snapshot.router_routing_id = std::move (routing_id);
    return *this;
}

spot_node_builder_t &spot_node_builder_t::connect_router (std::string endpoint)
{
    if (endpoint.empty () || is_blank (endpoint)) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "SPOT router manual endpoint is required");
    }
    _state->snapshot.router_manual_connections.push_back (std::move (endpoint));
    return *this;
}

spot_node_builder_t &spot_node_builder_t::enable_pub_sub (std::string endpoint)
{
    _state->snapshot.pub_bind_endpoint = endpoint;
    if (_state->snapshot.bind_endpoint.empty ()) {
        _state->snapshot.bind_endpoint = std::move (endpoint);
    }
    return *this;
}

spot_node_builder_t &spot_node_builder_t::enable_pub_sub (std::string endpoint, zlink::routing_id_t routing_id)
{
    enable_pub_sub (std::move (endpoint));
    _state->snapshot.pub_routing_id = std::move (routing_id);
    return *this;
}

spot_node_builder_t &spot_node_builder_t::connect_pub_sub (std::string endpoint)
{
    if (endpoint.empty () || is_blank (endpoint)) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "SPOT pub/sub manual endpoint is required");
    }
    _state->snapshot.pub_sub_manual_connections.push_back (std::move (endpoint));
    return *this;
}

spot_node_builder_t &spot_node_builder_t::enable_actor_gateway ()
{
    _state->snapshot.actor_gateway_enabled = true;
    return *this;
}

spot_node_builder_t &spot_node_builder_t::use_discovery (std::string channel_name)
{
    if (channel_name.empty ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "spot discovery channel name is required");
    }
    _state->snapshot.discovery_channel_name = std::move (channel_name);
    return *this;
}

spot_node_builder_t &spot_node_builder_t::use_registry_spot_remote_addresses ()
{
    if (!_state->resolvers.empty ()) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "registry spot remote address resolver cannot be combined with custom spot resolvers");
    }
    _state->snapshot.registry_spot_remote_addresses_enabled = true;
    _state->snapshot.registry_spot_route_channel.reset ();
    return *this;
}

spot_node_builder_t &spot_node_builder_t::use_registry_spot_remote_addresses (std::string route_channel_name)
{
    if (route_channel_name.empty ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "registry spot remote address route channel is required");
    }
    if (!_state->resolvers.empty ()) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "registry spot remote address resolver cannot be combined with custom spot resolvers");
    }
    _state->snapshot.registry_spot_remote_addresses_enabled = true;
    _state->snapshot.registry_spot_route_channel = std::move (route_channel_name);
    return *this;
}

spot_node_builder_t &spot_node_builder_t::accept_routes_from_channel (std::string route_channel_name,
                                                                      std::vector<std::string> manual_connections)
{
    if (route_channel_name.empty () || is_blank (route_channel_name)) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "accepted SPOT route channel name is required");
    }
    for (const auto &endpoint : manual_connections) {
        if (endpoint.empty () || is_blank (endpoint)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "accepted SPOT route manual endpoint is required");
        }
    }
    const auto duplicate =
      std::any_of (_state->snapshot.accepted_route_channels.begin (), _state->snapshot.accepted_route_channels.end (),
                   [&] (const auto &accepted) { return accepted.channel_name == route_channel_name; });
    if (duplicate) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "duplicate accepted SPOT route channel");
    }
    _state->snapshot.accepted_route_channels.push_back (
      accepted_spot_route_channel_t{std::move (route_channel_name), std::move (manual_connections)});
    return *this;
}

spot_node_builder_t &spot_node_builder_t::attach_channel_client (std::string channel_name,
                                                                 std::vector<std::string> manual_connections)
{
    if (channel_name.empty () || is_blank (channel_name)) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "attached client/server channel client name is required");
    }
    for (const auto &endpoint : manual_connections) {
        if (endpoint.empty () || is_blank (endpoint)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "attached channel client manual endpoint is required");
        }
    }
    _state->snapshot.attached_channel_client_details.push_back (
      attached_channel_client_t{channel_name, std::move (manual_connections)});
    _state->snapshot.attached_channel_clients.push_back (std::move (channel_name));
    return *this;
}

spot_node_builder_t &spot_node_builder_t::attach_publisher (std::string channel_name,
                                                            std::vector<std::string> manual_connections)
{
    if (channel_name.empty () || is_blank (channel_name)) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "attached SPOT publisher channel name is required");
    }
    for (const auto &endpoint : manual_connections) {
        if (endpoint.empty () || is_blank (endpoint)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "attached SPOT publisher manual endpoint is required");
        }
    }
    _state->snapshot.attached_publisher_details.push_back (
      attached_publisher_t{channel_name, std::move (manual_connections)});
    _state->snapshot.attached_publishers.push_back (std::move (channel_name));
    return *this;
}

spot_node_builder_t &
spot_node_builder_t::add_spot_factory (std::string spot_name, std::type_index spot_type, bool entry_spot)
{
    const auto [_, inserted] = _state->spot_factories.emplace (spot_name, spot_type);
    if (!inserted) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "duplicate spot factory registration");
    }
    if (entry_spot) {
        if (_state->snapshot.entry_spot_name) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "entry spot is already registered");
        }
        _state->snapshot.entry_spot_name = spot_name;
    }
    _state->snapshot.spot_names.push_back (std::move (spot_name));
    return *this;
}

spot_node_builder_t &spot_node_builder_t::add_actor_factory_erased (std::string actor_type,
                                                                    std::type_index factory_type)
{
    const auto [_, inserted] = _state->actor_factories.emplace (actor_type, factory_type);
    if (!inserted) {
        throw framework_exception_t (framework_error_kind_t::actor_already_exists,
                                     "duplicate actor factory registration");
    }
    _state->snapshot.actor_types.push_back (std::move (actor_type));
    return *this;
}

spot_node_builder_t &
spot_node_builder_t::add_spot_resolver (std::string name,
                                        std::function<std::optional<spot_route_t> (spot_rid_t)> resolver)
{
    if (name.empty () || !resolver) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "spot resolver requires a name and callback");
    }
    if (_state->snapshot.registry_spot_remote_addresses_enabled) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "custom spot resolvers cannot be combined with registry spot remote address resolver");
    }
    const auto [_, inserted] = _state->resolvers.emplace (std::move (name), std::move (resolver));
    if (!inserted) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "duplicate spot resolver registration");
    }
    return *this;
}

spot_node_snapshot_t spot_node_builder_t::snapshot () const
{
    return _state->snapshot;
}

spot_context_t spot_node_builder_t::create_spot (std::string spot_name)
{
    return detail::spot_node_runtime_t (_state).create_spot (std::move (spot_name));
}

std::optional<std::string> spot_node_builder_t::spot_name_for (spot_rid_t spot_rid) const
{
    return detail::spot_node_runtime_t (_state).spot_name_for (std::move (spot_rid));
}

std::optional<spot_route_t> spot_node_builder_t::resolve_spot (spot_rid_t spot_rid) const
{
    return detail::spot_node_runtime_t (_state).resolve_spot (std::move (spot_rid));
}

zlink_builder_t &zlink_builder_t::spot_node (std::string spot_node_name,
                                             std::function<void (spot_node_builder_t &)> configure)
{
    auto state = std::make_shared<detail::spot_node_builder_state_t> (std::move (spot_node_name));
    spot_node_builder_t builder (state);
    if (configure) {
        configure (builder);
    }
    _state->spot_nodes[state->snapshot.name] = state;
    return *this;
}

std::vector<spot_node_snapshot_t> zlink_builder_t::spot_nodes () const
{
    std::vector<spot_node_snapshot_t> result;
    result.reserve (_state->spot_nodes.size ());
    for (const auto &[_, state] : _state->spot_nodes) {
        result.push_back (state->snapshot);
    }
    return result;
}

} // namespace zlink::framework

namespace zlink::framework::detail
{

spot_node_runtime_t::spot_node_runtime_t (std::shared_ptr<spot_node_builder_state_t> state) : _state (std::move (state))
{
}

spot_node_runtime_t spot_node_runtime_t::from (const spot_node_builder_t &builder)
{
    return spot_node_runtime_t (builder._state);
}

spot_context_t spot_node_runtime_t::create_spot (std::string spot_name)
{
    const auto found = _state->spot_factories.find (spot_name);
    if (found == _state->spot_factories.end ()) {
        throw framework_exception_t (framework_error_kind_t::spot_create_failed, "spot factory is not registered");
    }

    auto rid =
      spot_rid_t::from_string (_state->snapshot.name + ":" + spot_name + ":" + std::to_string (_state->next_spot_id++));
    _state->spot_rids_by_name[spot_name] = rid;
    _state->spot_names_by_rid[std::string (rid.value ())] = spot_name;

    auto context_state = std::make_shared<spot_context_state_t> ();
    context_state->node = _state;
    context_state->node_rid = node_rid_t::from_string (_state->snapshot.name);
    context_state->spot_rid = rid;
    context_state->spot_name = std::move (spot_name);
    return spot_context_t (context_state);
}

std::optional<std::string> spot_node_runtime_t::spot_name_for (spot_rid_t spot_rid) const
{
    const auto found = _state->spot_names_by_rid.find (std::string (spot_rid.value ()));
    if (found == _state->spot_names_by_rid.end ()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<spot_route_t> spot_node_runtime_t::resolve_spot (spot_rid_t spot_rid) const
{
    if (const auto name = spot_name_for (spot_rid)) {
        return spot_route_t{node_rid_t::from_string (_state->snapshot.name), std::move (spot_rid), *name};
    }
    for (const auto &[_, resolver] : _state->resolvers) {
        if (auto route = resolver (spot_rid)) {
            return route;
        }
    }
    return std::nullopt;
}

const std::vector<std::string> &spot_node_runtime_t::ordering_log (const spot_context_t &context) const
{
    return context._state->ordering_log;
}

} // namespace zlink::framework::detail
