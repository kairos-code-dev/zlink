/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/mesh/mesh_node_runtime.hpp"
#include "runtime/messaging/async_submit_runtime.hpp"

#include "runtime/channels/channel_runtime.hpp"
#include "runtime/mesh/mesh_metadata_codec.hpp"
#include "runtime/messaging/client_call_codec.hpp"
#include "runtime/messaging/request_failure_mapper.hpp"
#include "runtime/spots/spot_route_packets.hpp"

#include <zlink/framework/contracts/configuration/zlink_builder.hpp>
#include <zlink/framework/contracts/errors/error.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <utility>

namespace zlink::framework::detail
{
namespace
{

std::chrono::milliseconds one_way_send_timeout (const mesh_node_builder_state_t &state)
{
    return state.socket.send_timeout.value_or (std::chrono::seconds (1));
}

std::string node_submit_target (const zlink::routing_id_t &node)
{
    return "mesh:node:" + node.to_hex ();
}

std::string channel_submit_target (const std::string &channel)
{
    return "mesh:channel:" + channel;
}

std::string spot_submit_target (const zlink::routing_id_t &node,
                                const std::string &spot)
{
    return "mesh:spot:" + node.to_hex () + ":" + spot;
}

std::string actor_submit_target (const actor_ref_t &actor)
{
    return "mesh:actor:" + std::string (actor.node_rid ().value ()) + ":"
           + std::string (actor.actor_id ()) + ":"
           + std::to_string (actor.generation ());
}

std::string send_ready_target (const host::send_ready_data_t &ready)
{
    using kind_t = host::send_ready_data_t::destination_kind_t;
    switch (ready.destination_kind) {
        case kind_t::node:
            return node_submit_target (ready.target_node_rid);
        case kind_t::channel:
            return channel_submit_target (ready.channel_name);
        case kind_t::spot:
            return spot_submit_target (ready.target_node_rid, ready.target_spot_id);
        case kind_t::actor:
        case kind_t::bound_session:
            return actor_submit_target (ready.target_actor);
    }
    return {};
}

} // namespace

namespace
{

framework_exception_t configuration_error (std::string message)
{
    return framework_exception_t (framework_error_kind_t::request_protocol_error,
                                  std::move (message));
}

std::uint64_t next_connection_intent_id ()
{
    static std::atomic_uint64_t next{1};
    return next.fetch_add (1, std::memory_order_relaxed);
}

std::pair<std::uint64_t, std::uint64_t>
operation_key (const host::operation_id_t &operation)
{
    return {operation.high, operation.low};
}

void trace_mesh_actor (std::string_view stage,
                       const actor_ref_t &actor,
                       const host::operation_id_t &operation = {},
                       std::optional<int> result = std::nullopt)
{
    const auto *enabled = std::getenv ("ZLINK_CPP_AUTO_CONNECT_TRACE");
    if (enabled == nullptr || *enabled == '\0')
        return;
    std::cerr << "zlink mesh-actor stage=" << stage
              << " node=" << actor.node_rid ().value ()
              << " actor=" << actor.actor_id ()
              << " generation=" << actor.generation ()
              << " operation=" << operation.high << ":" << operation.low;
    if (result)
        std::cerr << " result=" << *result;
    std::cerr << '\n';
}

} // namespace

mesh_node_builder_state_t::mesh_node_builder_state_t (std::string name) :
    mesh_name (name),
    spot_state (std::make_shared<spot_node_builder_state_t> (name)),
    spot_builder (spot_state)
{
}

mesh_node_runtime_t::mesh_node_runtime_t (std::shared_ptr<mesh_node_builder_state_t> state) :
    _state (std::move (state))
{
    if (!_state) {
        throw configuration_error ("MeshNode registration is required");
    }
}

mesh_node_runtime_t::~mesh_node_runtime_t ()
{
    stop ();
}

void mesh_node_runtime_t::bind_serializers (serializer_registry_t &serializers) noexcept
{
    _serializers = &serializers;
}

void mesh_node_runtime_t::bind_descriptor_publisher (
  std::function<void (const std::map<std::string, int> &,
                      int,
                      std::uint64_t)> publisher)
{
    std::lock_guard lock (_state->mutex);
    _descriptor_publisher = std::move (publisher);
}

void mesh_node_runtime_t::start ()
{
    if (_node) {
        return;
    }
    runtime::messaging::activate_submit_owner (this);

    std::lock_guard lock (_state->mutex);
    _state->spot_state->one_way_send_timeout = one_way_send_timeout (*_state);
    if (_state->mesh_name.empty ()) {
        throw configuration_error ("MeshName is required");
    }
    if (_state->listen_endpoint.empty ()) {
        throw configuration_error ("MeshNode listen endpoint is required");
    }
    if (!_state->routing_id) {
        throw configuration_error ("MeshNode routing id is required");
    }
    if (_state->channels.empty ()) {
        throw configuration_error ("MeshNode requires at least one ChannelName");
    }
    if (_state->socket.send_timeout
        && (_state->socket.send_timeout->count () <= 0
            || _state->socket.send_timeout->count ()
                 > std::numeric_limits<int>::max ())) {
        throw configuration_error (
          "MeshNode send timeout must be between 1 and INT_MAX milliseconds");
    }

    std::vector<runtime::mesh::service_channel_descriptor_t> channels;
    channels.reserve (_state->channels.size ());
    for (const auto &[channel_name, channel] : _state->channels) {
        channels.push_back (
          runtime::mesh::service_channel_descriptor_t{
            channel_name, channel.weight});
    }
    std::sort (channels.begin (), channels.end (),
               [] (const auto &left, const auto &right) {
                   return left.name < right.name;
               });
    std::set<std::string> object_stable_types (
      _state->spot_state->snapshot.actor_types.begin (),
      _state->spot_state->snapshot.actor_types.end ());
    object_stable_types.insert ("framework.spot");
    auto node = std::make_shared<host::public_host_runtime_t> (
      host::host_options_t{
        runtime::mesh::raw_mesh_node_options_t{
          runtime::mesh::service_node_descriptor_t{
            .mesh_name = _state->mesh_name,
            .node_routing_id = _state->routing_id->to_bytes (),
            .lifecycle_generation = 1,
            .descriptor_revision = 1,
            .advertised_endpoint = _state->listen_endpoint,
            .channels = std::move (channels),
            .state = runtime::mesh::service_node_state_t::preparing,
            .effective_max_message_bytes =
              _state->socket.max_message_size > 0
                ? static_cast<std::uint32_t> (_state->socket.max_message_size)
                : 4u * 1024u * 1024u,
            .placement_weight = _state->placement_weight},
          _state->socket.mailbox_message_budget,
          _state->socket.mailbox_byte_budget,
          1024,
          4u * 1024u * 1024u},
        _state->spot_state->snapshot.entry_spot_name.value_or ("entry"),
        std::move (object_stable_types)});
    if (_user_spot_store && _user_spot_materializer) {
        node->configure_user_spot_operations (
          _user_spot_store, _user_spot_materializer);
    }
    if (_instance_spot_materializer) {
        node->configure_instance_spot_operations (
          _user_spot_store, _instance_spot_relocations,
          _instance_spot_owner, _instance_spot_materializer);
    }
    if (_session_route_owner_resolver)
        node->configure_session_route_owner (
          _session_route_owner_resolver);
    node->start ();
    if (_instance_spot_materializer)
        (void) node->recover_instance_spot_activations ();
    const auto resolved_endpoint = node->status ().local_endpoint ();
    if (!resolved_endpoint.empty ()) {
        _state->listen_endpoint = resolved_endpoint;
    }
    {
        std::lock_guard peer_lock (_peer_mutex);
        for (const auto &peer : _state->peer_connections) {
            const auto intent =
              peer.expected_routing_id
                ? node->connect_peer (peer.endpoint, *peer.expected_routing_id)
                : node->connect_peer (peer.endpoint);
            if (intent)
                _peer_connection_intents.emplace (
                  peer.endpoint, next_connection_intent_id ());
        }
    }
    _node = std::move (node);
    spot_node_runtime_t spot_runtime (_state->spot_state);
    spot_runtime.attach_native_node (_node);
    if (_state->spot_state->snapshot.entry_spot_name) {
        (void) spot_runtime.create_spot (
          *_state->spot_state->snapshot.entry_spot_name);
    }
}

void mesh_node_runtime_t::configure_user_spot_operations (
  std::shared_ptr<location_store_t> store,
  host::user_spot_materializer_t materializer)
{
    if (_node)
        throw configuration_error (
          "User Spot operations must be configured before MeshNode start");
    _user_spot_store = std::move (store);
    _user_spot_materializer = std::move (materializer);
}

void mesh_node_runtime_t::configure_instance_spot_operations (
  std::shared_ptr<location_store_t> store,
  std::shared_ptr<runtime::stateful::relocation_store_port_t> relocations,
  location_owner_token_t owner,
  host::instance_spot_activation_materializer_t materializer)
{
    if (_node)
        throw configuration_error (
          "Instance Spot operations must be configured before MeshNode start");
    if (!store || !relocations || owner.owner_id.empty ()
        || owner.lease_generation <= 0 || !materializer)
        throw configuration_error (
          "Instance Spot operations require Location and Relocation Stores, an owner lease, and a materializer");
    _user_spot_store = std::move (store);
    _instance_spot_relocations = std::move (relocations);
    _instance_spot_owner = std::move (owner);
    _instance_spot_materializer = std::move (materializer);
}

void mesh_node_runtime_t::configure_session_route_owner (
  std::function<std::optional<location_owner_token_t> ()>
    owner_resolver)
{
    if (!owner_resolver)
        throw configuration_error (
          "Session route owner resolver is required");
    _session_route_owner_resolver = std::move (owner_resolver);
    if (_node)
        _node->configure_session_route_owner (
          _session_route_owner_resolver);
}

bool mesh_node_runtime_t::activate_instance_spot_remote (
  const zlink::routing_id_t &target_node,
  runtime::protocol::instance_spot_activation_header_t request,
  std::optional<std::vector<std::uint8_t>> metadata,
  runtime::protocol::application_payload_t application_payload,
  std::chrono::milliseconds timeout,
  host::instance_spot_activation_completion_t completion)
{
    if (!_node)
        return false;
    return _node->activate_instance_spot_remote (
      target_node, std::move (request), std::move (metadata),
      std::move (application_payload), timeout,
      std::move (completion));
}

bool mesh_node_runtime_t::send_instance_spot_activation_remote (
  const zlink::routing_id_t &target_node,
  runtime::protocol::instance_spot_activation_header_t request,
  std::optional<std::vector<std::uint8_t>> metadata,
  runtime::protocol::application_payload_t application_payload)
{
    return _node && _node->send_instance_spot_activation_remote (
      target_node, std::move (request), std::move (metadata),
      std::move (application_payload));
}

void mesh_node_runtime_t::stop () noexcept
{
    runtime::messaging::shutdown_submit_owner (this);
    if (!_node) {
        return;
    }
    try {
        {
            std::lock_guard lock (_peer_mutex);
            _peer_connection_intents.clear ();
        }
        _actors.clear ();
        for (auto &[_, spot] : _spots)
            (void) spot.close ();
        _spots.clear ();
        spot_node_runtime_t (_state->spot_state)
          .detach_native_node ();
        _node->close ();
    }
    catch (...) {
    }
    _node.reset ();
}

void mesh_node_runtime_t::connect_peer (
  const zlink::routing_id_t &expected_routing_id,
  const std::string &endpoint)
{
    if (!_node || endpoint.empty ())
        return;
    std::lock_guard lock (_peer_mutex);
    if (_peer_connection_intents.contains (endpoint))
        return;
    if (_node->connect_peer (endpoint, expected_routing_id))
        _peer_connection_intents.emplace (
          endpoint, next_connection_intent_id ());
}

void mesh_node_runtime_t::disconnect_peer (const std::string &endpoint) noexcept
{
    if (!_node || endpoint.empty ())
        return;
    try {
        std::lock_guard lock (_peer_mutex);
        const auto found = _peer_connection_intents.find (endpoint);
        if (found == _peer_connection_intents.end ())
            return;
        _node->disconnect_peer (endpoint);
        _peer_connection_intents.erase (found);
    }
    catch (...) {
    }
}

host::spot_handle_t
mesh_node_runtime_t::get_or_create_spot (std::string spot_id)
{
    if (!_node) {
        throw configuration_error ("MeshNode has not started");
    }
    const auto &key = spot_id;
    if (const auto found = _spots.find (key); found != _spots.end ())
        return found->second;
    auto spot = _node->get_or_create_spot (spot_id);
    _spots.emplace (key, spot);
    return spot;
}

zlink::submit_result_t mesh_node_runtime_t::send_to_spot (
  const std::string &source_spot_id,
  const zlink::routing_id_t &target_node_rid,
  const std::string &target_spot_id,
  std::uint64_t target_spot_generation,
  const std::vector<zlink::message_t> &parts,
  std::vector<std::uint8_t> metadata)
{
    runtime::messaging::note_submit_attempt (
      spot_submit_target (target_node_rid, target_spot_id), this,
      one_way_send_timeout (*_state), _state->max_pending);
    return get_or_create_spot (source_spot_id)
      .send_to_spot (target_node_rid, target_spot_id,
                     target_spot_generation, parts,
                     zlink::send_flags_t::dontwait, metadata);
}

zlink::submit_result_t mesh_node_runtime_t::request_to_spot (
  const std::string &source_spot_id,
  const zlink::routing_id_t &target_node_rid,
  const std::string &target_spot_id,
  std::uint64_t target_spot_generation,
  const std::vector<zlink::message_t> &parts,
  host::operation_id_t &operation_id,
  std::chrono::milliseconds timeout,
  std::vector<std::uint8_t> metadata)
{
    return get_or_create_spot (source_spot_id)
      .request_to_spot (target_node_rid, target_spot_id,
                        target_spot_generation, parts, operation_id,
                        zlink::send_flags_t::none, timeout, metadata);
}

host::actor_handle_t mesh_node_runtime_t::create_actor (
  std::string actor_type,
  std::string actor_id,
  const std::vector<zlink::message_t> &creation_parts,
  std::chrono::milliseconds timeout)
{
    if (!_node) {
        throw configuration_error ("MeshNode has not started");
    }
    if (actor_id.empty ()) {
        throw configuration_error ("actor id is required");
    }
    if (const auto found = _actors.find (actor_id); found != _actors.end ())
        return found->second;
    (void) creation_parts;
    (void) timeout;
    auto actor = _node->create_actor (
      std::move (actor_type), actor_id);
    _actors.emplace (std::move (actor_id), actor);
    return actor;
}

zlink::submit_result_t mesh_node_runtime_t::send_to_actor (
  const actor_ref_t &target,
  const std::vector<zlink::message_t> &parts,
  std::vector<std::uint8_t> metadata)
{
    if (!_node) {
        throw configuration_error ("MeshNode has not started");
    }
    runtime::messaging::note_submit_attempt (
      actor_submit_target (target), this, one_way_send_timeout (*_state),
      _state->max_pending);
    return _node->send_to_actor (target, parts, metadata);
}

zlink::submit_result_t mesh_node_runtime_t::request_to_actor (
  const actor_ref_t &target,
  const std::vector<zlink::message_t> &parts,
  host::operation_id_t &operation_id,
  std::chrono::milliseconds timeout,
  std::vector<std::uint8_t> metadata)
{
    if (!_node) {
        throw configuration_error ("MeshNode has not started");
    }
    return _node->request_to_actor (
      target, parts, operation_id, timeout, metadata);
}

zlink::submit_result_t mesh_node_runtime_t::send_actor_bound_session (
  const actor_ref_t &actor,
  std::uint64_t expected_binding_generation,
  const std::vector<zlink::message_t> &parts)
{
    if (!_node) {
        throw configuration_error ("MeshNode has not started");
    }
    runtime::messaging::note_submit_attempt (
      actor_submit_target (actor), this, one_way_send_timeout (*_state),
      _state->max_pending);
    (void) expected_binding_generation;
    return _node->send_to_actor (actor, parts);
}

zlink::context_t &mesh_node_runtime_t::native_context ()
{
    if (!_node) {
        throw configuration_error ("MeshNode has not started");
    }
    return _node->transport ().context ();
}

std::size_t mesh_node_runtime_t::admitted_peer_count () const
{
    if (!_node) {
        return 0;
    }
    return _node->transport ().topology ().peers ().size ();
}

bool mesh_node_runtime_t::has_admitted_peer (
  const zlink::routing_id_t &peer_rid,
  std::uint64_t lifecycle_generation) const
{
    if (!_node || lifecycle_generation == 0)
        return false;
    const auto peer = _node->transport ().topology ().peer (
      peer_rid.to_bytes ());
    return peer
           && peer->descriptor.lifecycle_generation
                == lifecycle_generation
           && peer->descriptor.state
                == runtime::mesh::service_node_state_t::serving;
}

host::public_host_runtime_t &mesh_node_runtime_t::native_node ()
{
    if (!_node) {
        throw configuration_error ("MeshNode has not started");
    }
    return *_node;
}

bool mesh_node_runtime_t::prepare_actor_transfer (
  const host::actor_transfer_prepare_t &prepare,
  std::chrono::milliseconds timeout,
  host::actor_transfer_token_t &token,
  host::actor_transfer_prepare_result_t &result)
{
    if (!_node) {
        throw configuration_error ("MeshNode has not started");
    }
    (void) timeout;
    return _node->prepare_actor_transfer (prepare, token, result);
}

result_t<actor_ref_t> mesh_node_runtime_t::create_application_actor (
  std::string actor_type,
  std::string actor_id,
  const std::optional<zlink::message_t> &creation_payload,
  std::chrono::milliseconds timeout)
{
    try {
        std::vector<zlink::message_t> parts;
        if (creation_payload)
            parts.push_back (*creation_payload);
        auto native = create_actor (actor_type, actor_id, parts, timeout);
        {
            std::lock_guard<std::recursive_mutex> lock (_state->spot_state->mutex);
            _state->spot_state->actor_types_by_id[actor_id] = actor_type;
            _state->spot_state->mesh_runtime_owned_native_actor_ids.insert (actor_id);
            _state->spot_state->core_actor_membership_epochs.try_emplace (actor_id, 1);
        }
        return result_t<actor_ref_t>::success (native.ref ());
    }
    catch (const std::exception &error) {
        return result_t<actor_ref_t>::failure (framework_error_kind_t::request_failed,
                                               error.what ());
    }
}

result_t<actor_join_reply_t> mesh_node_runtime_t::join_application_actor_to_entry_spot (
  const actor_ref_t &actor,
  const node_rid_t &target_node,
  const zlink::message_t &request,
  std::chrono::milliseconds timeout)
{
    const auto found = _actors.find (std::string (actor.actor_id ()));
    if (found == _actors.end ()) {
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::actor_route_not_found, "local Actor handle was not found");
    }
    host::operation_id_t operation;
    const std::vector<zlink::message_t> parts{request};
    const auto submitted = found->second.join_entry_spot (
      zlink::routing_id_t::from (std::string (target_node.value ())), parts, operation, timeout);
    if (submitted != zlink::submit_result_t::ok) {
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::request_failed, "Actor entry Spot join was not submitted");
    }
    auto joined = wait_for_join_completion (operation, actor, timeout);
    if (joined && joined.value ().result_code == 0) {
        std::lock_guard<std::recursive_mutex> lock (_state->spot_state->mutex);
        ++_state->spot_state
            ->core_actor_membership_epochs[std::string (actor.actor_id ())];
    }
    return joined;
}

result_t<actor_join_reply_t> mesh_node_runtime_t::join_application_actor_to_spot (
  actor_ref_t actor,
  const node_rid_t &target_node,
  const spot_id_t &target_spot,
  std::uint64_t target_spot_generation,
  const zlink::message_t &request,
  std::chrono::milliseconds timeout,
  std::optional<zlink::routing_id_t> bound_session_node_rid,
  std::optional<zlink::routing_id_t> bound_session_rid)
{
    spot_node_runtime_t spot_runtime (_state->spot_state);
    const auto local_routing_id = routing_id ();
    const bool remote =
      local_routing_id
      && local_routing_id->to_hex ()
           != zlink::routing_id_t::from (std::string (target_node.value ())).to_hex ();
    if (!remote) {
        const auto found = _actors.find (std::string (actor.actor_id ()));
        if (found == _actors.end ()) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::actor_route_not_found,
              "local Actor handle was not found");
        }
        host::operation_id_t operation;
        const std::vector<zlink::message_t> parts{request};
        const auto submitted = found->second.join_spot (
          zlink::routing_id_t::from (std::string (target_node.value ())),
          target_spot,
          target_spot_generation, parts, operation, timeout);
        if (submitted != zlink::submit_result_t::ok) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::request_failed,
              "Actor Spot join was not submitted");
        }
        auto joined = wait_for_join_completion (operation, actor, timeout);
        if (joined && joined.value ().result_code == 0) {
            std::lock_guard<std::recursive_mutex> lock (_state->spot_state->mutex);
            ++_state->spot_state
                ->core_actor_membership_epochs[std::string (actor.actor_id ())];
        }
        return joined;
    }
    if (!_serializers) {
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "MeshNode serializers are not configured");
    }
    runtime::messaging::client_call_codec_t codec;
    auto request_route =
      [&] (const auto &route_request, std::string packet_name)
        -> result_t<runtime::messaging::message_parts_t> {
          const auto header = codec.create_envelope (
            runtime::messaging::message_kind_t::request, "spot",
            std::move (packet_name), timeout);
          auto encoded =
            codec.encode_envelope_parts (header, route_request, *_serializers);
          auto origin = get_or_create_spot (
            "__zlink-route-origin-" + routing_id ()->to_hex ());
          host::operation_id_t operation;
          const auto submitted = origin.request_to_spot (
            zlink::routing_id_t::from (std::string (target_node.value ())),
            target_spot,
            target_spot_generation, encoded.items (), operation,
            zlink::send_flags_t::none, timeout);
          if (submitted != zlink::submit_result_t::ok) {
              return result_t<runtime::messaging::message_parts_t>::failure (
                framework_error_kind_t::request_failed,
                "Actor transfer route request was not submitted");
          }
          auto completed = wait_for_completion (operation, timeout);
          if (!completed)
              return detail::propagate_failure<runtime::messaging::message_parts_t> (
                completed, "Actor transfer route request failed");
          if (completed.value ().record.terminal_result
              != static_cast<int> (zlink::request_result_t::ok)) {
              return result_t<runtime::messaging::message_parts_t>::failure (
                framework_error_kind_t::request_failed,
                "Actor transfer route request returned an error");
          }
          return result_t<runtime::messaging::message_parts_t>::success (
            runtime::messaging::message_parts_t (
              std::move (completed.value ().parts)));
      };

    const auto source_spot = spot_runtime.actor_spot (actor);
    if (!source_spot) {
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::actor_route_not_found,
          "source Actor is not joined to a local Spot");
    }
    const auto transfer_id = spot_runtime.next_actor_transfer_id ();
    const auto completion_operation_id_low =
      _state->next_join_completion_operation.fetch_add (
        1, std::memory_order_relaxed);
    const auto completion_operation_id_high =
      static_cast<std::uint64_t> (
        std::hash<std::string>{} (_state->mesh_name))
      | 1ULL;
    const auto admission_request = spot_actor_admission_route_request_t{
      .transfer_id = transfer_id,
      .actor_node_rid = std::string (actor.node_rid ().value ()),
      .actor_type = std::string (actor.actor_type ()),
      .actor_id = std::string (actor.actor_id ()),
      .actor_generation = actor.generation (),
      .completion_operation_id_high =
        completion_operation_id_high,
      .completion_operation_id_low =
        completion_operation_id_low,
      .source_spot_id = *source_spot,
      .target_spot_id = target_spot,
      .payload = request.to_bytes ()};
    auto admission_parts = request_route (
      admission_request, spot_actor_admission_route_request_t::packet_name);
    if (!admission_parts)
        return detail::propagate_failure<actor_join_reply_t> (
          admission_parts, "remote Actor admission failed");
    auto admission = codec.decode_envelope_reply<spot_actor_admission_route_reply_t> (
      admission_parts.value (), *_serializers,
      "remote Actor admission reply is empty",
      "remote Actor admission reply decode failed", "ActorTransferAdmission");
    if (!admission)
        return detail::propagate_failure<actor_join_reply_t> (
          admission, "remote Actor admission failed");
    if (!admission.value ().accepted) {
        return result_t<actor_join_reply_t>::success (
          actor_join_reply_t{
            1, actor, zlink::message_t::from (admission.value ().payload)});
    }

    auto prepared = spot_runtime.transfer_actor_out (actor, transfer_id);
    if (!prepared)
        return detail::propagate_failure<actor_join_reply_t> (
          prepared, "Actor transfer-out failed");
    auto left = spot_runtime.leave_actor_for_remote_transfer (actor);
    if (!left) {
        spot_runtime.fail_remote_actor_transfer (actor, false);
        return detail::propagate_failure<actor_join_reply_t> (
          left, "source Actor leave failed");
    }

    spot_runtime.emit_actor_transfer_marker (
      "commit_request", actor, transfer_id, target_spot, target_node);
    const auto prepare_request = spot_actor_commit_route_request_t{
      .transfer_id = transfer_id,
      .actor_node_rid = std::string (actor.node_rid ().value ()),
      .actor_type = std::string (actor.actor_type ()),
      .actor_id = std::string (actor.actor_id ()),
      .actor_generation = actor.generation (),
      .completion_root_reference =
        admission.value ().completion_root_reference,
      .completion_root_checksum =
        admission.value ().completion_root_checksum,
      .target_spot_id = target_spot,
      .transfer_state = prepared.value ().state.to_bytes (),
      .core_transfer = true,
      .prepare = true};
    auto prepare_parts = request_route (
      prepare_request, spot_actor_commit_route_request_t::packet_name);
    if (!prepare_parts) {
        spot_runtime.fail_remote_actor_transfer (actor, true);
        return detail::propagate_failure<actor_join_reply_t> (
          prepare_parts, "remote Actor prepare failed");
    }
    auto prepared_reply = codec.decode_envelope_reply<spot_actor_join_route_reply_t> (
      prepare_parts.value (), *_serializers,
      "remote Actor prepare reply is empty",
      "remote Actor prepare reply decode failed", "ActorTransferPrepare");
    if (!prepared_reply) {
        spot_runtime.fail_remote_actor_transfer (actor, true);
        return detail::propagate_failure<actor_join_reply_t> (
          prepared_reply, "remote Actor prepare failed");
    }

    const auto native_actor = actor;
    std::uint64_t membership_epoch = 1;
    {
        std::lock_guard<std::recursive_mutex> lock (_state->spot_state->mutex);
        const auto epoch = _state->spot_state->core_actor_membership_epochs.find (
          std::string (actor.actor_id ()));
        if (epoch != _state->spot_state->core_actor_membership_epochs.end ())
            membership_epoch = epoch->second;
    }
    host::actor_transfer_prepare_t core_prepare;
    core_prepare.role = host::actor_transfer_role_t::source;
    core_prepare.transfer_id = transfer_id;
    core_prepare.actor = native_actor;
    core_prepare.source_spot_id = *source_spot;
    core_prepare.target_spot_id = target_spot;
    core_prepare.target_spot_generation =
      target_spot_generation;
    core_prepare.target_node_rid =
      zlink::routing_id_t::from (std::string (target_node.value ()));
    host::actor_transfer_token_t core_token;
    host::actor_transfer_prepare_result_t core_result;
    const auto core_prepared =
      prepare_actor_transfer (core_prepare, timeout, core_token, core_result);
    if (!core_prepared) {
        spot_runtime.fail_remote_actor_transfer (actor, true);
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::request_failed,
          "source Framework Actor relocation prepare failed");
    }

    std::vector<spot_actor_handoff_packet_t> backlog;
    for (auto &packet : spot_runtime.take_actor_handoff_backlog (actor)) {
        backlog.push_back (spot_actor_handoff_packet_t{
          std::move (packet.packet_name), std::move (packet.payload),
          std::move (packet.content_type), std::move (packet.metadata),
          packet.is_request});
    }
    const auto finalize_request = spot_actor_commit_route_request_t{
      .transfer_id = transfer_id,
      .actor_node_rid = std::string (actor.node_rid ().value ()),
      .actor_type = std::string (actor.actor_type ()),
      .actor_id = std::string (actor.actor_id ()),
      .actor_generation = actor.generation (),
      .completion_root_reference =
        admission.value ().completion_root_reference,
      .completion_root_checksum =
        admission.value ().completion_root_checksum,
      .target_spot_id = target_spot,
      .bound_session_node_rid =
        bound_session_node_rid ? bound_session_node_rid->to_string () : std::string{},
      .bound_session_rid =
        bound_session_rid ? bound_session_rid->to_string () : std::string{},
      .transfer_state = prepared.value ().state.to_bytes (),
      .handoff_backlog = std::move (backlog),
      .core_transfer = true,
      .core_transfer_id_high = 0,
      .core_transfer_id_low = 0,
      .core_membership_epoch = membership_epoch,
      .core_final_sequence = 0,
      .core_reserve_message_count = 0,
      .core_reserve_byte_count = 0,
      .finalize = true};
    auto finalize_parts = request_route (
      finalize_request, spot_actor_commit_route_request_t::packet_name);
    if (!finalize_parts) {
        spot_runtime.fail_remote_actor_transfer (actor, true);
        return detail::propagate_failure<actor_join_reply_t> (
          finalize_parts, "remote Actor finalize failed");
    }
    auto finalized = codec.decode_envelope_reply<spot_actor_join_route_reply_t> (
      finalize_parts.value (), *_serializers,
      "remote Actor finalize reply is empty",
      "remote Actor finalize reply decode failed", "ActorTransferFinalize");
    if (!finalized) {
        spot_runtime.fail_remote_actor_transfer (actor, true);
        return detail::propagate_failure<actor_join_reply_t> (
          finalized, "remote Actor finalize failed");
    }
    const auto next_membership_epoch = membership_epoch + 1;
    const auto core_committed = core_token.commit (next_membership_epoch);
    if (!core_committed) {
        spot_runtime.fail_remote_actor_transfer (actor, true);
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::request_failed,
          "source Framework Actor relocation commit failed");
    }
    {
        std::lock_guard<std::recursive_mutex> lock (_state->spot_state->mutex);
        _state->spot_state->core_actor_membership_epochs[std::string (actor.actor_id ())]
          = next_membership_epoch;
    }
    const auto joined = actor_join_reply_from_spot_route (finalized.value ());
    spot_runtime.complete_remote_actor_transfer (
      actor, joined.actor,
      spot_route_t{target_node, target_spot, {}}, transfer_id);
    spot_runtime.emit_actor_transfer_marker (
      "commit_ack", actor, transfer_id, target_spot, target_node);
    spot_runtime.emit_actor_transfer_marker (
      "message_follow_registered", actor, transfer_id, target_spot, target_node);
    return result_t<actor_join_reply_t>::success (
      actor_join_reply_t{
        joined.result_code, joined.actor,
        zlink::message_t::from (admission.value ().payload)});
}

result_t<actor_join_reply_t> mesh_node_runtime_t::wait_for_join_completion (
  const host::operation_id_t &operation,
  const actor_ref_t &actor,
  std::chrono::milliseconds timeout)
{
    auto completed = wait_for_completion (operation, timeout);
    if (!completed) {
        return result_t<actor_join_reply_t>::failure (
          completed.error_kind (),
          completed.error () ? completed.error ()->what () : "Actor Spot join failed");
    }
    auto completion = std::move (completed.value ());
    if (!completion.record.join_completion) {
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "Actor Spot completion did not carry a join result (terminal result "
            + std::to_string (completion.record.terminal_result) + ", errno "
            + std::to_string (completion.record.failure_errno) + ")");
    }
    const auto &joined = *completion.record.join_completion;
    const auto reply = completion.parts.empty () ? zlink::message_t{} : completion.parts.front ();
    if (joined.join_result == host::join_admission_t::rejected) {
        return result_t<actor_join_reply_t>::success (
          actor_join_reply_t{1, actor, reply});
    }
    const auto &native = joined.current_actor;
    return result_t<actor_join_reply_t>::success (
      actor_join_reply_t{
        0,
        actor_ref_t (native.node_rid (), std::string (actor.actor_type ()),
                     std::string (native.actor_id ()), native.generation ()),
        reply});
}

result_t<std::optional<zlink::message_t>>
mesh_node_runtime_t::relay_application_actor (
  const actor_ref_t &actor,
  const stream_header_t &header,
  const zlink::message_t &payload,
  std::chrono::milliseconds timeout)
{
    runtime::messaging::client_call_codec_t codec;
    const auto kind =
      header.kind () == stream_message_kind_t::send
        ? runtime::messaging::message_kind_t::command
        : runtime::messaging::message_kind_t::request;
    auto envelope =
      codec.create_envelope (kind, "actor", std::string (header.packet_name ()), timeout);
    envelope.metadata = header.metadata ().values ();
    if (const auto correlation = header.correlation_id ())
        envelope.correlation_id = std::string (*correlation);
    return relay_application_actor (actor, envelope, payload, timeout);
}

result_t<std::optional<zlink::message_t>>
mesh_node_runtime_t::relay_application_actor (
  const actor_ref_t &actor,
  const runtime::messaging::envelope_header_t &header,
  const zlink::message_t &payload,
  std::chrono::milliseconds timeout)
{
    try {
        const auto kind = header.kind;
        auto encoded =
          runtime::messaging::envelope_codec_t{}.encode_raw_body_parts (header, payload);
        const auto native_actor = host::mesh_node_t::remote_actor_ref (
          zlink::routing_id_t::from (std::string (actor.node_rid ().value ())),
          std::string (actor.actor_id ()), actor.generation ());
        if (kind == runtime::messaging::message_kind_t::command) {
            const auto submitted = send_to_actor (native_actor, encoded.items ());
            if (submitted != zlink::submit_result_t::ok) {
                return result_t<std::optional<zlink::message_t>>::failure (
                  framework_error_kind_t::request_failed,
                  "Actor relay send was not accepted");
            }
            return result_t<std::optional<zlink::message_t>>::success (std::nullopt);
        }

        host::operation_id_t operation;
        const auto submitted =
          request_to_actor (native_actor, encoded.items (), operation, timeout);
        trace_mesh_actor (
          "remote-session-bind-submitted", actor, operation,
          static_cast<int> (submitted));
        if (submitted != zlink::submit_result_t::ok) {
            return result_t<std::optional<zlink::message_t>>::failure (
              framework_error_kind_t::request_failed,
              "Actor relay request was not accepted");
        }
        auto completed = wait_for_completion (operation, timeout);
        if (!completed) {
            return result_t<std::optional<zlink::message_t>>::failure (
              completed.error_kind (),
              completed.error () ? completed.error ()->what ()
                                  : "Actor relay request completion failed",
              completed.error () && completed.error ()->is_retriable ());
        }
        runtime::messaging::message_parts_t reply (std::move (completed.value ().parts));
        auto reply_header = runtime::messaging::envelope_codec_t{}.decode_header (reply);
        if (!reply_header) {
            return result_t<std::optional<zlink::message_t>>::failure (
              reply_header.error_kind (),
              reply_header.error () ? reply_header.error ()->what ()
                                     : "Actor relay reply header decode failed");
        }
        if (reply_header.value ().kind == runtime::messaging::message_kind_t::error) {
            const auto message =
              reply_header.value ().error_message.value_or ("Actor relay request failed");
            runtime::messaging::request_failure_mapper_t failure_mapper;
            const auto mapped = failure_mapper.error_header_exception (
              reply_header.value ().error_code.value_or ("request_failed"), message,
              "Actor relay request");
            return result_t<std::optional<zlink::message_t>>::failure (
              mapped.kind (), message, mapped.is_retriable ());
        }
        auto body = runtime::messaging::envelope_codec_t{}.decode_body (reply);
        if (!body)
            return result_t<std::optional<zlink::message_t>>::success (std::nullopt);
        return result_t<std::optional<zlink::message_t>>::success (
          std::make_optional (std::move (body.value ())));
    }
    catch (const framework_exception_t &error) {
        return detail::result_access_t::failure<std::optional<zlink::message_t>> (error);
    }
    catch (const std::exception &error) {
        return result_t<std::optional<zlink::message_t>>::failure (
          framework_error_kind_t::request_failed, error.what ());
    }
}

result_t<void> mesh_node_runtime_t::bind_application_actor_session (
  const actor_ref_t &actor,
  const node_rid_t &session_node,
  std::chrono::milliseconds timeout)
{
    if (!_serializers) {
        return result_t<void>::failure (
          framework_error_kind_t::request_protocol_error,
          "MeshNode serializers are not configured");
    }
    try {
        runtime::messaging::client_call_codec_t codec;
        auto header = codec.create_envelope (
          runtime::messaging::message_kind_t::request, "actor",
          actor_bound_session_bind_route_request_t::packet_name, timeout);
        auto request = actor_bound_session_bind_route_request_t{
          .actor_node_rid = std::string (actor.node_rid ().value ()),
          .actor_type = std::string (actor.actor_type ()),
          .actor_id = std::string (actor.actor_id ()),
          .actor_generation = actor.generation (),
          .session_node_rid = std::string (session_node.value ())};
        auto encoded =
          codec.encode_envelope_parts (header, request, *_serializers);
        const auto native_actor = host::mesh_node_t::remote_actor_ref (
          zlink::routing_id_t::from (std::string (actor.node_rid ().value ())),
          std::string (actor.actor_id ()), actor.generation ());
        host::operation_id_t operation;
        const auto submitted =
          request_to_actor (native_actor, encoded.items (), operation, timeout);
        if (submitted != zlink::submit_result_t::ok) {
            return result_t<void>::failure (
              framework_error_kind_t::actor_session_not_bound,
              "Remote Actor session binding was not accepted");
        }
        auto completed = wait_for_completion (operation, timeout);
        if (!completed) {
            return detail::propagate_failure<void> (
              completed, "Remote Actor session binding did not complete");
        }
        if (completed.value ().record.terminal_result
            != static_cast<int> (zlink::request_result_t::ok)) {
            return result_t<void>::failure (
              framework_error_kind_t::actor_session_not_bound,
              "Remote Actor session binding completed with result "
                + std::to_string (
                  completed.value ().record.terminal_result)
                + " (errno "
                + std::to_string (
                  completed.value ().record.failure_errno)
                + ")");
        }
        runtime::messaging::message_parts_t reply (
          std::move (completed.value ().parts));
        auto decoded =
          codec.decode_envelope_reply<actor_bound_session_route_reply_t> (
            reply, *_serializers, "Remote Actor session binding reply is empty",
            "Remote Actor session binding reply decode failed",
            "BindActorSession");
        if (!decoded) {
            return detail::propagate_failure<void> (
              decoded, "Remote Actor session binding failed");
        }
        return decoded.value ().accepted
                 ? result_t<void>::success ()
                 : result_t<void>::failure (
                     framework_error_kind_t::actor_session_not_bound,
                     "Remote Actor session binding was rejected");
    }
    catch (const framework_exception_t &error) {
        return detail::result_access_t::failure<void> (error);
    }
    catch (const std::exception &error) {
        return result_t<void>::failure (
          framework_error_kind_t::request_failed, error.what ());
    }
}

result_t<void> mesh_node_runtime_t::notify_application_actor_disconnected (
  const actor_ref_t &actor,
  const node_rid_t &target_node,
  std::chrono::milliseconds timeout)
{
    if (!_serializers) {
        return result_t<void>::failure (
          framework_error_kind_t::request_protocol_error,
          "MeshNode serializers are not configured");
    }
    try {
        runtime::messaging::client_call_codec_t codec;
        auto envelope = codec.create_envelope (
          runtime::messaging::message_kind_t::request, "spot",
          spot_actor_disconnect_route_request_t::packet_name, timeout);
        auto encoded = codec.encode_envelope_parts (
          envelope, make_spot_actor_disconnect_route_request (actor), *_serializers);
        host::operation_id_t operation;
        const auto submitted = request_to_node (
          zlink::routing_id_t::from (std::string (target_node.value ())),
          encoded.items (), operation, timeout);
        if (submitted != zlink::submit_result_t::ok) {
            return result_t<void>::failure (
              framework_error_kind_t::request_failed,
              "Actor disconnect notification was not submitted");
        }
        auto completed = wait_for_completion (operation, timeout);
        if (!completed) {
            return detail::propagate_failure<void> (
              completed, "Actor disconnect notification failed");
        }
        if (completed.value ().record.terminal_result
            != static_cast<int> (zlink::request_result_t::ok)) {
            return result_t<void>::failure (
              framework_error_kind_t::request_failed,
              "Actor disconnect notification returned an error");
        }
        runtime::messaging::message_parts_t reply (
          std::move (completed.value ().parts));
        auto decoded =
          codec.decode_envelope_reply<spot_actor_disconnect_route_reply_t> (
            reply, *_serializers, "Actor disconnect reply is empty",
            "Actor disconnect reply decode failed", "DisconnectActor");
        return decoded
                 ? result_t<void>::success ()
                 : detail::propagate_failure<void> (
                     decoded, "Actor disconnect notification failed");
    }
    catch (const framework_exception_t &error) {
        return detail::result_access_t::failure<void> (error);
    }
    catch (const std::exception &error) {
        return result_t<void>::failure (
          framework_error_kind_t::request_failed, error.what ());
    }
}

std::optional<actor_ref_t>
mesh_node_runtime_t::follow_relocated_actor (const actor_ref_t &actor)
{
    spot_node_runtime_t runtime (_state->spot_state);
    const auto follow_target = runtime.actor_message_follow_target (actor);
    if (!follow_target) {
        runtime.emit_actor_transfer_marker (
          "message_follow_expired", actor, {}, std::nullopt, std::nullopt);
        return std::nullopt;
    }
    runtime.emit_actor_transfer_marker (
      "message_follow_relay", actor, {}, follow_target->route.spot_id,
      follow_target->route.node_rid);
    return follow_target->actor;
}

result_t<mesh_node_runtime_t::operation_completion_t>
mesh_node_runtime_t::wait_for_completion (
  const host::operation_id_t &operation,
  std::chrono::milliseconds timeout)
{
    std::unique_lock lock (_completion_mutex);
    const auto key = operation_key (operation);
    if (!_completion_ready.wait_for (
          lock, timeout, [&] { return _completed_operations.find (key)
                                     != _completed_operations.end (); })) {
        return result_t<operation_completion_t>::failure (
          framework_error_kind_t::request_failed, "MeshNode operation timed out");
    }
    auto ready = _completed_operations.find (key);
    auto completion = std::move (ready->second);
    _completed_operations.erase (ready);
    return result_t<operation_completion_t>::success (std::move (completion));
}

zlink::submit_result_t
mesh_node_runtime_t::send_to_node (const zlink::routing_id_t &target,
                                   const std::vector<zlink::message_t> &parts,
                                   std::vector<std::uint8_t> metadata)
{
    if (!_node) {
        throw configuration_error ("MeshNode has not started");
    }
    runtime::messaging::note_submit_attempt (
      node_submit_target (target), this, one_way_send_timeout (*_state),
      _state->max_pending);
    (void) metadata;
    return _node->send_to_node (target, parts);
}

zlink::submit_result_t
mesh_node_runtime_t::send_to_node (const zlink::routing_id_t &target,
                                   const std::vector<zlink::message_t> &parts,
                                   const std::map<std::string, std::string> &metadata)
{
    const auto encoded = mesh_metadata_codec_t::encode (metadata);
    return send_to_node (target, parts, std::vector<std::uint8_t> (encoded));
}

zlink::submit_result_t mesh_node_runtime_t::request_to_node (
  const zlink::routing_id_t &target,
  const std::vector<zlink::message_t> &parts,
  host::operation_id_t &operation_id,
  std::chrono::milliseconds timeout,
  std::vector<std::uint8_t> metadata)
{
    if (!_node) {
        throw configuration_error ("MeshNode has not started");
    }
    (void) metadata;
    return _node->request_to_node (
      target, parts, operation_id, timeout);
}

zlink::submit_result_t mesh_node_runtime_t::request_to_node (
  const zlink::routing_id_t &target,
  const std::vector<zlink::message_t> &parts,
  host::operation_id_t &operation_id,
  std::chrono::milliseconds timeout,
  const std::map<std::string, std::string> &metadata)
{
    const auto encoded = mesh_metadata_codec_t::encode (metadata);
    return request_to_node (
      target, parts, operation_id, timeout, std::vector<std::uint8_t> (encoded));
}

zlink::submit_result_t
mesh_node_runtime_t::send_to_channel (const std::string &channel_name,
                                      const std::vector<zlink::message_t> &parts,
                                      std::vector<std::uint8_t> metadata)
{
    if (!_node) {
        throw configuration_error ("MeshNode has not started");
    }
    runtime::messaging::note_submit_attempt (
      channel_submit_target (channel_name), this, one_way_send_timeout (*_state),
      _state->max_pending);
    (void) metadata;
    return _node->send_to_channel (channel_name, parts);
}

zlink::submit_result_t
mesh_node_runtime_t::send_to_channel (const std::string &channel_name,
                                      const std::vector<zlink::message_t> &parts,
                                      const std::map<std::string, std::string> &metadata)
{
    const auto encoded = mesh_metadata_codec_t::encode (metadata);
    return send_to_channel (channel_name, parts, std::vector<std::uint8_t> (encoded));
}

zlink::submit_result_t mesh_node_runtime_t::request_to_channel (
  const std::string &channel_name,
  const std::vector<zlink::message_t> &parts,
  host::operation_id_t &operation_id,
  std::chrono::milliseconds timeout,
  std::vector<std::uint8_t> metadata)
{
    if (!_node) {
        throw configuration_error ("MeshNode has not started");
    }
    (void) metadata;
    return _node->request_to_channel (
      channel_name, parts, operation_id, timeout);
}

zlink::submit_result_t mesh_node_runtime_t::request_to_channel (
  const std::string &channel_name,
  const std::vector<zlink::message_t> &parts,
  host::operation_id_t &operation_id,
  std::chrono::milliseconds timeout,
  const std::map<std::string, std::string> &metadata)
{
    const auto encoded = mesh_metadata_codec_t::encode (metadata);
    return request_to_channel (
      channel_name, parts, operation_id, timeout, std::vector<std::uint8_t> (encoded));
}

std::size_t mesh_node_runtime_t::dispatch_ready (
  const std::function<void (const host::ready_record_t &,
                            const host::receive_record_t &,
                            std::vector<zlink::message_t>)> &dispatch)
{
    if (!dispatch)
        throw configuration_error ("MeshNode dispatch callback is required");

    return _node->dispatch_ready (
      [&] (const host::ready_record_t &ready_record,
           const host::receive_record_t &record,
           std::vector<zlink::message_t> parts) {
            if (record.kind == host::record_kind_t::completion) {
                if (const auto *enabled =
                      std::getenv ("ZLINK_CPP_AUTO_CONNECT_TRACE");
                    enabled != nullptr && *enabled != '\0') {
                    std::cerr
                      << "zlink mesh-completion operation="
                      << record.operation_id.high << ":"
                      << record.operation_id.low
                      << " terminal=" << record.terminal_result
                      << " errno=" << record.failure_errno
                      << " parts=" << parts.size () << '\n';
                }
                {
                    std::lock_guard lock (_completion_mutex);
                    _completed_operations[operation_key (record.operation_id)] =
                      operation_completion_t{record, parts};
                }
                _completion_ready.notify_all ();
            }
            if (record.kind == host::record_kind_t::send_ready
                && record.send_ready) {
                const auto target = send_ready_target (*record.send_ready);
                if (!target.empty ()) {
                    runtime::messaging::notify_submit_ready (target, this);
                }
            }
            dispatch (ready_record, record, std::move (parts));
      });
}

host::node_status_t mesh_node_runtime_t::status () const
{
    if (!_node) {
        throw configuration_error ("MeshNode has not started");
    }
    return _node->status ();
}

std::string mesh_node_runtime_t::mesh_name () const
{
    std::lock_guard lock (_state->mutex);
    return _state->mesh_name;
}

std::optional<zlink::routing_id_t> mesh_node_runtime_t::routing_id () const
{
    std::lock_guard lock (_state->mutex);
    return _state->routing_id;
}

std::string mesh_node_runtime_t::listen_endpoint () const
{
    std::lock_guard lock (_state->mutex);
    return _state->listen_endpoint;
}

std::map<std::string, int> mesh_node_runtime_t::channel_weights () const
{
    std::lock_guard lock (_state->mutex);
    std::map<std::string, int> result;
    for (const auto &[name, registration] : _state->channels)
        result.emplace (name, registration.weight);
    return result;
}

int mesh_node_runtime_t::placement_weight () const
{
    std::lock_guard lock (_state->mutex);
    return _state->placement_weight;
}

std::int32_t mesh_node_runtime_t::actor_limit () const
{
    std::lock_guard lock (_state->mutex);
    return _state->actor_limit;
}

std::int32_t mesh_node_runtime_t::spot_limit () const
{
    std::lock_guard lock (_state->mutex);
    return _state->spot_limit;
}

std::int32_t
mesh_node_runtime_t::activation_concurrency_limit () const
{
    std::lock_guard lock (_state->mutex);
    return _state->activation_concurrency_limit;
}

void mesh_node_runtime_t::set_placement_weight (int weight)
{
    if (!_node)
        throw configuration_error ("MeshNode has not started");
    if (weight < 0 || weight > 10000)
        throw configuration_error (
          "placement weight must be in range 0..10000");
    auto descriptor =
      native_node ().transport ().topology ()
        .local_descriptor ();
    if (descriptor.descriptor_revision
          == std::numeric_limits<std::uint64_t>::max ())
        throw configuration_error (
          "MeshNode descriptor revision is exhausted");
    descriptor.placement_weight = weight;
    ++descriptor.descriptor_revision;
    std::function<void (const std::map<std::string, int> &,
                        int,
                        std::uint64_t)> publisher;
    std::map<std::string, int> channel_weights;
    {
        std::lock_guard lock (_state->mutex);
        publisher = _descriptor_publisher;
        for (const auto &[name, registration] : _state->channels)
            channel_weights.emplace (name, registration.weight);
    }
    if (publisher)
        publisher (channel_weights, weight, descriptor.descriptor_revision);
    native_node ().transport ().topology ().publish_local (
      std::move (descriptor));
    std::lock_guard lock (_state->mutex);
    _state->placement_weight = weight;
}

std::size_t mesh_node_runtime_t::max_pending () const noexcept
{
    return _state->max_pending;
}

void mesh_node_runtime_t::set_channel_weight (const std::string &channel_name,
                                              int weight)
{
    if (!_node)
        throw configuration_error ("MeshNode has not started");
    if (weight < 0 || weight > 10000)
        throw configuration_error (
          "channel weight must be in range 0..10000");
    auto descriptor =
      native_node ().transport ().topology ().local_descriptor ();
    const auto descriptor_channel = std::find_if (
      descriptor.channels.begin (), descriptor.channels.end (),
      [&] (const auto &candidate) { return candidate.name == channel_name; });
    if (descriptor_channel == descriptor.channels.end ())
        throw configuration_error ("RouteMesh channel is not configured: "
                                   + mesh_name () + "/" + channel_name);
    descriptor_channel->weight = weight;
    if (descriptor.descriptor_revision
          == std::numeric_limits<std::uint64_t>::max ())
        throw configuration_error (
          "MeshNode descriptor revision is exhausted");
    ++descriptor.descriptor_revision;
    std::function<void (const std::map<std::string, int> &,
                        int,
                        std::uint64_t)> publisher;
    std::map<std::string, int> channel_weights;
    int placement_weight = 100;
    {
        std::lock_guard lock (_state->mutex);
        const auto found = _state->channels.find (channel_name);
        if (found == _state->channels.end ())
            throw configuration_error ("RouteMesh channel is not configured: "
                                       + _state->mesh_name + "/" + channel_name);
        for (const auto &[name, registration] : _state->channels)
            channel_weights.emplace (
              name, name == channel_name ? weight : registration.weight);
        placement_weight = _state->placement_weight;
        publisher = _descriptor_publisher;
    }
    if (publisher)
        publisher (
          channel_weights, placement_weight, descriptor.descriptor_revision);
    native_node ().transport ().topology ().publish_local (
      std::move (descriptor));
    std::lock_guard lock (_state->mutex);
    _state->channels.at (channel_name).weight = weight;
}

void mesh_node_runtime_t::application_work_enqueued () noexcept
{
    _pending_application_callbacks.fetch_add (1, std::memory_order_relaxed);
}

void mesh_node_runtime_t::application_work_started () noexcept
{
    _pending_application_callbacks.fetch_sub (1, std::memory_order_relaxed);
    _active_application_callbacks.fetch_add (1, std::memory_order_relaxed);
}

void mesh_node_runtime_t::application_work_finished () noexcept
{
    _active_application_callbacks.fetch_sub (1, std::memory_order_relaxed);
}

std::uint64_t mesh_node_runtime_t::pending_application_callbacks () const noexcept
{
    return _pending_application_callbacks.load (std::memory_order_relaxed);
}

std::uint64_t mesh_node_runtime_t::active_application_callbacks () const noexcept
{
    return _active_application_callbacks.load (std::memory_order_relaxed);
}

std::shared_ptr<mesh_node_runtime_t>
mesh_node_runtime_t::from (zlink_builder_t &builder, const std::string &mesh_name)
{
    const auto found = builder._state->mesh_nodes.find (mesh_name);
    if (found == builder._state->mesh_nodes.end ()) {
        return {};
    }
    return std::make_shared<mesh_node_runtime_t> (found->second);
}

std::vector<std::shared_ptr<mesh_node_builder_state_t>>
mesh_node_runtime_t::registrations (zlink_builder_t &builder)
{
    std::vector<std::shared_ptr<mesh_node_builder_state_t>> registrations;
    registrations.reserve (builder._state->mesh_nodes.size ());
    for (const auto &[_, registration] : builder._state->mesh_nodes)
        registrations.push_back (registration);
    return registrations;
}

} // namespace zlink::framework::detail

namespace zlink::framework
{

mesh_peer_connections_t::mesh_peer_connections_t (
  std::shared_ptr<detail::mesh_node_builder_state_t> state) :
    _state (std::move (state))
{
}

void mesh_peer_connections_t::connect (std::string endpoint)
{
    if (endpoint.empty ()) {
        throw detail::configuration_error ("peer endpoint is required");
    }
    std::lock_guard lock (_state->mutex);
    _state->peer_connections.push_back (
      mesh_peer_connection_t{detail::next_connection_intent_id (), {}, std::move (endpoint)});
}

void mesh_peer_connections_t::connect (zlink::routing_id_t expected_routing_id,
                                       std::string endpoint)
{
    if (endpoint.empty ()) {
        throw detail::configuration_error ("peer endpoint is required");
    }
    std::lock_guard lock (_state->mutex);
    _state->peer_connections.push_back (mesh_peer_connection_t{
      detail::next_connection_intent_id (), std::move (expected_routing_id), std::move (endpoint)});
}

void mesh_peer_connections_t::disconnect (std::string endpoint)
{
    std::lock_guard lock (_state->mutex);
    std::erase_if (_state->peer_connections,
                   [&endpoint] (const mesh_peer_connection_t &connection) {
                       return connection.endpoint == endpoint;
                   });
}

std::vector<mesh_peer_connection_t> mesh_peer_connections_t::list_connections () const
{
    std::lock_guard lock (_state->mutex);
    return _state->peer_connections;
}

mesh_channel_builder_t::mesh_channel_builder_t (
  std::shared_ptr<detail::mesh_node_builder_state_t> state, std::string channel_name) :
    _state (std::move (state)), _channel_name (std::move (channel_name))
{
}

mesh_channel_builder_t &mesh_channel_builder_t::set_weight (int weight)
{
    if (weight < 0 || weight > 10000) {
        throw detail::configuration_error (
          "ChannelName weight must be in range 0..10000");
    }
    std::lock_guard lock (_state->mutex);
    _state->channels[_channel_name].weight = weight;
    return *this;
}

mesh_channel_builder_t &mesh_channel_builder_t::use_handler_group (std::string group_name)
{
    if (group_name.empty ()) {
        throw detail::configuration_error ("handler group name is required");
    }
    std::lock_guard lock (_state->mutex);
    _state->channels[_channel_name].handler_group = std::move (group_name);
    return *this;
}

mesh_channel_builder_t &mesh_channel_builder_t::add_handler_registration (
  detail::mesh_handler_registration_t registration)
{
    detail::route_handler_descriptor_t descriptor{
      registration.request ? runtime::messaging::message_kind_t::request
                           : runtime::messaging::message_kind_t::command,
      registration.dispatch_name,
      registration.packet_name,
      registration.owner_type,
      registration.message_type,
      registration.reply_type};
    std::lock_guard lock (_state->mutex);
    _state->handlers.add_handler (std::move (descriptor), std::move (registration.invoke));
    return *this;
}

mesh_node_builder_t::mesh_node_builder_t (
  std::shared_ptr<detail::mesh_node_builder_state_t> state) :
    _state (std::move (state)), _peer_connections (_state)
{
}

mesh_channel_builder_t mesh_node_builder_t::channel_name (std::string channel_name)
{
    if (channel_name.empty ()) {
        throw detail::configuration_error ("ChannelName is required");
    }
    std::function<void (const std::string &)> observer;
    {
        std::lock_guard lock (_state->mutex);
        _state->channels.try_emplace (channel_name);
        observer = _state->channel_name_observer;
    }
    if (observer) {
        observer (channel_name);
    }
    return mesh_channel_builder_t (_state, std::move (channel_name));
}

mesh_node_builder_t &mesh_node_builder_t::listen (std::string endpoint)
{
    if (endpoint.empty ()) {
        throw detail::configuration_error ("MeshNode listen endpoint is required");
    }
    std::lock_guard lock (_state->mutex);
    _state->listen_endpoint = std::move (endpoint);
    return *this;
}

mesh_node_builder_t &mesh_node_builder_t::set_routing_id (zlink::routing_id_t routing_id)
{
    std::lock_guard lock (_state->mutex);
    _state->spot_state->snapshot.routing_id = routing_id;
    _state->routing_id = std::move (routing_id);
    return *this;
}

mesh_node_builder_t &
mesh_node_builder_t::set_placement_weight (int weight)
{
    if (weight < 0 || weight > 10000)
        throw detail::configuration_error (
          "placement weight must be in range 0..10000");
    std::lock_guard lock (_state->mutex);
    _state->placement_weight = weight;
    return *this;
}

mesh_node_builder_t &
mesh_node_builder_t::set_actor_limit (std::int32_t limit)
{
    if (limit < 0)
        throw detail::configuration_error (
          "Actor capacity limit must be non-negative");
    std::lock_guard lock (_state->mutex);
    _state->actor_limit = limit;
    return *this;
}

mesh_node_builder_t &
mesh_node_builder_t::set_spot_limit (std::int32_t limit)
{
    if (limit < 0)
        throw detail::configuration_error (
          "Spot capacity limit must be non-negative");
    std::lock_guard lock (_state->mutex);
    _state->spot_limit = limit;
    return *this;
}

mesh_node_builder_t &
mesh_node_builder_t::set_activation_concurrency (
  std::int32_t limit)
{
    if (limit <= 0)
        throw detail::configuration_error (
          "Activation concurrency limit must be positive");
    std::lock_guard lock (_state->mutex);
    _state->activation_concurrency_limit = limit;
    return *this;
}

mesh_node_socket_config_t &mesh_node_builder_t::configure_router_socket ()
{
    return _state->socket;
}

mesh_peer_connections_t &mesh_node_builder_t::peer_connections ()
{
    return _peer_connections;
}

mesh_node_builder_t &
mesh_node_builder_t::set_default_request_timeout (std::chrono::milliseconds timeout)
{
    if (timeout <= std::chrono::milliseconds::zero ()) {
        throw detail::configuration_error ("request timeout must be greater than zero");
    }
    std::lock_guard lock (_state->mutex);
    _state->default_request_timeout = timeout;
    return *this;
}

spot_node_builder_t &mesh_node_builder_t::spot_builder ()
{
    return _state->spot_builder;
}

std::string mesh_node_builder_t::route_dispatch_name () const
{
    std::lock_guard lock (_state->mutex);
    return _state->mesh_name;
}

} // namespace zlink::framework
