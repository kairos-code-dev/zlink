/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/mesh/mesh_node_host_service.hpp"

#include "runtime/channels/route_handler_registry.hpp"
#include "runtime/channels/channel_reply_writer.hpp"
#include "runtime/mesh/mesh_record_dispatcher.hpp"
#include "runtime/mesh/user_spot_terminal_mapping.hpp"
#include "runtime/messaging/envelope_codec.hpp"
#include "runtime/locations/pending_creation_projection.hpp"
#include "runtime/locations/location_runtime.hpp"
#include "runtime/locations/sha256.hpp"
#include "runtime/locations/source_creation_cleanup.hpp"
#include "runtime/spots/spot_route_packets.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>
#include <utility>

namespace zlink::framework::runtime
{

namespace
{

std::atomic_uint64_t next_user_spot_operation{1};

void trace_mesh_host_stop (const char *stage)
{
    const char *value = std::getenv ("ZLINK_CPP_HOST_STOP_TRACE");
    if (value != nullptr && std::string_view (value) != "" && std::string_view (value) != "0")
        std::cerr << "zlink-cpp-host-stop stage=mesh-host-" << stage << std::endl;
}

void reject_application_request (
  const host::receive_record_t &record,
                                 std::vector<zlink::message_t> parts)
{
    const bool request = record.kind == host::record_kind_t::node_request
                         || record.kind == host::record_kind_t::channel_request
                         || record.kind == host::record_kind_t::spot_request
                         || record.kind == host::record_kind_t::actor_request;
    if (!request)
        return;
    messaging::message_parts_t encoded (std::move (parts));
    messaging::envelope_codec_t codec;
    const auto header = codec.decode_header (encoded);
    if (!header)
        return;
    detail::channel_reply_writer_t replies;
    auto reply = replies.reply_raw_envelope (
      replies.create_error_header (
        header.value ().channel_name, header.value (),
        framework_exception_t (framework_error_kind_t::request_rejected,
                               "MeshNode is draining and rejects new application work")),
      zlink::message_t::from (""));
    (void) host::reply (record.reply_token, reply.items ());
}

} // namespace

mesh_node_host_service_t::mesh_node_host_service_t (
  std::vector<std::shared_ptr<detail::mesh_node_builder_state_t>> registrations,
  serializer_registry_t &serializers,
  dispatch_options_t dispatch_options) :
    _registrations (std::move (registrations)),
    _serializers (&serializers),
    _dispatch_options (std::move (dispatch_options)),
    _application_dispatch (std::make_unique<offload_executor_t> (
      0, std::max<std::size_t> (2, std::thread::hardware_concurrency ()), 4096,
      std::chrono::milliseconds (100), "zlink-mesh-app"))
{
    detail::register_spot_route_packet_serializers (serializers);
    _nodes.reserve (_registrations.size ());
    for (const auto &registration : _registrations) {
        auto node = std::make_shared<detail::mesh_node_runtime_t> (registration);
        node->bind_serializers (serializers);
        _nodes.push_back (std::move (node));
    }
}

mesh_node_host_service_t::~mesh_node_host_service_t ()
{
    stop ();
}

task_t<spot_create_result_t>
mesh_node_host_service_t::create_user_spot (
  const std::shared_ptr<detail::mesh_node_runtime_t> &,
  bool exclusive,
  std::optional<spot_rid_t> spot_rid,
  std::string stable_type,
  std::optional<std::string> mesh_name,
  std::optional<message_t> request,
  std::optional<placement_profile_t> profile,
  std::optional<affinity_key_t> affinity,
  std::chrono::milliseconds timeout)
{
    const auto deadline =
      std::chrono::steady_clock::now () + timeout;
    if (!_location_store || stable_type.empty ())
        return task_t<spot_create_result_t> (
          result_t<spot_create_result_t>::failure (
            framework_error_kind_t::invalid_configuration,
            "User Spot creation requires a Location Store and stable type"));
    std::shared_ptr<detail::mesh_node_runtime_t> source;
    if (mesh_name) {
        const auto found = std::find_if (
          _nodes.begin (), _nodes.end (),
          [&] (const auto &node) {
              return node && node->mesh_name () == *mesh_name;
          });
        if (found == _nodes.end ())
            return task_t<spot_create_result_t> (
              result_t<spot_create_result_t>::failure (
                framework_error_kind_t::mesh_not_found,
                "The selected Mesh is not registered in this process"));
        source = *found;
    } else {
        if (_nodes.empty ())
            return task_t<spot_create_result_t> (
              result_t<spot_create_result_t>::failure (
                framework_error_kind_t::object_client_not_configured,
                "No object Client or Server Mesh is registered"));
        if (_nodes.size () != 1)
            return task_t<spot_create_result_t> (
              result_t<spot_create_result_t>::failure (
                framework_error_kind_t::mesh_selection_required,
                "More than one object Mesh is registered; select one with in_mesh"));
        source = _nodes.front ();
    }
    if (!spot_rid) {
        const auto status = source->native_node ().status ();
        static std::atomic_uint64_t next_generated_spot{1};
        spot_rid = spot_rid_t::from_string (
          "user:" + status.routing_id ().to_string () + ":"
          + std::to_string (
            next_generated_spot.fetch_add (
              1, std::memory_order_relaxed)));
    }
    const auto selected_mesh = mesh_name.value_or (
      source->mesh_name ());
    std::vector<mesh_node_descriptor_t> candidates;
    location_page_request_t page;
    do {
        auto listed =
          _location_store->list_mesh_nodes (selected_mesh, page)
            .result ()
            .value ();
        for (auto &descriptor : listed.items) {
            const auto capable = std::any_of (
              descriptor.object_capabilities.begin (),
              descriptor.object_capabilities.end (),
              [&] (const object_capability_t &capability) {
                  return capability.object_kind
                           == placement_object_kind_t::user_spot
                    && capability.stable_type == stable_type
                    && (!profile
                        || capability.placement_profiles.contains (
                          std::string (profile->value ())));
              });
            if (descriptor.state == framework_runtime_state_t::serving
                && descriptor.object_role == object_role_t::server
                && descriptor.placement_weight > 0 && capable)
                candidates.push_back (std::move (descriptor));
        }
        page.continuation_token = std::move (listed.continuation_token);
    } while (page.continuation_token);
    if (candidates.empty ())
        return task_t<spot_create_result_t> (
          result_t<spot_create_result_t>::failure (
            framework_error_kind_t::placement_capacity_exhausted,
            "No eligible User Spot target is ready"));
    const auto total_weight = std::accumulate (
      candidates.begin (), candidates.end (), std::uint64_t{0},
      [] (std::uint64_t total, const auto &candidate) {
          return total + candidate.placement_weight;
      });
    auto choice = std::hash<std::string>{} (
      std::string (spot_rid->value ()))
      % total_weight;
    auto target = candidates.front ();
    for (const auto &candidate : candidates) {
        if (choice < candidate.placement_weight) {
            target = candidate;
            break;
        }
        choice -= candidate.placement_weight;
    }
    std::vector<std::byte> application_bytes;
    if (request) {
        const auto raw = detail::message_to_raw (*request, *_serializers);
        const auto bytes = raw.to_bytes ();
        if (bytes.size () > 1024u * 1024u)
            return task_t<spot_create_result_t> (
              result_t<spot_create_result_t>::failure (
                framework_error_kind_t::invalid_configuration,
                "User Spot creation request exceeds 1 MiB"));
        application_bytes.reserve (bytes.size ());
        for (const auto value : bytes)
            application_bytes.push_back (
              static_cast<std::byte> (value));
    }
    const object_creation_key_t key{
      placement_object_kind_t::user_spot,
      std::string (spot_rid->value ())};
    object_reservation_fence_t fence;
    bool source_created_reservation = false;
    const auto target_token = location_owner_token_t{
      target.owner_id, target.lease_generation};
    const std::string creating_text =
      "zlink:user-spot:creating:v1\n" + stable_type + "\n"
      + std::string (spot_rid->value ());
    std::vector<std::byte> creating_payload;
    creating_payload.reserve (creating_text.size ());
    for (const auto value : creating_text)
        creating_payload.push_back (static_cast<std::byte> (
          static_cast<unsigned char> (value)));
    object_reserve_request_t reserve_request;
    reserve_request.key = key;
    reserve_request.intent.stable_type = stable_type;
    reserve_request.intent.placement_profile = profile;
    reserve_request.intent.affinity_key = affinity;
    reserve_request.intent.request_content_reference =
      encode_inline_creation_content (application_bytes);
    reserve_request.intent.request_sha256 = sha256 (application_bytes);
    reserve_request.intent.request_encoded_size =
      static_cast<std::uint64_t> (application_bytes.size ());
    reserve_request.target = {
      selected_mesh,
      node_rid_t::from_string (target.rid.to_string ()),
      target.lifecycle_generation,
      target_token};
    reserve_request.creating_payload = std::move (creating_payload);
    reserve_request.pending_capacity_delta = 1;
    const auto reserved =
      _location_store
        ->reserve (std::move (reserve_request))
        .result ()
        .value ();
    const auto retry_generated_collision =
      [&] () -> task_t<spot_create_result_t> {
        const auto now = std::chrono::steady_clock::now ();
        if (now >= deadline)
            return task_t<spot_create_result_t> (
              result_t<spot_create_result_t>::failure (
                framework_error_kind_t::deadline_exceeded,
                "User Spot automatic RID collision exhausted the create deadline"));
        const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds> (
            deadline - now);
        if (remaining <= std::chrono::milliseconds::zero ())
            return task_t<spot_create_result_t> (
              result_t<spot_create_result_t>::failure (
                framework_error_kind_t::deadline_exceeded,
                "User Spot automatic RID collision exhausted the create deadline"));
        return create_user_spot (
          source, true, std::nullopt, stable_type, mesh_name,
          std::move (request), profile, affinity, remaining);
      };
    if (const auto *ready =
          std::get_if<object_already_exists_t> (&reserved)) {
        if (exclusive)
            return retry_generated_collision ();
        return task_t<spot_create_result_t> (
          result_t<spot_create_result_t>::success (
            {spot_ref_t{
               *spot_rid,
               ready->current.object_generation,
               ready->current.allocation.mesh_name,
               ready->current.allocation.node_rid},
             spot_create_state_t::existing,
             std::nullopt}));
    }
    if (const auto *mismatch =
          std::get_if<object_type_mismatch_t> (&reserved)) {
        if (exclusive
            && mismatch->current.allocation.capacity_state
                 == placement_capacity_state_t::active)
            return retry_generated_collision ();
        return task_t<spot_create_result_t> (
          result_t<spot_create_result_t>::failure (
            framework_error_kind_t::spot_type_mismatch,
            "User Spot stable type does not match"));
    }
    if (std::holds_alternative<
          object_placement_capacity_exhausted_t> (reserved))
        return task_t<spot_create_result_t> (
          result_t<spot_create_result_t>::failure (
            framework_error_kind_t::placement_capacity_exhausted,
            "User Spot placement capacity is exhausted"));
    if (const auto *created =
          std::get_if<object_reserved_t> (&reserved)) {
        fence = created->fence;
        source_created_reservation = true;
    } else if (const auto *conflict =
                 std::get_if<object_reserve_conflict_t> (&reserved)) {
        const auto *snapshot =
          std::get_if<authority_snapshot_t> (&conflict->current);
        if (!snapshot || !snapshot->pending_creation
            || snapshot->allocation.stable_type != stable_type)
            return task_t<spot_create_result_t> (
              result_t<spot_create_result_t>::failure (
                framework_error_kind_t::request_failed,
                "User Spot Creating attempt cannot be joined"));
        fence = {
          snapshot->pending_creation->reservation_id,
          snapshot->store_version,
          snapshot->object_generation,
          snapshot->authority_owner_generation,
          {snapshot->allocation.mesh_name,
           snapshot->allocation.node_rid,
           snapshot->allocation.node_lifecycle_generation,
           snapshot->owner},
          snapshot->allocation.capacity_delta};
        target.mesh_name = fence.target.mesh_name;
        target.rid = zlink::routing_id_t::from (
          std::string (fence.target.node_rid.value ()));
        target.lifecycle_generation =
          fence.target.node_lifecycle_generation;
        target.owner_id = fence.target.owner.owner_id;
        target.lease_generation = fence.target.owner.lease_generation;
    } else {
        return task_t<spot_create_result_t> (
          result_t<spot_create_result_t>::failure (
            framework_error_kind_t::request_failed,
            "User Spot reservation failed"));
    }
    const auto source_status = source->native_node ().status ();
    protocol::user_spot_create_header_t command{
      0,
      {source_status.lifecycle_generation (),
       next_user_spot_operation.fetch_add (
         1, std::memory_order_relaxed)},
      source_status.routing_id ().to_bytes (),
      source_status.lifecycle_generation (),
      zlink::routing_id_t::from (
        std::string (spot_rid->value ()))
        .to_bytes (),
      stable_type,
      {fence.reservation_id,
       fence.expected_store_version,
       fence.object_generation,
       fence.authority_owner_generation,
       target.rid.to_bytes (),
       target.lifecycle_generation,
       fence.target.owner.owner_id,
       static_cast<std::uint64_t> (
         fence.target.owner.lease_generation),
       fence.pending_capacity_delta},
      static_cast<std::uint64_t> (
        std::chrono::duration_cast<std::chrono::milliseconds> (
          std::chrono::system_clock::now ().time_since_epoch ()
          + timeout)
          .count ())};
    auto completion =
      std::make_shared<detail::task_completion_source_t<
        spot_create_result_t>> ();
    auto output = completion->task ();
    bool accepted = false;
    try {
        accepted =
          source->native_node ().create_user_spot_remote (
            target.rid, std::move (command), timeout,
            [completion, target, serializers = _serializers,
             store = _location_store, key, fence,
             source_created_reservation] (
              foundation::operation_terminal_t terminal,
              protocol::user_spot_create_reply_t reply,
              std::optional<protocol::application_payload_t>
                application_reply) {
            if (terminal
                  != foundation::operation_terminal_t::completed
                || reply.header.terminal_result != 0) {
                if (terminal
                    == foundation::operation_terminal_t::completed)
                    (void) cleanup_source_created_reservation (
                      store, key, fence,
                      source_created_reservation);
                completion->complete (
                  result_t<spot_create_result_t>::failure (
                    user_spot_terminal::
                      map_user_spot_operation_failure (
                      terminal, reply.header, true),
                    "Remote User Spot creation failed"));
                return;
            }
            if (reply.result
                == protocol::user_spot_create_result_t::rejected)
                (void) cleanup_source_created_reservation (
                  store, key, fence,
                  source_created_reservation);
            std::optional<message_t> decoded_reply;
            if (application_reply)
                decoded_reply = message_t::from_raw (
                  zlink::message_t::from (
                    application_reply->payload),
                  serializers);
            completion->complete (
              result_t<spot_create_result_t>::success (
                {{spot_rid_t::from_string (
                    zlink::routing_id_t::from (
                      reply.spot_routing_id)
                      .to_string ()),
                  reply.object_generation,
                  target.mesh_name,
                  node_rid_t::from_string (
                    target.rid.to_string ())},
                 reply.result
                       == protocol::user_spot_create_result_t::existing
                   ? spot_create_state_t::existing
                   : reply.result
                         == protocol::user_spot_create_result_t::created
                     ? spot_create_state_t::created
                     : spot_create_state_t::rejected,
                 std::move (decoded_reply)}));
            });
    }
    catch (...) {
        (void) cleanup_source_created_reservation (
          _location_store, key, fence,
          source_created_reservation);
        completion->complete (
          result_t<spot_create_result_t>::failure (
            framework_error_kind_t::request_failed,
            "User Spot create submission failed"));
        return output;
    }
    if (!accepted) {
        (void) cleanup_source_created_reservation (
          _location_store, key, fence,
          source_created_reservation);
        completion->complete (
          result_t<spot_create_result_t>::failure (
            framework_error_kind_t::request_rejected,
            "User Spot create operation was not admitted"));
    }
    return output;
}

task_t<std::optional<spot_ref_t>>
mesh_node_host_service_t::find_user_spot (spot_rid_t spot_rid)
{
    if (!_location_store)
        return task_t<std::optional<spot_ref_t>> (
          result_t<std::optional<spot_ref_t>>::failure (
            framework_error_kind_t::invalid_configuration,
            "Spot manager requires a Location Store"));
    const auto read = _location_store
      ->read_authority (
        authority_key_t{
          "2:" + std::string (spot_rid.value ())})
      .result ()
      .value ();
    const auto *snapshot = std::get_if<authority_snapshot_t> (&read);
    if (!snapshot
        || snapshot->allocation.object_kind
             != placement_object_kind_t::user_spot
        || snapshot->allocation.capacity_state
             != placement_capacity_state_t::active)
        return task_t<std::optional<spot_ref_t>> (
          result_t<std::optional<spot_ref_t>>::success (
            std::nullopt));
    return task_t<std::optional<spot_ref_t>> (
      result_t<std::optional<spot_ref_t>>::success (
        spot_ref_t{
          std::move (spot_rid),
          snapshot->object_generation,
          snapshot->allocation.mesh_name,
          snapshot->allocation.node_rid}));
}

task_t<bool> mesh_node_host_service_t::close_user_spot (
  const std::shared_ptr<detail::mesh_node_runtime_t> &,
  spot_ref_t spot)
{
    if (!_location_store)
        return task_t<bool> (result_t<bool>::failure (
          framework_error_kind_t::invalid_configuration,
          "Spot manager requires a Location Store"));
    const auto source_node = std::find_if (
      _nodes.begin (), _nodes.end (),
      [&] (const auto &node) {
          return node && node->mesh_name () == spot.mesh_name ();
      });
    if (source_node == _nodes.end ())
        return task_t<bool> (result_t<bool>::failure (
          framework_error_kind_t::mesh_not_found,
          "The SpotRef Mesh is not registered in this process"));
    const auto source = *source_node;
    const auto read = _location_store
      ->read_authority (
        authority_key_t{
          "2:" + std::string (spot.spot_rid ().value ())})
      .result ()
      .value ();
    const auto *snapshot = std::get_if<authority_snapshot_t> (&read);
    if (!snapshot)
        return task_t<bool> (result_t<bool>::success (false));
    if (snapshot->object_generation != spot.object_generation ())
        return task_t<bool> (result_t<bool>::failure (
          framework_error_kind_t::spot_generation_stale,
          "User Spot generation is stale"));
    if (snapshot->allocation.object_kind
          != placement_object_kind_t::user_spot
        || snapshot->allocation.capacity_state
             != placement_capacity_state_t::active
        || snapshot->allocation.mesh_name != spot.mesh_name ()
        || snapshot->allocation.node_rid.value ()
             != spot.node_rid ().value ())
        return task_t<bool> (result_t<bool>::failure (
          framework_error_kind_t::spot_moving,
          "User Spot owner is moving"));
    const auto source_status = source->native_node ().status ();
    protocol::user_spot_close_header_t command{
      0,
      {source_status.lifecycle_generation (),
       next_user_spot_operation.fetch_add (
         1, std::memory_order_relaxed)},
      source_status.routing_id ().to_bytes (),
      source_status.lifecycle_generation (),
      {zlink::routing_id_t::from (
         std::string (spot.spot_rid ().value ()))
         .to_bytes (),
       spot.object_generation (),
       zlink::routing_id_t::from (
         std::string (snapshot->allocation.node_rid.value ()))
         .to_bytes (),
       snapshot->allocation.node_lifecycle_generation,
       snapshot->authority_owner_generation,
       snapshot->store_version},
      static_cast<std::uint64_t> (
        std::chrono::duration_cast<std::chrono::milliseconds> (
          std::chrono::system_clock::now ().time_since_epoch ()
          + std::chrono::seconds (30))
          .count ())};
    auto completion =
      std::make_shared<detail::task_completion_source_t<bool>> ();
    auto output = completion->task ();
    const auto accepted =
      source->native_node ().close_user_spot_remote (
        zlink::routing_id_t::from (
          std::string (snapshot->allocation.node_rid.value ())),
        std::move (command), std::chrono::seconds (30),
        [completion] (
          foundation::operation_terminal_t terminal,
          protocol::user_spot_close_reply_t reply) {
            if (terminal
                  != foundation::operation_terminal_t::completed
                || reply.header.terminal_result != 0) {
                completion->complete (
                  result_t<bool>::failure (
                    user_spot_terminal::
                      map_user_spot_operation_failure (
                      terminal, reply.header, false),
                    "Remote User Spot close failed"));
                return;
            }
            completion->complete (
              result_t<bool>::success (reply.closed));
        });
    if (!accepted)
        completion->complete (
          result_t<bool>::failure (
            framework_error_kind_t::request_rejected,
            "User Spot close operation was not admitted"));
    return output;
}

void mesh_node_host_service_t::start (service_provider_t &services)
{
    _services = &services;
    _stop.store (false, std::memory_order_release);
    _accept_application_dispatch.store (true, std::memory_order_release);
    auto store = std::shared_ptr<location_store_t> (
      &services.get_required<location_store_t> (),
      [] (location_store_t *) noexcept {});
    _location_store = store;
    for (std::size_t index = 0; index < _nodes.size (); ++index) {
        const auto registration = _registrations[index];
        const auto source = _nodes[index];
        registration->spot_state->create_user_spot =
          [this, source] (
            bool exclusive,
            std::optional<spot_rid_t> spot_rid,
            std::string stable_type,
            std::optional<std::string> mesh_name,
            std::optional<message_t> request,
            std::optional<placement_profile_t> profile,
            std::optional<affinity_key_t> affinity,
            std::chrono::milliseconds timeout) {
              return create_user_spot (
                source, exclusive, std::move (spot_rid),
                std::move (stable_type), std::move (mesh_name),
                std::move (request), std::move (profile),
                std::move (affinity), timeout);
          };
        registration->spot_state->find_user_spot =
          [this] (spot_rid_t spot_rid) {
              return find_user_spot (std::move (spot_rid));
          };
        registration->spot_state->close_user_spot =
          [this, source] (spot_ref_t spot) {
              return close_user_spot (source, std::move (spot));
          };
        _nodes[index]->configure_user_spot_operations (
          store,
          [this, registration] (
            const stateful::object_ref_t &object,
            const std::string &stable_type,
            const std::vector<std::byte> &payload) {
              std::vector<std::uint8_t> rid_bytes;
              rid_bytes.reserve (object.key.size ());
              for (const auto value : object.key)
                  rid_bytes.push_back (
                    static_cast<std::uint8_t> (
                      static_cast<unsigned char> (value)));
              std::vector<std::uint8_t> request_bytes;
              request_bytes.reserve (payload.size ());
              for (const auto value : payload)
                  request_bytes.push_back (
                    std::to_integer<std::uint8_t> (value));
              detail::spot_node_runtime_t spots (
                registration->spot_state);
              auto created = spots.get_or_create_spot (
                stable_type,
                spot_rid_t::from_string (
                  zlink::routing_id_t::from (rid_bytes)
                    .to_string ()),
                zlink::message_t::from (request_bytes));
              std::optional<protocol::application_payload_t>
                application_reply;
              if (created.reply) {
                  application_reply =
                    protocol::application_payload_t{
                      {},
                      "application/octet-stream",
                      detail::message_to_raw (
                        *created.reply, *_serializers)
                        .to_bytes ()};
              }
              return host::user_spot_materialize_result_t{
                created.state
                  == spot_create_state_t::created
                  || created.state
                       == spot_create_state_t::existing,
                std::move (application_reply)};
          });
    }
    for (const auto &node : _nodes)
        node->start ();
    auto &location_runtime =
      services.get_required<location_runtime_t> ();
    _location_owner = location_runtime.current_owner_token ();
    if (!_location_owner)
        throw framework_exception_t (
          framework_error_kind_t::invalid_configuration,
          "MeshNode publication requires an active Location owner lease");
    _published_mesh_nodes.clear ();
    for (std::size_t index = 0; index < _nodes.size (); ++index) {
        const auto &node = _nodes[index];
        const auto &registration = _registrations[index];
        const auto status = node->status ();
        mesh_node_descriptor_t descriptor;
        descriptor.mesh_name = node->mesh_name ();
        descriptor.rid = status.routing_id ();
        descriptor.lifecycle_generation =
          status.lifecycle_generation ();
        descriptor.descriptor_revision = 1;
        descriptor.endpoint = status.local_endpoint ();
        descriptor.channel_weights = node->channel_weights ();
        descriptor.object_role = object_role_t::server;
        descriptor.placement_weight = 100;
        descriptor.state = framework_runtime_state_t::serving;
        descriptor.security_identity =
          _location_owner->owner_id;
        descriptor.owner_id = _location_owner->owner_id;
        descriptor.lease_generation =
          _location_owner->lease_generation;
        for (const auto &stable_type :
             registration->spot_state->snapshot.spot_names) {
            if (registration->spot_state->snapshot.entry_spot_name
                == stable_type)
                continue;
            descriptor.object_capabilities.push_back (
              object_capability_t{
                .object_kind =
                  placement_object_kind_t::user_spot,
                .stable_type = stable_type});
        }
        const auto written =
          _location_store
            ->update_mesh_node (
              descriptor, location_write_intent_t::new_claim)
            .result ()
            .value ();
        if (written.status != location_write_status_t::stored)
            throw framework_exception_t (
              framework_error_kind_t::invalid_configuration,
              "MeshNode Location descriptor publication was fenced");
        _published_mesh_nodes.push_back (
          {descriptor.mesh_name, descriptor.rid});
    }
    for (std::size_t index = 0; index < _nodes.size (); ++index) {
        const auto node = _nodes[index];
        const auto registration = _registrations[index];
        _threads.emplace_back ([this, node, registration] {
            while (!_stop.load (std::memory_order_acquire)) {
                const auto count = node->dispatch_ready (
                  [&] (const host::ready_record_t &owner,
                       const host::receive_record_t &record,
                       std::vector<zlink::message_t> parts) {
                      detail::spot_node_runtime_t spot_runtime (registration->spot_state);
                      const bool transfer_dispatch =
                        owner.owner_kind == host::owner_kind_t::actor
                        && (record.kind == host::record_kind_t::actor_send
                            || record.kind == host::record_kind_t::actor_request)
                        && spot_runtime.actor_transfer_in_progress (
                          owner.actor.actor_id ());
                      auto run_direct = [this] (auto &&work) {
                          {
                              std::lock_guard lock (_dispatch_gate_mutex);
                              ++_active_direct_dispatch;
                          }
                          try {
                              work ();
                          }
                          catch (...) {
                              {
                                  std::lock_guard lock (_dispatch_gate_mutex);
                                  --_active_direct_dispatch;
                              }
                              _dispatch_gate_changed.notify_all ();
                              throw;
                          }
                          {
                              std::lock_guard lock (_dispatch_gate_mutex);
                              --_active_direct_dispatch;
                          }
                          _dispatch_gate_changed.notify_all ();
                      };
                      if (transfer_dispatch) {
                          run_direct ([&] {
                              (void) spot_runtime.dispatch_mesh_record (
                                owner, record, parts, *_services, *_serializers);
                          });
                          return;
                      }
                      if (owner.domain == host::ready_domain_t::application) {
                          bool accepted = false;
                          {
                              std::lock_guard lock (_dispatch_gate_mutex);
                              if (_accept_application_dispatch.load (
                                    std::memory_order_relaxed)) {
                                  node->application_work_enqueued ();
                                  accepted = true;
                              }
                          }
                          if (!accepted) {
                              reject_application_request (record, std::move (parts));
                              return;
                          }
                          std::vector<std::vector<std::uint8_t>> owned_part_bytes;
                          owned_part_bytes.reserve (parts.size ());
                          for (const auto &part : parts)
                              owned_part_bytes.push_back (part.to_bytes ());
                          try {
                              _application_dispatch->submit (
                                [this, node, registration, owner, record,
                                 part_bytes = std::move (owned_part_bytes)] () mutable {
                                    node->application_work_started ();
                                    try {
                                        std::vector<zlink::message_t> owned_parts;
                                        owned_parts.reserve (part_bytes.size ());
                                        for (const auto &bytes : part_bytes) {
                                            owned_parts.emplace_back (bytes.size ());
                                            if (!bytes.empty ())
                                                std::memcpy (
                                                  owned_parts.back ().data (),
                                                  bytes.data (), bytes.size ());
                                        }
                                        detail::spot_node_runtime_t
                                          application_spot_runtime (
                                            registration->spot_state);
                                        if (!application_spot_runtime
                                               .dispatch_mesh_record (
                                                 owner, record, owned_parts,
                                                 *_services, *_serializers)) {
                                            detail::mesh_record_dispatcher_t dispatcher (
                                              *_services, *_serializers,
                                              registration->handlers,
                                              _dispatch_options);
                                            (void) dispatcher.dispatch (
                                              record, std::move (owned_parts));
                                        }
                                    }
                                    catch (...) {
                                        node->application_work_finished ();
                                        _dispatch_gate_changed.notify_all ();
                                        return;
                                    }
                                    node->application_work_finished ();
                                    _dispatch_gate_changed.notify_all ();
                                });
                          }
                          catch (...) {
                              node->application_work_started ();
                              node->application_work_finished ();
                              _dispatch_gate_changed.notify_all ();
                              throw;
                          }
                          return;
                      }
                      run_direct ([&] {
                          if (spot_runtime.dispatch_mesh_record (
                                owner, record, parts, *_services, *_serializers)) {
                              return;
                          }
                          detail::mesh_record_dispatcher_t dispatcher (
                            *_services, *_serializers, registration->handlers,
                            _dispatch_options);
                          (void) dispatcher.dispatch (record, std::move (parts));
                      });
                  });
                detail::spot_node_runtime_t maintenance (registration->spot_state);
                (void) maintenance.cleanup_expired_actor_admissions ();
                if (count == 0)
                    std::this_thread::sleep_for (std::chrono::milliseconds (1));
            }
        });
    }
}

void mesh_node_host_service_t::request_stop () noexcept
{
    seal_application_dispatch ();
}

void mesh_node_host_service_t::seal_application_dispatch () noexcept
{
    {
        std::lock_guard lock (_dispatch_gate_mutex);
        _accept_application_dispatch.store (false, std::memory_order_release);
    }
    _dispatch_gate_changed.notify_all ();
}

bool mesh_node_host_service_t::wait_for_accepted_callbacks_until (
  std::chrono::steady_clock::time_point deadline) noexcept
{
    std::unique_lock lock (_dispatch_gate_mutex);
    return _dispatch_gate_changed.wait_until (lock, deadline, [this] {
        if (_active_direct_dispatch != 0)
            return false;
        return std::all_of (_nodes.begin (), _nodes.end (), [] (const auto &node) {
            return node->pending_application_callbacks () == 0
                   && node->active_application_callbacks () == 0;
        });
    });
}

void mesh_node_host_service_t::stop () noexcept
{
    request_stop ();
    if (_application_dispatch)
        _application_dispatch->drain ();
    trace_mesh_host_stop ("application-drained");
    _stop.store (true, std::memory_order_release);
    trace_mesh_host_stop ("pump-join-begin");
    for (auto &thread : _threads) {
        if (thread.joinable ())
            thread.join ();
    }
    trace_mesh_host_stop ("pump-join-end");
    _threads.clear ();
    if (_location_store && _location_owner) {
        for (const auto &key : _published_mesh_nodes) {
            try {
                (void) _location_store
                  ->remove_mesh_node (key, *_location_owner)
                  .result ();
            }
            catch (...) {
            }
        }
    }
    _published_mesh_nodes.clear ();
    _location_owner.reset ();
    for (auto &node : _nodes) {
        trace_mesh_host_stop ("node-stop-begin");
        node->stop ();
        trace_mesh_host_stop ("node-stop-end");
    }
}

std::vector<std::shared_ptr<detail::mesh_node_runtime_t>>
mesh_node_host_service_t::nodes () const
{
    return _nodes;
}

zlink::submit_result_t mesh_node_host_service_t::submit_local_node_send (
  const std::shared_ptr<detail::mesh_node_runtime_t> &node,
  const std::vector<zlink::message_t> &parts)
{
    const auto found = std::find (_nodes.begin (), _nodes.end (), node);
    if (found == _nodes.end () || _services == nullptr || _serializers == nullptr)
        return zlink::submit_result_t::not_found;
    const auto index = static_cast<std::size_t> (std::distance (_nodes.begin (), found));
    const auto registration = _registrations[index];
    const auto source_rid = node->routing_id ();
    if (!source_rid)
        return zlink::submit_result_t::not_found;

    std::vector<std::vector<std::uint8_t>> owned_part_bytes;
    owned_part_bytes.reserve (parts.size ());
    for (const auto &part : parts)
        owned_part_bytes.push_back (part.to_bytes ());

    {
        std::lock_guard lock (_dispatch_gate_mutex);
        if (!_accept_application_dispatch.load (std::memory_order_relaxed))
            return zlink::submit_result_t::terminated;
        if (node->pending_application_callbacks () + node->active_application_callbacks ()
            >= node->max_pending ())
            return zlink::submit_result_t::backpressured;
        node->application_work_enqueued ();
    }

    try {
        _application_dispatch->submit (
          [this, node, registration, source_rid,
           part_bytes = std::move (owned_part_bytes)] () mutable {
              node->application_work_started ();
              try {
                  std::vector<zlink::message_t> owned_parts;
                  owned_parts.reserve (part_bytes.size ());
                  for (const auto &bytes : part_bytes) {
                      owned_parts.emplace_back (bytes.size ());
                      if (!bytes.empty ())
                          std::memcpy (owned_parts.back ().data (), bytes.data (), bytes.size ());
                  }
                  host::receive_record_t record;
                  record.kind = host::record_kind_t::node_send;
                  record.domain = host::ready_domain_t::application;
                  record.source_node_rid = *source_rid;
                  detail::mesh_record_dispatcher_t dispatcher (
                    *_services, *_serializers, registration->handlers, _dispatch_options);
                  (void) dispatcher.dispatch (record, std::move (owned_parts));
              }
              catch (...) {
                  node->application_work_finished ();
                  _dispatch_gate_changed.notify_all ();
                  return;
              }
              node->application_work_finished ();
              _dispatch_gate_changed.notify_all ();
          });
    }
    catch (...) {
        node->application_work_started ();
        node->application_work_finished ();
        _dispatch_gate_changed.notify_all ();
        return zlink::submit_result_t::backpressured;
    }
    return zlink::submit_result_t::ok;
}

} // namespace zlink::framework::runtime
