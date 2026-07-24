/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/foundation/operation_registry.hpp"
#include "runtime/execution/actor_execution_context.hpp"
#include "runtime/mesh/raw_mesh_node_owner.hpp"
#include "runtime/locations/in_memory_location_store.hpp"
#include "runtime/locations/sha256.hpp"
#include "runtime/locations/source_creation_cleanup.hpp"
#include "runtime/stateful/raw_stateful_dispatch.hpp"
#include "runtime/stateful/public_host_runtime.hpp"
#include "runtime/stateful/stateful_object_runtime.hpp"
#include "runtime/stateful/stream_session_registry.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <future>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace foundation = zlink::framework::runtime::foundation;
namespace mesh = zlink::framework::runtime::mesh;
namespace protocol = zlink::framework::runtime::protocol;
namespace stateful = zlink::framework::runtime::stateful;
namespace host = zlink::framework::runtime::host;

using namespace std::chrono_literals;

namespace
{

class execution_mode_spot_t final : public zlink::framework::spot_t
{
};

class recording_actor_client_t final : public zlink::framework::actor_client_t
{
  public:
    std::atomic_int request_submissions{0};

  protected:
    zlink::framework::task_t<void> send_to_actor_erased (
      zlink::framework::actor_ref_t,
      std::string,
      zlink::framework::message_t,
      const zlink::framework::actor_send_call_t::metadata_map_t &) override
    {
        return zlink::framework::task_t<void> (
          zlink::framework::result_t<void>::success ());
    }

    zlink::framework::task_t<zlink::framework::message_t>
    request_to_actor_erased (
      zlink::framework::actor_ref_t,
      std::string,
      zlink::framework::message_t,
      std::optional<std::chrono::milliseconds>) override
    {
        request_submissions.fetch_add (1);
        return zlink::framework::task_t<zlink::framework::message_t> (
          zlink::framework::result_t<zlink::framework::message_t>::success (
            zlink::framework::message_t{}));
    }

    zlink::framework::serializer_registry_t &actor_client_serializers () override
    {
        return serializers;
    }

  private:
    zlink::framework::serializer_registry_t serializers;
};

void verify_spot_id_contract ()
{
    using namespace zlink::framework;
    static_assert (std::is_same_v<spot_id_t, std::string>);

    const auto user = detail::new_user_spot_id ();
    assert (user.size () == 36);
    assert (user[8] == '-' && user[13] == '-' && user[18] == '-'
            && user[23] == '-' && user[14] == '4');
    assert (user[19] == '8' || user[19] == '9'
            || user[19] == 'a' || user[19] == 'b');
    assert (detail::valid_spot_id (user));

    const auto entry = detail::new_entry_spot_id ("server");
    assert (entry.starts_with ("server-entry-"));
    assert (detail::is_framework_entry_spot_id (entry));
    assert (entry != detail::new_entry_spot_id ("server"));

    assert (!detail::valid_spot_id (""));
    assert (!detail::valid_spot_id (std::string (256, 'x')));
    assert (!detail::valid_spot_id (std::string ("\xc3\x28", 2)));
    assert (detail::valid_spot_id ("Room") && detail::valid_spot_id ("room"));

    bool rejected = false;
    try {
        (void) spot_ref_t (
          std::string (256, 'x'), 1, "mesh",
          node_rid_t::from_string ("node"));
    }
    catch (const std::invalid_argument &) {
        rejected = true;
    }
    assert (rejected);
}

void verify_user_spot_execution_mode_registration ()
{
    using namespace zlink::framework;

    spot_node_builder_t builder;
    builder.add_spot<execution_mode_spot_t> ("wide");
    builder.add_spot<execution_mode_spot_t> (
      "actors", user_spot_execution_mode_t::per_actor);

    const auto snapshot = builder.snapshot ();
    assert (
      snapshot.spot_execution_modes.at ("wide")
      == user_spot_execution_mode_t::spot_wide);
    assert (
      snapshot.spot_execution_modes.at ("actors")
      == user_spot_execution_mode_t::per_actor);
}

void verify_self_actor_request_rejected_before_submission ()
{
    using namespace zlink::framework;

    recording_actor_client_t actor_client;
    const actor_ref_t actor (
      node_rid_t::from_string ("node"), "player", "actor-1", 1);
    runtime::actor_execution_scope_t scope (
      "player:actor-1", "spot-1");
    actor_request_call_t request (
      actor_client, actor, "SelfRequest", message_t{});

    const auto result = request.submit_message ().result ();
    assert (!result);
    assert (
      result.error_kind ()
      == framework_error_kind_t::invalid_configuration);
    assert (actor_client.request_submissions.load () == 0);
}

void verify_same_gate_request_rejected_before_submission ()
{
    using namespace zlink::framework;

    std::atomic_int submissions = 0;
    request_call_t<int> request (
      "SameGate",
      [&submissions] (const auto &, auto, const auto &) {
          submissions.fetch_add (1);
          return task_t<int> (result_t<int>::success (1));
      },
      [] (bool) {
          return result_t<void>::failure (
            framework_error_kind_t::invalid_configuration,
            "awaited request requires the current Spot execution gate");
      });

    const auto result = request.submit ().result ();
    assert (!result);
    assert (
      result.error_kind ()
      == framework_error_kind_t::invalid_configuration);
    assert (submissions.load () == 0);
}

void verify_creation_terminal_operation_isolation ()
{
    using namespace zlink::framework;
    auto store =
      std::make_shared<
        zlink::framework::runtime::in_memory_location_store_t> ();
    const auto claimed =
      std::get<owner_lease_claimed_t> (
        store
          ->claim_owner_lease (
            "terminal-owner", std::chrono::seconds (15))
          .result ()
          .value ());
    mesh_node_descriptor_t target{
      .mesh_name = "m6b-mesh",
      .rid = zlink::routing_id_t::from ("terminal-target"),
      .lifecycle_generation = 1,
      .descriptor_revision = 1,
      .endpoint = "tcp://127.0.0.1:1",
      .application_version = 1,
      .object_capabilities =
        {{.object_kind = placement_object_kind_t::actor,
          .stable_type = "player"}},
      .object_role = object_role_t::server,
      .capacity = {.actors = {.limit = 8}},
      .state = framework_runtime_state_t::serving,
      .security_identity = "terminal",
      .owner_id = claimed.token.owner_id,
      .lease_generation = claimed.token.lease_generation};
    assert (
      store
        ->update_mesh_node (
          target, location_write_intent_t::new_claim)
        .result ()
        .value ()
        .status == location_write_status_t::stored);
    object_reserve_request_t request{
      .key = {placement_object_kind_t::actor, "terminal-actor"},
      .intent = {.stable_type = "player"},
      .target =
        {.mesh_name = "m6b-mesh",
         .node_rid = node_rid_t::from_string ("terminal-target"),
         .node_lifecycle_generation = 1,
         .owner = claimed.token},
      .capacity_bundle = {.actor_slots = 1}};
    const auto reserved =
      std::get<object_reserved_t> (
        store->reserve (request).result ().value ());
    const creation_operation_identity_t first{
      node_rid_t::from_string ("terminal-source"), 3, {5, 7}};
    const std::vector<std::byte> envelope{
      std::byte{'v'}, std::byte{'1'}};
    const creation_terminal_publication_t publication{
      first,
      envelope,
      zlink::framework::runtime::sha256 (envelope),
      std::chrono::system_clock::now ()
        + std::chrono::seconds (30)};
    assert (
      std::holds_alternative<
        object_creation_completed_result_t> (
        store
          ->complete_creation (
            {request.key, reserved.fence,
             object_creation_rejected_t{publication}})
          .result ()
          .value ()));
    assert (
      store->read_creation_terminal (first)
        .result ()
        .value ()
        .has_value ());
    const creation_operation_identity_t second{
      node_rid_t::from_string ("terminal-source"), 3, {5, 8}};
    assert (
      !store->read_creation_terminal (second)
         .result ()
         .value ()
         .has_value ());
    assert (
      std::holds_alternative<object_reserved_t> (
        store->reserve (request).result ().value ()));
}

void verify_typed_capacity_retry_uses_second_candidate ()
{
    using namespace zlink::framework;
    auto store =
      std::make_shared<
        zlink::framework::runtime::in_memory_location_store_t> ();
    const auto first_owner =
      std::get<owner_lease_claimed_t> (
        store->claim_owner_lease ("capacity-first", 15s)
          .result ().value ()).token;
    const auto second_owner =
      std::get<owner_lease_claimed_t> (
        store->claim_owner_lease ("capacity-second", 15s)
          .result ().value ()).token;
    const auto publish = [&] (
      std::string rid, const location_owner_token_t &owner) {
        mesh_node_descriptor_t descriptor{
          .mesh_name = "capacity-mesh",
          .rid = zlink::routing_id_t::from (rid),
          .lifecycle_generation = 1,
          .descriptor_revision = 1,
          .endpoint = "tcp://127.0.0.1:1",
          .application_version = 1,
          .object_capabilities =
            {{.object_kind = placement_object_kind_t::user_spot,
              .stable_type = "room"}},
          .object_role = object_role_t::server,
          .capacity =
            {.spots = {.limit = 32},
             .spot_types =
               {{.object_kind =
                    placement_object_kind_t::user_spot,
                 .stable_type = "room",
                 .usage = {.limit = 1}}}},
          .state = framework_runtime_state_t::serving,
          .security_identity = "capacity",
          .owner_id = owner.owner_id,
          .lease_generation = owner.lease_generation};
        assert (
          store->update_mesh_node (
            std::move (descriptor),
            location_write_intent_t::new_claim)
            .result ().value ().status
          == location_write_status_t::stored);
    };
    publish ("capacity-node-a", first_owner);
    publish ("capacity-node-b", second_owner);
    const auto target = [] (
      std::string rid, const location_owner_token_t &owner) {
        return object_creation_target_t{
          "capacity-mesh", node_rid_t::from_string (rid), 1, owner};
    };
    object_reserve_request_t occupied{
      .key = {placement_object_kind_t::user_spot, "occupied"},
      .intent = {.stable_type = "room"},
      .target = target ("capacity-node-a", first_owner),
      .capacity_bundle = {
        .spot_slots = 1,
        .spot_type = spot_type_capacity_delta_t{
          .object_kind = placement_object_kind_t::user_spot,
          .stable_type = "room",
          .slots = 1}}};
    assert (std::holds_alternative<object_reserved_t> (
      store->reserve (occupied).result ().value ()));

    object_reserve_request_t request{
      .key = {placement_object_kind_t::user_spot, "retry-room"},
      .intent = {.stable_type = "room"},
      .target = target ("capacity-node-a", first_owner),
      .capacity_bundle = {
        .spot_slots = 1,
        .spot_type = spot_type_capacity_delta_t{
          .object_kind = placement_object_kind_t::user_spot,
          .stable_type = "room",
          .slots = 1}}};
    assert (std::holds_alternative<
      object_placement_capacity_exhausted_t> (
      store->reserve (request).result ().value ()));
    request.target = target ("capacity-node-b", second_owner);
    assert (std::holds_alternative<object_reserved_t> (
      store->reserve (request).result ().value ()));
}

std::vector<std::uint8_t> bytes (std::string value)
{
    return {value.begin (), value.end ()};
}

mesh::service_node_descriptor_t descriptor (std::string rid)
{
    return {"m6b-mesh",
            bytes (std::move (rid)),
            1,
            1,
            "tcp://127.0.0.1:0",
            {},
            mesh::service_node_state_t::preparing};
}

stateful::object_ref_t create_ready (
  stateful::stateful_object_runtime_t &runtime,
  stateful::create_request_t request)
{
    const auto reserved = runtime.begin_create (request);
    assert (reserved.status == stateful::create_status_t::reserved);
    assert (reserved.factory_owner);
    assert (runtime.commit_create (reserved.attempt)
            == stateful::stateful_error_t::none);
    const auto ready = runtime.find (request.kind, request.key);
    assert (ready);
    return *ready;
}

void verify_global_identity_remote_create_and_generation_fence ()
{
    stateful::stateful_object_runtime_t runtime;
    runtime.replace_placement_candidates (
      {{"mesh-a", "node-a", {"player", "room", "transient"}, 100, 16, 0, 4, 0},
       {"mesh-b", "node-b", {"player", "room", "transient"}, 100, 16, 0, 4, 0}});

    auto first = runtime.begin_create (
      {stateful::object_kind_t::actor, "actor-1", "player",
       std::string ("mesh-a"), {}, false, false});
    assert (first.status == stateful::create_status_t::reserved);
    assert (first.object.mesh_name == "mesh-a");

    const auto joined = runtime.begin_create (
      {stateful::object_kind_t::actor, "actor-1", "player",
       std::string ("mesh-b"), {}, false, false});
    assert (joined.status == stateful::create_status_t::joined);
    assert (joined.attempt == first.attempt);
    assert (!joined.factory_owner);
    assert (runtime.commit_create (first.attempt)
            == stateful::stateful_error_t::none);

    const auto global_existing = runtime.begin_create (
      {stateful::object_kind_t::actor, "actor-1", "player",
       std::string ("mesh-b"), {}, false, false});
    assert (global_existing.status == stateful::create_status_t::existing);
    assert (global_existing.object.mesh_name == "mesh-a");

    const auto original = *runtime.find (
      stateful::object_kind_t::actor, "actor-1");
    assert (runtime.destroy_actor (original)
            == stateful::stateful_error_t::none);
    const auto replacement = create_ready (
      runtime,
      {stateful::object_kind_t::actor, "actor-1", "player",
       std::string ("mesh-b"), {}, false, false});
    assert (replacement.object_generation
            == original.object_generation + 1);
    assert (runtime.destroy_actor (original)
            == stateful::stateful_error_t::generation_stale);
}

void verify_membership_turns_and_independent_infrastructure ()
{
    stateful::stateful_object_runtime_t runtime (4, 2);
    runtime.replace_placement_candidates (
      {{"mesh-a", "node-a", {"player", "room"}, 100, 16, 0, 8, 0},
       {"mesh-b", "node-b", {"player", "room"}, 100, 16, 0, 8, 0}});
    const auto actor = create_ready (
      runtime,
      {stateful::object_kind_t::actor, "actor-turn", "player",
       std::string ("mesh-a"), {}, false, false});
    const auto spot = create_ready (
      runtime,
      {stateful::object_kind_t::user_spot, "spot-global", "room",
       std::string ("mesh-b"), {}, false, false});

    assert (runtime.enqueue (
              actor, stateful::turn_domain_t::application, {50, {50}})
            == stateful::stateful_error_t::none);
    assert (runtime.register_timer (actor, {7, 1000, 1000, 60})
            == stateful::stateful_error_t::none);
    const auto [prepare_error, token] =
      runtime.begin_membership_move (actor, spot);
    assert (prepare_error == stateful::stateful_error_t::none);
    assert (runtime.enqueue (
              actor, stateful::turn_domain_t::application, {51, {51}})
            == stateful::stateful_error_t::none);
    assert (runtime.enqueue_timer_tick (actor, 7, {60})
            == stateful::stateful_error_t::none);
    const auto [commit_error, moved_actor] =
      runtime.commit_membership_move (token);
    assert (commit_error == stateful::stateful_error_t::none);
    assert (moved_actor.object_generation == actor.object_generation);
    assert (moved_actor.authority_owner_generation
            == actor.authority_owner_generation + 1);
    assert (runtime.actor_membership (moved_actor)
            == std::optional<std::string> ("spot-global"));
    assert (runtime.timers (moved_actor)
            == std::vector<stateful::logical_timer_t> (
              {{7, 1000, 1000, 61}}));
    assert (runtime.close_spot (spot)
            == std::pair (stateful::stateful_error_t::none, false));

    for (const auto expected : {50u, 51u, 60u}) {
        const auto [queued_error, queued] = runtime.try_claim (
          moved_actor, stateful::turn_domain_t::application);
        assert (queued_error == stateful::stateful_error_t::none);
        assert (queued && queued->sequence == expected);
        assert (runtime.complete_claim (
                  moved_actor, stateful::turn_domain_t::application)
                == stateful::stateful_error_t::none);
    }

    assert (runtime.enqueue (
              moved_actor, stateful::turn_domain_t::application, {1, {1}})
            == stateful::stateful_error_t::none);
    assert (runtime.enqueue (
              moved_actor, stateful::turn_domain_t::application, {2, {2}})
            == stateful::stateful_error_t::none);
    assert (runtime.enqueue (
              moved_actor, stateful::turn_domain_t::infrastructure, {9, {9}})
            == stateful::stateful_error_t::none);

    const auto [first_error, first] = runtime.try_claim (
      moved_actor, stateful::turn_domain_t::application);
    assert (first_error == stateful::stateful_error_t::none);
    assert (first && first->sequence == 1);

    const auto [infra_error, infrastructure] = runtime.try_claim (
      moved_actor, stateful::turn_domain_t::infrastructure);
    assert (infra_error == stateful::stateful_error_t::none);
    assert (infrastructure && infrastructure->sequence == 9);
    assert (runtime.complete_claim (
              moved_actor, stateful::turn_domain_t::infrastructure)
            == stateful::stateful_error_t::none);

    assert (runtime.yield_claim (moved_actor, {3, {3}})
            == stateful::stateful_error_t::none);
    const auto [second_error, second] = runtime.try_claim (
      moved_actor, stateful::turn_domain_t::application);
    assert (second_error == stateful::stateful_error_t::none);
    assert (second && second->sequence == 3);
    assert (runtime.complete_claim (
              moved_actor, stateful::turn_domain_t::application)
            == stateful::stateful_error_t::none);
    const auto [continuation_error, continuation] = runtime.try_claim (
      moved_actor, stateful::turn_domain_t::application);
    assert (continuation_error == stateful::stateful_error_t::none);
    assert (continuation && continuation->sequence == 2);
    assert (runtime.complete_claim (
              moved_actor, stateful::turn_domain_t::application)
            == stateful::stateful_error_t::none);
}

void verify_instance_cold_activation_only_from_intent ()
{
    stateful::stateful_object_runtime_t runtime;
    runtime.replace_placement_candidates (
      {{"mesh-a", "node-a", {"transient"}, 100, 16, 0, 8, 0}});

    const auto forbidden = runtime.begin_create (
      {stateful::object_kind_t::instance_spot, "instance-1", "transient",
       std::nullopt, {}, false, false});
    assert (forbidden.error
            == stateful::stateful_error_t::
              instance_manager_create_forbidden);

    std::size_t factory_count = 0;
    stateful::create_request_t request{
      stateful::object_kind_t::instance_spot,
      "instance-1",
      "transient",
      std::nullopt,
      {},
      false,
      false};
    const auto activated = runtime.activate_instance (
      request, [&] (const stateful::object_ref_t &) {
          ++factory_count;
          return true;
      });
    assert (activated.status == stateful::create_status_t::reserved);
    assert (activated.error == stateful::stateful_error_t::none);
    assert (factory_count == 1);

    const auto existing = runtime.activate_instance (
      request, [&] (const stateful::object_ref_t &) {
          ++factory_count;
          return true;
      });
    assert (existing.status == stateful::create_status_t::existing);
    assert (factory_count == 1);
}

void verify_session_binding_and_terminal_once ()
{
    stateful::stateful_object_runtime_t runtime;
    runtime.replace_placement_candidates (
      {{"mesh-a", "node-a", {"player"}, 100, 16, 0, 8, 0}});
    const auto actor = create_ready (
      runtime,
      {stateful::object_kind_t::actor,
       "session-actor",
       "player",
       std::nullopt,
       {},
       false,
       false});
    const auto second_actor = create_ready (
      runtime,
      {stateful::object_kind_t::actor,
       "session-actor-2",
       "player",
       std::nullopt,
       {},
       false,
       false});

    std::size_t authority_reads = 0;
    stateful::stream_session_registry_t sessions (
      [&] (const std::string &actor_id) {
          ++authority_reads;
          return runtime.find (stateful::object_kind_t::actor, actor_id);
      });
    const auto connection = sessions.open ("stream-rid");
    const auto [bind_error, binding] = sessions.bind (connection, actor);
    assert (bind_error == stateful::stateful_error_t::none);
    const auto [second_bind_error, second_binding] =
      sessions.bind (connection, second_actor);
    assert (second_bind_error == stateful::stateful_error_t::none);
    assert (sessions.is_current (binding));
    assert (sessions.is_current (second_binding));
    assert (authority_reads == 2);
    const auto [dispatch_error, dispatch] =
      sessions.admit_inbound (binding);
    assert (dispatch_error == stateful::stateful_error_t::none);
    assert (dispatch && dispatch->inbound_sequence == 1);
    const auto [second_dispatch_error, second_dispatch] =
      sessions.admit_inbound (second_binding);
    assert (second_dispatch_error == stateful::stateful_error_t::none);
    assert (second_dispatch && second_dispatch->inbound_sequence == 2);
    assert (authority_reads == 2);

    const auto reconnect = sessions.open ("stream-rid");
    assert (reconnect.connection_generation
            == connection.connection_generation + 1);
    assert (!sessions.is_current (binding));
    assert (sessions.admit_inbound (binding).first
            == stateful::stateful_error_t::conflict);
    assert (!sessions.is_current (second_binding));

    foundation::operation_registry_t operations (1);
    foundation::operation_id_t id{};
    id[15] = 1;
    std::size_t terminal_count = 0;
    assert (operations.register_operation (
      id, foundation::operation_registry_t::clock_t::now () + 1s,
      [&] (foundation::operation_terminal_t,
           std::vector<std::uint8_t>) { ++terminal_count; }));
    assert (operations.complete (id, {1}));
    assert (!operations.cancel (id));
    assert (terminal_count == 1);
}

void verify_raw_spot_and_actor_routing ()
{
    mesh::raw_mesh_node_owner_t source (
      mesh::raw_mesh_node_options_t{descriptor ("raw-source")});
    mesh::raw_mesh_node_owner_t target (
      mesh::raw_mesh_node_options_t{descriptor ("raw-target")});
    source.start ();
    target.start ();
    const auto source_descriptor = source.topology ().local_descriptor ();
    const auto target_descriptor = target.topology ().local_descriptor ();
    const auto deadline =
      mesh::service_liveness_registry_t::clock_t::now () + 5s;
    assert (source.connect_peer (target.endpoint (), target_descriptor));
    while ((!source.topology ().peer (target_descriptor.node_routing_id)
            || !target.topology ().peer (source_descriptor.node_routing_id))
           && mesh::service_liveness_registry_t::clock_t::now ()
                < deadline) {
        const auto now =
          mesh::service_liveness_registry_t::clock_t::now ();
        (void) source.drain_monitor_events (now);
        (void) target.drain_monitor_events (now);
        (void) source.pump_one (now);
        (void) target.pump_one (now);
        std::this_thread::sleep_for (1ms);
    }
    assert (source.topology ().peer (target_descriptor.node_routing_id));
    assert (target.topology ().peer (source_descriptor.node_routing_id));

    stateful::stateful_object_runtime_t objects;
    objects.replace_placement_candidates (
      {{"m6b-mesh", "raw-target", {"room", "player"},
        100, 16, 0, 8, 0}});
    const auto spot = create_ready (
      objects,
      {stateful::object_kind_t::user_spot,
       "spot-1",
       "room",
       std::nullopt,
       {},
       false,
       false});
    const auto actor = create_ready (
      objects,
      {stateful::object_kind_t::actor,
       "actor-1",
       "player",
       std::nullopt,
       {},
       false,
       false});
    stateful::raw_stateful_dispatch_t dispatch (objects, target);

    const protocol::spot_route_fence_t spot_fence{
      "spot-1",
      spot.object_generation,
      target_descriptor.node_routing_id,
      target_descriptor.lifecycle_generation,
      spot.authority_owner_generation};
    assert (source.send_to_spot (
      target_descriptor.node_routing_id, "source-spot",
      spot_fence, {"SpotPacket", "application/json", bytes ("spot")}));
    mesh::raw_mesh_pump_result_t spot_pump =
      mesh::raw_mesh_pump_result_t::no_data;
    while (spot_pump != mesh::raw_mesh_pump_result_t::application
           && mesh::service_liveness_registry_t::clock_t::now ()
                < deadline) {
        spot_pump = target.pump_one (
          mesh::service_liveness_registry_t::clock_t::now ());
        assert (spot_pump != mesh::raw_mesh_pump_result_t::protocol_error);
    }
    assert (spot_pump == mesh::raw_mesh_pump_result_t::application);
    assert (dispatch.ingest (spot)
            == stateful::stateful_error_t::none);
    const auto [spot_delivery_error, spot_delivery] =
      dispatch.try_claim (spot);
    assert (spot_delivery_error == stateful::stateful_error_t::none);
    assert (spot_delivery
            && spot_delivery->payload.payload == bytes ("spot"));
    assert (dispatch.complete (*spot_delivery)
            == stateful::stateful_error_t::none);

    const protocol::actor_route_fence_t actor_fence{
      "actor-1",
      actor.object_generation,
      target_descriptor.node_routing_id,
      target_descriptor.lifecycle_generation,
      actor.authority_owner_generation};
    using request_result_t =
      std::pair<foundation::operation_terminal_t,
                std::vector<std::uint8_t>>;
    std::promise<request_result_t> promise;
    auto future = promise.get_future ();
    assert (source.request_to_actor (
      target_descriptor.node_routing_id, std::nullopt, actor_fence,
      {"ActorPacket", "application/json", bytes ("request")}, 2s,
      [&promise] (foundation::operation_terminal_t terminal,
                  std::vector<std::uint8_t> payload) {
          promise.set_value ({terminal, std::move (payload)});
      }));
    mesh::raw_mesh_pump_result_t actor_pump =
      mesh::raw_mesh_pump_result_t::no_data;
    while (actor_pump != mesh::raw_mesh_pump_result_t::application
           && mesh::service_liveness_registry_t::clock_t::now ()
                < deadline) {
        actor_pump = target.pump_one (
          mesh::service_liveness_registry_t::clock_t::now ());
        assert (actor_pump != mesh::raw_mesh_pump_result_t::protocol_error);
    }
    assert (actor_pump == mesh::raw_mesh_pump_result_t::application);
    assert (dispatch.ingest (actor)
            == stateful::stateful_error_t::none);
    const auto [actor_delivery_error, actor_delivery] =
      dispatch.try_claim (actor);
    assert (actor_delivery_error == stateful::stateful_error_t::none);
    assert (actor_delivery && actor_delivery->request);
    assert (actor_delivery->payload.payload == bytes ("request"));
    assert (dispatch.complete (
              *actor_delivery,
              protocol::application_payload_t{
                "ActorReply", "application/json", bytes ("reply")})
            == stateful::stateful_error_t::none);

    while (future.wait_for (0ms) != std::future_status::ready
           && mesh::service_liveness_registry_t::clock_t::now ()
                < deadline) {
        const auto pump = source.pump_one (
          mesh::service_liveness_registry_t::clock_t::now ());
        assert (pump != mesh::raw_mesh_pump_result_t::protocol_error);
        std::this_thread::sleep_for (1ms);
    }
    assert (future.wait_for (0ms) == std::future_status::ready);
    const auto result = future.get ();
    assert (result.first
            == foundation::operation_terminal_t::completed);
    assert (protocol::decode_application_payload (result.second).payload
            == bytes ("reply"));

    auto stale_fence = actor_fence;
    ++stale_fence.object_generation;
    std::promise<request_result_t> stale_promise;
    auto stale_future = stale_promise.get_future ();
    std::size_t stale_terminal_count = 0;
    assert (source.request_to_actor (
      target_descriptor.node_routing_id, std::nullopt, stale_fence,
      {"ActorPacket", "application/json", bytes ("stale")}, 2s,
      [&stale_promise, &stale_terminal_count] (
        foundation::operation_terminal_t terminal,
        std::vector<std::uint8_t> payload) {
          ++stale_terminal_count;
          stale_promise.set_value ({terminal, std::move (payload)});
      }));
    actor_pump = mesh::raw_mesh_pump_result_t::no_data;
    while (actor_pump != mesh::raw_mesh_pump_result_t::application
           && mesh::service_liveness_registry_t::clock_t::now ()
                < deadline) {
        actor_pump = target.pump_one (
          mesh::service_liveness_registry_t::clock_t::now ());
        assert (actor_pump != mesh::raw_mesh_pump_result_t::protocol_error);
    }
    assert (actor_pump == mesh::raw_mesh_pump_result_t::application);
    assert (dispatch.ingest (actor)
            == stateful::stateful_error_t::generation_stale);
    while (stale_future.wait_for (0ms) != std::future_status::ready
           && mesh::service_liveness_registry_t::clock_t::now ()
                < deadline) {
        const auto pump = source.pump_one (
          mesh::service_liveness_registry_t::clock_t::now ());
        assert (pump != mesh::raw_mesh_pump_result_t::protocol_error);
        std::this_thread::sleep_for (1ms);
    }
    assert (stale_future.wait_for (0ms)
            == std::future_status::ready);
    assert (stale_future.get ().first
            == foundation::operation_terminal_t::transport_failed);
    assert (stale_terminal_count == 1);
    source.close ();
    target.close ();
}

void verify_remote_user_spot_create_close_terminal_once ()
{
    using namespace zlink::framework;
    auto store =
      std::make_shared<zlink::framework::runtime::
                         in_memory_location_store_t> ();
    const auto claimed =
      store->claim_owner_lease ("target-owner", 30s)
        .result ()
        .value ();
    const auto *owner =
      std::get_if<owner_lease_claimed_t> (&claimed);
    assert (owner);
    mesh_node_descriptor_t target_location{
      .mesh_name = "m6b-mesh",
      .rid = zlink::routing_id_t::from ("user-target"),
      .lifecycle_generation = 1,
      .descriptor_revision = 1,
      .endpoint = "tcp://127.0.0.1:1",
      .application_version = 1,
      .object_capabilities =
        {{.object_kind = placement_object_kind_t::user_spot,
          .stable_type = "room",
          .policy = maintenance_policy_kind_t::recreate}},
      .object_role = object_role_t::server,
      .capacity = {.spots = {.limit = 100}},
      .state = framework_runtime_state_t::serving,
      .security_identity = "test",
      .owner_id = owner->token.owner_id,
      .lease_generation = owner->token.lease_generation};
    assert (
      store
        ->update_mesh_node (
          target_location, location_write_intent_t::new_claim)
        .result ()
        .value ()
        .status == location_write_status_t::stored);

    const std::string spot_id = "remote-room";
    const std::vector<std::byte> creation_payload{
      std::byte{0x41}, std::byte{0x42}};
    const object_reserve_request_t reserve{
      .key = {placement_object_kind_t::user_spot,
              spot_id},
      .intent =
        {.stable_type = "room",
         .request_content_reference =
           "inline-v1:bd9444ea:QUI",
         .request_sha256 =
           zlink::framework::runtime::sha256 (creation_payload),
         .request_encoded_size = 2},
      .target =
        {.mesh_name = "m6b-mesh",
         .node_rid = node_rid_t::from_string ("user-target"),
         .node_lifecycle_generation = 1,
         .owner = owner->token},
      .creating_payload = {std::byte{0x7f}},
      .capacity_bundle = {
        .spot_slots = 1,
        .spot_type = spot_type_capacity_delta_t{
          .object_kind = placement_object_kind_t::user_spot,
          .stable_type = "room",
          .slots = 1}}};
    const auto reserved =
      store->reserve (reserve).result ().value ();
    const auto *reservation =
      std::get_if<object_reserved_t> (&reserved);
    assert (reservation);
    assert (reservation->creating.pending_creation);
    assert (
      reservation->creating.pending_creation->reservation_id
      == reservation->fence.reservation_id);
    assert (
      reservation->creating.pending_creation
        ->request_content_reference
      == reserve.intent.request_content_reference);
    const std::string invalid_spot_id = "remote-room-invalid";
    auto invalid_reserve = reserve;
    invalid_reserve.key.global_id = invalid_spot_id;
    invalid_reserve.intent.request_encoded_size = 3;
    const auto invalid_reserved =
      store->reserve (invalid_reserve).result ().value ();
    const auto *invalid_reservation =
      std::get_if<object_reserved_t> (&invalid_reserved);
    assert (invalid_reservation);
    const std::string cleanup_spot_id = "remote-room-cleanup";
    auto cleanup_reserve = reserve;
    cleanup_reserve.key.global_id = cleanup_spot_id;
    const auto cleanup_reserved =
      store->reserve (cleanup_reserve).result ().value ();
    const auto *cleanup_reservation =
      std::get_if<object_reserved_t> (&cleanup_reserved);
    assert (cleanup_reservation);
    assert (
      zlink::framework::runtime::
        cleanup_source_created_reservation (
          store, cleanup_reserve.key,
          cleanup_reservation->fence, false)
      == zlink::framework::runtime::
           source_creation_cleanup_t::not_owned);
    auto wrong_cleanup_fence = cleanup_reservation->fence;
    wrong_cleanup_fence.reservation_id += "-other";
    assert (
      zlink::framework::runtime::
        cleanup_source_created_reservation (
          store, cleanup_reserve.key,
          wrong_cleanup_fence, true)
      == zlink::framework::runtime::
           source_creation_cleanup_t::stale);
    assert (
      zlink::framework::runtime::
        cleanup_source_created_reservation (
          store, cleanup_reserve.key,
          cleanup_reservation->fence, true)
      == zlink::framework::runtime::
           source_creation_cleanup_t::aborted);
    assert (
      std::holds_alternative<authority_missing_t> (
        store
          ->read_authority (
            {"2:" + cleanup_spot_id})
          .result ()
          .value ()));

    auto source = std::make_shared<host::public_host_runtime_t> (
      host::host_options_t{
        mesh::raw_mesh_node_options_t{descriptor ("user-source")}});
    host::host_options_t target_options{
      mesh::raw_mesh_node_options_t{descriptor ("user-target")}};
    target_options.user_spot_operation_capacity = 1;
    target_options.user_spot_operation_replay_retention = 50ms;
    auto target = std::make_shared<host::public_host_runtime_t> (
      std::move (target_options));
    std::size_t materialize_count = 0;
    target->configure_user_spot_operations (
      store,
      [&materialize_count] (
        const stateful::object_ref_t &object,
        const std::string &stable_type,
        const std::vector<std::byte> &creation) {
          ++materialize_count;
          assert (object.object_generation != 0);
          assert (stable_type == "room");
          assert (creation
                  == std::vector<std::byte> (
                    {std::byte{0x41}, std::byte{0x42}}));
          return host::user_spot_materialize_result_t{
            true, std::nullopt};
      });
    source->start ();
    target->start ();
    assert (source->connect_peer (
      target->status ().local_endpoint (),
      target->status ().routing_id ()));
    const auto deadline =
      std::chrono::steady_clock::now () + 5s;
    auto dispatch = [] (const host::ready_record_t &,
                        const host::receive_record_t &,
                        std::vector<zlink::message_t>) {};
    while ((!source->transport ().topology ().peer (
              target->status ().routing_id ().to_bytes ())
            || !target->transport ().topology ().peer (
              source->status ().routing_id ().to_bytes ()))
           && std::chrono::steady_clock::now () < deadline) {
        (void) source->dispatch_ready (dispatch);
        (void) target->dispatch_ready (dispatch);
        std::this_thread::sleep_for (1ms);
    }
    assert (source->transport ().topology ().peer (
      target->status ().routing_id ().to_bytes ()));

    const auto unix_deadline =
      static_cast<std::uint64_t> (
        std::chrono::duration_cast<std::chrono::milliseconds> (
          std::chrono::system_clock::now ().time_since_epoch ()
          + 100ms)
          .count ());
    protocol::user_spot_create_header_t create{
      1,
      {99, 1},
      source->status ().routing_id ().to_bytes (),
      source->status ().lifecycle_generation (),
      spot_id,
      "room",
      {reservation->fence.reservation_id,
       reservation->fence.expected_store_version,
       reservation->fence.object_generation,
       reservation->fence.authority_owner_generation,
       target->status ().routing_id ().to_bytes (),
       target->status ().lifecycle_generation (),
       reservation->fence.target.owner.owner_id,
       static_cast<std::uint64_t> (
         reservation->fence.target.owner.lease_generation),
       reservation->fence.capacity_bundle.spot_slots},
      unix_deadline};
    auto invalid_create = create;
    invalid_create.operation = {98, 1};
    invalid_create.spot_id = invalid_spot_id;
    invalid_create.deadline_unix_ms =
      static_cast<std::uint64_t> (
        std::chrono::duration_cast<std::chrono::milliseconds> (
          std::chrono::system_clock::now ().time_since_epoch ()
          + 20ms)
          .count ());
    invalid_create.reservation = {
      invalid_reservation->fence.reservation_id,
      invalid_reservation->fence.expected_store_version,
      invalid_reservation->fence.object_generation,
      invalid_reservation->fence.authority_owner_generation,
      target->status ().routing_id ().to_bytes (),
      target->status ().lifecycle_generation (),
      invalid_reservation->fence.target.owner.owner_id,
      static_cast<std::uint64_t> (
        invalid_reservation->fence.target.owner.lease_generation),
      invalid_reservation->fence.capacity_bundle.spot_slots};
    std::optional<protocol::user_spot_create_reply_t>
      invalid_reply;
    assert (source->create_user_spot_remote (
      target->status ().routing_id (), invalid_create, 5s,
      [&] (foundation::operation_terminal_t terminal,
           protocol::user_spot_create_reply_t reply,
           std::optional<protocol::application_payload_t>) {
          assert (
            terminal
            == foundation::operation_terminal_t::completed);
          invalid_reply = std::move (reply);
      }));
    while (!invalid_reply
           && std::chrono::steady_clock::now () < deadline) {
        (void) target->dispatch_ready (dispatch);
        (void) source->dispatch_ready (dispatch);
        std::this_thread::sleep_for (1ms);
    }
    assert (invalid_reply);
    assert (invalid_reply->header.terminal_result == 105);
    assert (materialize_count == 0);
    std::this_thread::sleep_for (80ms);
    auto mismatch_create = create;
    mismatch_create.operation = {97, 1};
    mismatch_create.stable_type = "other-room";
    mismatch_create.deadline_unix_ms =
      static_cast<std::uint64_t> (
        std::chrono::duration_cast<std::chrono::milliseconds> (
          std::chrono::system_clock::now ().time_since_epoch ()
          + 100ms)
          .count ());
    std::optional<protocol::user_spot_create_reply_t>
      mismatch_reply;
    assert (source->create_user_spot_remote (
      target->status ().routing_id (), mismatch_create, 5s,
      [&] (foundation::operation_terminal_t terminal,
           protocol::user_spot_create_reply_t reply,
           std::optional<protocol::application_payload_t>) {
          assert (
            terminal
            == foundation::operation_terminal_t::completed);
          mismatch_reply = std::move (reply);
      }));
    while (!mismatch_reply
           && std::chrono::steady_clock::now () < deadline) {
        (void) target->dispatch_ready (dispatch);
        (void) source->dispatch_ready (dispatch);
        std::this_thread::sleep_for (1ms);
    }
    assert (mismatch_reply);
    assert (mismatch_reply->header.terminal_result == 107);
    assert (
      mismatch_reply->header.failure_code
      == static_cast<std::uint32_t> (
        protocol::framework_error_code::spotTypeMismatch));
    std::optional<protocol::user_spot_create_reply_t>
      replayed_mismatch_reply;
    assert (source->create_user_spot_remote (
      target->status ().routing_id (), mismatch_create, 5s,
      [&] (foundation::operation_terminal_t terminal,
           protocol::user_spot_create_reply_t reply,
           std::optional<protocol::application_payload_t>) {
          assert (
            terminal
            == foundation::operation_terminal_t::completed);
          replayed_mismatch_reply = std::move (reply);
      }));
    while (!replayed_mismatch_reply
           && std::chrono::steady_clock::now () < deadline) {
        (void) target->dispatch_ready (dispatch);
        (void) source->dispatch_ready (dispatch);
        std::this_thread::sleep_for (1ms);
    }
    assert (replayed_mismatch_reply);
    assert (
      replayed_mismatch_reply->header.failure_code
      == static_cast<std::uint32_t> (
        protocol::framework_error_code::spotTypeMismatch));
    assert (materialize_count == 0);
    std::this_thread::sleep_for (200ms);
    create.deadline_unix_ms =
      static_cast<std::uint64_t> (
        std::chrono::duration_cast<std::chrono::milliseconds> (
          std::chrono::system_clock::now ().time_since_epoch ()
          + 100ms)
          .count ());
    std::optional<protocol::user_spot_create_reply_t>
      create_reply;
    std::size_t create_terminal_count = 0;
    assert (source->create_user_spot_remote (
      target->status ().routing_id (), create, 5s,
      [&] (foundation::operation_terminal_t terminal,
           protocol::user_spot_create_reply_t reply,
           std::optional<protocol::application_payload_t>) {
          assert (
            terminal
            == foundation::operation_terminal_t::completed);
          ++create_terminal_count;
          create_reply = std::move (reply);
      }));
    while (!create_reply
           && std::chrono::steady_clock::now () < deadline) {
        (void) target->dispatch_ready (dispatch);
        (void) source->dispatch_ready (dispatch);
        std::this_thread::sleep_for (1ms);
    }
    assert (create_reply);
    assert (create_reply->header.terminal_result == 0);
    assert (
      create_reply->result
      == protocol::user_spot_create_result_t::created);
    assert (create_terminal_count == 1);
    assert (materialize_count == 1);
    assert (
      zlink::framework::runtime::
        cleanup_source_created_reservation (
          store, reserve.key, reservation->fence, true)
      == zlink::framework::runtime::
           source_creation_cleanup_t::stale);
    const auto committed_authority =
      store
        ->read_authority (
          {"2:" + spot_id})
        .result ()
        .value ();
    const auto *committed_snapshot =
      std::get_if<authority_snapshot_t> (
        &committed_authority);
    assert (
      committed_snapshot
      && committed_snapshot->allocation.state
           == placement_allocation_state_t::active);
    std::optional<protocol::user_spot_create_reply_t>
      replayed_create_reply;
    assert (source->create_user_spot_remote (
      target->status ().routing_id (), create, 5s,
      [&] (foundation::operation_terminal_t terminal,
           protocol::user_spot_create_reply_t reply,
           std::optional<protocol::application_payload_t>) {
          assert (
            terminal
            == foundation::operation_terminal_t::completed);
          replayed_create_reply = std::move (reply);
      }));
    while (!replayed_create_reply
           && std::chrono::steady_clock::now () < deadline) {
        (void) target->dispatch_ready (dispatch);
        (void) source->dispatch_ready (dispatch);
        std::this_thread::sleep_for (1ms);
    }
    assert (replayed_create_reply);
    assert (
      replayed_create_reply->result
      == protocol::user_spot_create_result_t::created);
    assert (materialize_count == 1);

    auto capacity_create = create;
    capacity_create.operation = {99, 3};
    std::optional<protocol::user_spot_create_reply_t>
      capacity_reply;
    assert (source->create_user_spot_remote (
      target->status ().routing_id (), capacity_create, 5s,
      [&] (foundation::operation_terminal_t terminal,
           protocol::user_spot_create_reply_t reply,
           std::optional<protocol::application_payload_t>) {
          assert (
            terminal
            == foundation::operation_terminal_t::completed);
          capacity_reply = std::move (reply);
      }));
    while (!capacity_reply
           && std::chrono::steady_clock::now () < deadline) {
        (void) target->dispatch_ready (dispatch);
        (void) source->dispatch_ready (dispatch);
        std::this_thread::sleep_for (1ms);
    }
    assert (capacity_reply);
    assert (capacity_reply->header.terminal_result == 103);
    assert (materialize_count == 1);
    std::this_thread::sleep_for (200ms);
    std::optional<protocol::user_spot_create_reply_t>
      expired_reply;
    assert (source->create_user_spot_remote (
      target->status ().routing_id (), create, 5s,
      [&] (foundation::operation_terminal_t terminal,
           protocol::user_spot_create_reply_t reply,
           std::optional<protocol::application_payload_t>) {
          assert (
            terminal
            == foundation::operation_terminal_t::completed);
          expired_reply = std::move (reply);
      }));
    while (!expired_reply
           && std::chrono::steady_clock::now () < deadline) {
        (void) target->dispatch_ready (dispatch);
        (void) source->dispatch_ready (dispatch);
        std::this_thread::sleep_for (1ms);
    }
    assert (expired_reply);
    assert (expired_reply->header.terminal_result == 101);
    assert (materialize_count == 1);

    const auto authority =
      store
        ->read_authority (
          {"2:" + spot_id})
        .result ()
        .value ();
    const auto *ready =
      std::get_if<authority_snapshot_t> (&authority);
    assert (ready);
    assert (ready->allocation.state
            == placement_allocation_state_t::active);
    protocol::user_spot_close_header_t close{
      1,
      {99, 2},
      source->status ().routing_id ().to_bytes (),
      source->status ().lifecycle_generation (),
      {spot_id,
       ready->object_generation,
       target->status ().routing_id ().to_bytes (),
       target->status ().lifecycle_generation (),
       ready->authority_owner_generation,
       ready->store_version},
      static_cast<std::uint64_t> (
        std::chrono::duration_cast<std::chrono::milliseconds> (
          std::chrono::system_clock::now ().time_since_epoch ()
          + 5s)
          .count ())};
    std::optional<protocol::user_spot_close_reply_t>
      close_reply;
    assert (source->close_user_spot_remote (
      target->status ().routing_id (), close, 5s,
      [&] (foundation::operation_terminal_t terminal,
           protocol::user_spot_close_reply_t reply) {
          assert (
            terminal
            == foundation::operation_terminal_t::completed);
          close_reply = std::move (reply);
      }));
    while (!close_reply
           && std::chrono::steady_clock::now () < deadline) {
        (void) target->dispatch_ready (dispatch);
        (void) source->dispatch_ready (dispatch);
        std::this_thread::sleep_for (1ms);
    }
    assert (close_reply && close_reply->closed);
    assert (std::holds_alternative<authority_missing_t> (
      store
        ->read_authority (
          {"2:" + spot_id})
        .result ()
        .value ()));
    source->close ();
    target->close ();
}

} // namespace

int main ()
{
    verify_spot_id_contract ();
    verify_user_spot_execution_mode_registration ();
    verify_self_actor_request_rejected_before_submission ();
    verify_same_gate_request_rejected_before_submission ();
    verify_creation_terminal_operation_isolation ();
    verify_typed_capacity_retry_uses_second_candidate ();
    verify_global_identity_remote_create_and_generation_fence ();
    verify_membership_turns_and_independent_infrastructure ();
    verify_instance_cold_activation_only_from_intent ();
    verify_session_binding_and_terminal_once ();
    verify_raw_spot_and_actor_routing ();
    verify_remote_user_spot_create_close_terminal_once ();
    return 0;
}
