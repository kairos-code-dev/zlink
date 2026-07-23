/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/foundation/operation_registry.hpp"
#include "runtime/mesh/raw_mesh_node_owner.hpp"
#include "runtime/stateful/raw_stateful_dispatch.hpp"
#include "runtime/stateful/stateful_object_runtime.hpp"
#include "runtime/stateful/stream_session_registry.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <future>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace foundation = zlink::framework::runtime::foundation;
namespace mesh = zlink::framework::runtime::mesh;
namespace protocol = zlink::framework::runtime::protocol;
namespace stateful = zlink::framework::runtime::stateful;

using namespace std::chrono_literals;

namespace
{

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
    assert (second && second->sequence == 2);
    assert (runtime.complete_claim (
              moved_actor, stateful::turn_domain_t::application)
            == stateful::stateful_error_t::none);
    const auto [continuation_error, continuation] = runtime.try_claim (
      moved_actor, stateful::turn_domain_t::application);
    assert (continuation_error == stateful::stateful_error_t::none);
    assert (continuation && continuation->sequence == 3);
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

    stateful::stream_session_registry_t sessions (
      [&] (const std::string &actor_id) {
          return runtime.find (stateful::object_kind_t::actor, actor_id);
      });
    const auto connection = sessions.open ("stream-rid");
    const auto [bind_error, binding] = sessions.bind (connection, actor);
    assert (bind_error == stateful::stateful_error_t::none);
    const auto [dispatch_error, dispatch] =
      sessions.admit_inbound (binding);
    assert (dispatch_error == stateful::stateful_error_t::none);
    assert (dispatch && dispatch->inbound_sequence == 1);

    const auto reconnect = sessions.open ("stream-rid");
    assert (reconnect.connection_generation
            == connection.connection_generation + 1);
    assert (!sessions.is_current (binding));
    assert (sessions.admit_inbound (binding).first
            == stateful::stateful_error_t::conflict);

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
      bytes ("spot-1"),
      spot.object_generation,
      target_descriptor.node_routing_id,
      target_descriptor.lifecycle_generation,
      spot.authority_owner_generation};
    assert (source.send_to_spot (
      target_descriptor.node_routing_id, bytes ("source-spot"),
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

} // namespace

int main ()
{
    verify_global_identity_remote_create_and_generation_fence ();
    verify_membership_turns_and_independent_infrastructure ();
    verify_instance_cold_activation_only_from_intent ();
    verify_session_binding_and_terminal_once ();
    verify_raw_spot_and_actor_routing ();
    return 0;
}
