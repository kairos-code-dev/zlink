/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "spot_runtime.hpp"

#include <zlink/framework/contracts/configuration/zlink_builder.hpp>

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/channels/channel_reply_writer.hpp"
#include "runtime/channels/channel_runtime.hpp"
#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/channels/route_channel_runtime.hpp"
#include "runtime/diagnostics/dispatch_error_reporter.hpp"
#include "runtime/diagnostics/monitoring_runtime.hpp"
#include "runtime/diagnostics/runtime_metrics.hpp"
#include "runtime/diagnostics/message_flow_tracer.hpp"
#include "runtime/dispatch/offload_executor.hpp"
#include "runtime/execution/serial_execution_queue.hpp"
#include "runtime/messaging/envelope_codec.hpp"
#include "runtime/spots/spot_route_internal_dispatcher.hpp"
#include "runtime/diagnostics/flow_context.hpp"
#include "runtime/spots/spot_route_packets.hpp"
#include "runtime/streams/stream_runtime.hpp"
#include "runtime/timers/timer_runtime.hpp"

#include <zlink.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace zlink::framework
{

namespace
{

constexpr std::uint32_t actor_recv_info_no_bind_flag = 1u;

framework_error_kind_t submit_result_error_kind (zlink::submit_result_t result)
{
    switch (result) {
        case zlink::submit_result_t::not_connected:
            return framework_error_kind_t::route_not_connected;
        case zlink::submit_result_t::backpressured:
            return framework_error_kind_t::request_failed;
        case zlink::submit_result_t::invalid_argument:
        case zlink::submit_result_t::invalid_handle:
            return framework_error_kind_t::request_protocol_error;
        default:
            return framework_error_kind_t::request_failed;
    }
}

bool is_blank (const std::string &value)
{
    return std::all_of (value.begin (), value.end (),
                        [] (unsigned char ch) { return std::isspace (ch) != 0; });
}

class spot_worker_scheduler_t final : public detail::worker_scheduler_t
{
  public:
    spot_worker_scheduler_t (std::shared_ptr<runtime::offload_executor_t> workers,
                             std::weak_ptr<detail::spot_context_state_t> owner) :
        _workers (std::move (workers)), _owner (std::move (owner))
    {
    }

    bool try_schedule (std::function<void ()> work) override
    {
        return _workers && _workers->try_submit (std::move (work));
    }

    void post_owner (std::function<void ()> work) override
    {
        if (auto owner = _owner.lock ()) {
            (void) owner->try_post_serial ("worker-completion", std::move (work));
        }
    }

  private:
    std::shared_ptr<runtime::offload_executor_t> _workers;
    std::weak_ptr<detail::spot_context_state_t> _owner;
};

std::shared_ptr<runtime::offload_executor_t>
framework_worker_executor (const std::shared_ptr<detail::spot_node_builder_state_t> &node)
{
    if (node && node->worker_executor) {
        return node->worker_executor;
    }
    const auto hardware_workers =
      static_cast<std::size_t> (std::max (1u, std::thread::hardware_concurrency ()));
    auto executor = std::make_shared<runtime::offload_executor_t> (
      0, hardware_workers * 2, 1024, std::chrono::seconds (30), "zlink-spot-wrk");
    if (node) {
        node->worker_executor = executor;
    }
    return executor;
}

std::shared_ptr<detail::worker_scheduler_t>
make_spot_worker_scheduler (const std::shared_ptr<detail::spot_context_state_t> &owner)
{
    return std::make_shared<spot_worker_scheduler_t> (framework_worker_executor (owner->node),
                                                      owner);
}

void configure_spot_execution (const std::shared_ptr<detail::spot_context_state_t> &state)
{
    state->serial_executor = std::make_shared<runtime::offload_executor_t> (1, 0, "zlink-spot-ser");
    state->serial_queue =
      std::make_shared<runtime::serial_execution_queue_t> (*state->serial_executor);
    state->worker_scheduler = make_spot_worker_scheduler (state);
}

} // namespace

namespace detail
{

spot_node_builder_state_t::~spot_node_builder_state_t () = default;

void drain_spot_node_executors (spot_node_builder_state_t &node)
{
    for (auto &[_, context] : node.spot_contexts_by_rid) {
        auto state = context._state;
        if (state && state->serial_queue) {
            state->serial_queue->cancel_pending ();
        }
    }
    if (node.worker_executor) {
        node.worker_executor->drain ();
        node.worker_executor.reset ();
    }
    for (auto &[_, context] : node.spot_contexts_by_rid) {
        auto state = context._state;
        if (state && state->serial_executor) {
            if (state->serial_queue) {
                state->serial_queue->cancel_pending ();
                state->serial_queue->drain ();
                state->serial_queue.reset ();
            }
            state->serial_executor->drain ();
            state->serial_executor.reset ();
            state->worker_scheduler.reset ();
        }
    }
}

void cancel_spot_node_dispatch_queues (spot_node_builder_state_t &node)
{
    for (auto &[_, context] : node.spot_contexts_by_rid) {
        auto state = context._state;
        if (state && state->serial_queue) {
            state->serial_queue->cancel_pending ();
        }
    }
}

} // namespace detail

namespace
{

zlink::message_t framework_reply_or_empty (const std::optional<message_t> &reply,
                                           serializer_registry_t &serializers)
{
    return reply ? detail::message_to_raw (*reply, serializers) : zlink::message_t{};
}

void attach_native_spot_locked (const std::shared_ptr<detail::spot_context_state_t> &state)
{
    if (!state || !state->node) {
        return;
    }
    auto native_node = state->node->native_node.lock ();
    if (!native_node) {
        return;
    }

    const auto rid = std::string (state->spot_rid.value ());
    auto native = state->native_spot.lock ();
    if (!native) {
        const auto found = state->node->native_spots_by_rid.find (rid);
        if (found == state->node->native_spots_by_rid.end ()) {
            if (state->node->snapshot.entry_spot_name
                && *state->node->snapshot.entry_spot_name == state->spot_name) {
                native = std::make_shared<zlink::service::spot_t> (native_node->entry_spot ());
                const auto expected_rid = zlink::routing_id_t::from (
                  detail::effective_spot_node_rid (state->node->snapshot));
                if (native->routing_id ().to_string () != expected_rid.to_string ()) {
                    native->set_routing_id (expected_rid);
                }
            } else {
                try {
                    auto [spot, created] =
                      native_node->get_or_create_spot (zlink::routing_id_t::from (rid));
                    (void) created;
                    native = std::make_shared<zlink::service::spot_t> (std::move (spot));
                }
                catch (const std::exception &error) {
                    throw framework_exception_t (
                      framework_error_kind_t::spot_create_failed,
                      "native spot facade creation failed for '" + state->spot_name + "' (rid='"
                        + rid + "'): " + error.what ());
                }
            }
            state->node->native_spots_by_rid.emplace (rid, native);
        } else {
            native = found->second;
        }
        state->native_spot = native;
        try {
            native->set_dispatch_handler (
              [weak_state = std::weak_ptr<detail::spot_context_state_t> (state),
               weak_native_node = std::weak_ptr<zlink::service::spot_node_t> (native_node),
               weak_native = std::weak_ptr<zlink::service::spot_t> (native)] (
                const zlink::spot_dispatch_info_t &info) {
              auto owner = weak_state.lock ();
              if (!owner || !owner->node) {
                  return;
              }
              std::lock_guard<std::recursive_mutex> node_lock (owner->node->mutex);
              auto native_node = weak_native_node.lock ();
              auto native = weak_native.lock ();
              if (!native_node || !native) {
                  return;
              }
              auto current_native = owner->native_spot.lock ();
              if (!current_native || current_native.get () != native.get ()) {
                  return;
              }
              if (info.event == zlink::spot_dispatch_event_t::routed_readable) {
                  while (true) {
                      zlink::received_t inbound;
                      const int rc = native->recv_routed (inbound, zlink::recv_flags_t::dontwait);
                      if (rc != static_cast<int> (zlink::recv_result_t::ok)) {
                          break;
                      }
                      owner->queued_routed_packets.push_back (std::move (inbound));
                  }
                  return;
              }
              if (info.event != zlink::spot_dispatch_event_t::actor_readable || !info.actor) {
                  return;
              }
              while (true) {
                  auto first =
                    native_node->recv_actor_part (*info.actor, zlink::recv_flags_t::dontwait);
                  if (!first) {
                      break;
                  }
                  detail::spot_node_builder_state_t::queued_actor_packet_t packet;
                  packet.info = first->info;
                  packet.parts.push_back (std::move (first->part));
                  auto has_more = first->has_more;
                  while (has_more) {
                      auto next =
                        native_node->recv_actor_part (*info.actor, zlink::recv_flags_t::dontwait);
                      if (!next) {
                          break;
                      }
                      has_more = next->has_more;
                      packet.parts.push_back (std::move (next->part));
                  }
                  owner->node->queued_actor_packets.push_back (std::move (packet));
              }
              });
        }
        catch (const std::exception &error) {
            throw framework_exception_t (
              framework_error_kind_t::spot_create_failed,
              "native spot dispatch activation failed for '" + state->spot_name + "' (rid='"
                + rid + "'): " + error.what ());
        }
    }

    for (const auto &handler : state->handlers) {
        if (handler.kind == spot_handler_kind_t::subscription && !handler.topic.empty ()) {
            /* The node creates its subscription receiver lazily on the first
               subscription, and that creation waits a bounded time for the
               inproc attachment pipe. Under congestion the wait can expire, and
               the node reports every creation failure as "not supported"
               (ledger CPP-SPOT-SUB-ACT-001), so a spot that merely arrived at a
               busy moment would fail to be created at all. The failure is
               transient by nature: retry a few times before giving up. */
            constexpr int activation_attempts = 5;
            std::string last_error;
            bool activated = false;
            for (int attempt = 0; attempt < activation_attempts && !activated; ++attempt) {
                try {
                    native->set_subscription (handler.topic);
                    activated = true;
                }
                catch (const std::exception &error) {
                    last_error = error.what ();
                    std::this_thread::sleep_for (std::chrono::milliseconds (100));
                }
            }
            if (!activated) {
                throw framework_exception_t (
                  framework_error_kind_t::spot_create_failed,
                  "native spot subscription activation failed for '" + state->spot_name
                    + "' (rid='" + rid + "', topic='" + handler.topic + "'): " + last_error);
            }
        }
    }
}

void report_spot_dispatch_error (const std::shared_ptr<detail::spot_node_builder_state_t> &state,
                                 dispatch_error_surface_t surface,
                                 dispatch_message_kind_t message_kind,
                                 dispatch_error_reason_t reason,
                                 dispatch_error_action_t action,
                                 std::optional<std::string> packet_name = std::nullopt,
                                 std::optional<std::string> topic = std::nullopt,
                                 std::optional<std::string> spot_rid = std::nullopt,
                                 std::optional<std::string> actor_id = std::nullopt,
                                 std::exception_ptr exception = nullptr)
{
    if (!state) {
        return;
    }
    detail::dispatch_error_reporter_t (state->dispatch)
      .report (message_dispatch_error_event_t{
        surface, message_kind, reason, action, std::move (packet_name), std::nullopt,
        std::move (topic), std::move (spot_rid), std::move (actor_id), std::nullopt, std::nullopt,
        std::move (exception)});
}

void report_spot_dispatch_trace (const std::shared_ptr<detail::spot_node_builder_state_t> &state,
                                 message_flow_outcome_t outcome,
                                 dispatch_error_surface_t surface,
                                 dispatch_message_kind_t message_kind,
                                 std::string_view packet_name = {},
                                 std::string_view topic = {},
                                 std::string_view spot_rid = {},
                                 std::string_view actor_id = {})
{
    if (!state) {
        return;
    }
    // string_view params + lazy build: callers pass cheap views; std::string is
    // only allocated inside the lambda after the gate passes (zero cost when off).
    detail::message_flow_tracer_t (state->dispatch).trace (outcome, [&] {
        auto field = [] (std::string_view value) -> std::optional<std::string> {
            if (value.empty ()) {
                return std::nullopt;
            }
            return std::string (value);
        };
        return message_flow_event_t{
          outcome,          surface,          message_kind, field (packet_name),
          std::nullopt,     field (topic),    std::nullopt, std::nullopt,
          field (spot_rid), field (actor_id), std::nullopt};
    });
}

void decrement_actor_count_unlocked (detail::spot_context_state_t &state)
{
    if (state.actor_count > 0) {
        state.actor_count--;
    }
}

void erase_actor_route_unlocked (detail::spot_node_builder_state_t &state, const std::string &key)
{
    state.actor_spot_rids.erase (key);
    state.actor_routes.erase (key);
    state.actor_generations.erase (key);
    state.native_actors.erase (key);
}

std::uint64_t actor_generation_from_location (const actor_location_t &location)
{
    return location.actor_ref ? location.actor_ref->generation () : 0;
}

actor_location_t make_actor_location (const actor_ref_t &actor,
                                      const detail::spot_context_state_t &context)
{
    return actor_location_t{
      .actor_id = std::string (actor.actor_id ()),
      .actor_type = std::string (actor.actor_type ()),
      .actor_ref = actor,
      .node_rid = zlink::routing_id_t::from (std::string (actor.node_rid ().value ())),
      .location_kind = zlink::spot_kind::user,
      .spot_mesh_name = context.node->snapshot.name,
      .spot_rid = zlink::routing_id_t::from (std::string (context.spot_rid.value ())),
      .generation = 0};
}

actor_location_t make_entry_actor_location (const actor_ref_t &actor,
                                            const detail::spot_context_state_t &context)
{
    return actor_location_t{.actor_id = std::string (actor.actor_id ()),
                            .actor_type = std::string (actor.actor_type ()),
                            .actor_ref = actor,
                            .node_rid =
                              zlink::routing_id_t::from (std::string (context.node_rid.value ())),
                            .location_kind = zlink::spot_kind::entry,
                            .spot_mesh_name = context.node->snapshot.name,
                            .spot_rid = std::nullopt,
                            .generation = 0};
}

spot_location_t make_spot_location (const detail::spot_node_builder_state_t &state,
                                    const std::string &spot_name,
                                    const spot_rid_t &spot_rid)
{
    const auto kind = state.snapshot.entry_spot_name && *state.snapshot.entry_spot_name == spot_name
                        ? zlink::spot_kind::entry
                        : zlink::spot_kind::user;
    return spot_location_t{
      .mesh_name = state.snapshot.name,
      .spot_rid = zlink::routing_id_t::from (std::string (spot_rid.value ())),
      .spot_type = spot_name,
      .node_rid = zlink::routing_id_t::from (detail::effective_spot_node_rid (state.snapshot)),
      .spot_kind = kind,
      .route_endpoint = state.snapshot.router_bind_endpoint};
}

void deactivate_actor_location (std::weak_ptr<detail::spot_node_builder_state_t> weak_state,
                                const actor_location_t &location)
{
    auto state = weak_state.lock ();
    if (!state) {
        return;
    }

    std::function<result_t<void> (const actor_ref_t &)> destroy_actor_registry;
    const auto actor = location.actor_ref.value_or (
      actor_ref_t (node_rid_t::from_string (location.node_rid.to_string ()),
                   location.actor_type.value_or (std::string{}), location.actor_id,
                   actor_generation_from_location (location)));
    const auto key = std::string (actor.actor_type ()) + ":" + std::string (actor.actor_id ());
    {
        std::lock_guard<std::recursive_mutex> node_lock (state->mutex);
        // A lost claim races with a completed transfer: after this node hands the
        // actor to another node it records the newer generation as a forwarding
        // route. A loss notification for an older generation is stale and must not
        // erase that newer record.
        const auto recorded = state->actor_generations.find (key);
        if (recorded != state->actor_generations.end ()
            && recorded->second > actor.generation ()) {
            return;
        }
        erase_actor_route_unlocked (*state, key);
        state->actor_created_keys.erase (key);
        state->destroyed_actor_keys.insert (key);
        state->actor_instances.erase (key);
        detail::erase_actor_instance_index_unlocked (*state, actor.actor_type (),
                                                     actor.actor_id ());
        state->actor_mailboxes.erase (key);
        {
            const std::lock_guard<std::mutex> dedup_lock (state->dispatched_request_replies_mutex);
            state->dispatched_request_replies.erase (key);
        }
        destroy_actor_registry = state->destroy_actor_registry;
    }
    if (destroy_actor_registry) {
        (void) destroy_actor_registry (actor);
    }
}

result_t<void> claim_actor_location_before_activation (
  const std::shared_ptr<detail::spot_node_builder_state_t> &state,
  const actor_ref_t &committed,
  const detail::spot_context_state_t &context,
  bool &claimed,
  bool takeover = false)
{
    claimed = false;
    if (!state->location_lifecycle) {
        return result_t<void>::success ();
    }

    if (state->location_lifecycle->owns_actor (
          actor_location_key_t{std::string (committed.actor_id ())})) {
        return result_t<void>::success ();
    }

    auto location = make_actor_location (committed, context);
    const auto claim_result = state->location_lifecycle->claim_actor (
      location,
      [weak_state = std::weak_ptr<detail::spot_node_builder_state_t> (state)] (
        const actor_location_t &lost) { deactivate_actor_location (weak_state, lost); },
      takeover);
    if (claim_result.status != location_write_status_t::stored) {
        return result_t<void>::failure (claim_result.status
                                            == location_write_status_t::rejected_conflict
                                          ? framework_error_kind_t::actor_already_exists
                                          : framework_error_kind_t::request_failed,
                                        "actor location claim failed");
    }
    claimed = true;
    return result_t<void>::success ();
}

result_t<void> claim_pending_actor_location_before_activation (
  const std::shared_ptr<detail::spot_node_builder_state_t> &state,
  const actor_ref_t &source_actor,
  const spot_rid_t &source_spot_rid,
  const actor_ref_t &committed,
  const detail::spot_context_state_t &target,
  bool &claimed)
{
    claimed = false;
    if (!state->location_lifecycle) {
        return result_t<void>::success ();
    }
    if (state->location_lifecycle->owns_actor (
          actor_location_key_t{std::string (committed.actor_id ())})) {
        return result_t<void>::success ();
    }
    auto location = make_actor_location (committed, target);
    location.actor_ref = source_actor;
    location.node_rid = zlink::routing_id_t::from (std::string (source_actor.node_rid ().value ()));
    location.spot_rid =
      source_spot_rid.empty ()
        ? std::optional<zlink::routing_id_t>{}
        : std::make_optional (zlink::routing_id_t::from (std::string (source_spot_rid.value ())));
    location.location_kind =
      source_spot_rid.empty () ? zlink::spot_kind::entry : zlink::spot_kind::user;
    const auto result = state->location_lifecycle->claim_actor (
      std::move (location),
      [weak_state = std::weak_ptr<detail::spot_node_builder_state_t> (state)] (
        const actor_location_t &lost) { deactivate_actor_location (weak_state, lost); },
      true);
    if (result.status != location_write_status_t::stored) {
        return result_t<void>::failure (result.status == location_write_status_t::rejected_conflict
                                          ? framework_error_kind_t::actor_already_exists
                                          : framework_error_kind_t::request_failed,
                                        "pending actor location claim failed");
    }
    claimed = true;
    return result_t<void>::success ();
}

void release_actor_location (detail::spot_node_builder_state_t &state, const actor_ref_t &actor)
{
    if (!state.location_lifecycle || actor.empty ()) {
        return;
    }
    (void) state.location_lifecycle->release_actor (
      actor_location_key_t{std::string (actor.actor_id ())});
}

result_t<void> update_actor_location_after_move (detail::spot_node_builder_state_t &state,
                                                 const actor_ref_t &actor,
                                                 const detail::spot_context_state_t &context,
                                                 bool entry)
{
    if (!state.location_lifecycle || actor.empty ()) {
        return result_t<void>::success ();
    }
    auto location =
      entry ? make_entry_actor_location (actor, context) : make_actor_location (actor, context);
    const auto tracked = state.location_lifecycle->owns_actor (
      actor_location_key_t{std::string (actor.actor_id ())});
    const auto updated = state.location_lifecycle->update_actor_location (std::move (location));
    if (updated.status != location_write_status_t::stored) {
        return result_t<void>::failure (framework_error_kind_t::request_failed,
                                        "actor committed location update failed: status="
                                          + std::to_string (static_cast<int> (updated.status))
                                          + " tracked=" + (tracked ? "true" : "false"));
    }
    return result_t<void>::success ();
}

std::string spot_mesh_channel_name (const std::shared_ptr<detail::spot_context_state_t> &state)
{
    if (state && !state->spot_name.empty ()) {
        return state->spot_name;
    }
    return "spot-mesh";
}

std::optional<std::string>
optional_spot_route_channel_name (const std::shared_ptr<detail::spot_context_state_t> &state)
{
    if (!state || !state->node || !state->channel_runtime) {
        return std::nullopt;
    }
    std::lock_guard lock (state->channel_runtime->mutex);
    if (state->node->snapshot.spot_route_channel_name) {
        const auto &route_channel_name = *state->node->snapshot.spot_route_channel_name;
        if (state->channel_runtime->route_channels.find (route_channel_name)
            != state->channel_runtime->route_channels.end ()) {
            return route_channel_name;
        }
    }
    if (state->channel_runtime->route_channels.size () == 1) {
        return state->channel_runtime->route_channels.begin ()->first;
    }
    if (state->node->snapshot.accepted_route_channels.size () == 1) {
        const auto &route_channel_name =
          state->node->snapshot.accepted_route_channels.front ().channel_name;
        if (state->channel_runtime->route_channels.find (route_channel_name)
            != state->channel_runtime->route_channels.end ()) {
            return route_channel_name;
        }
    }
    return std::nullopt;
}

runtime::messaging::message_parts_t
encode_spot_route_parts (runtime::messaging::message_kind_t kind,
                         const std::string &route_channel_name,
                         const std::string &packet_name,
                         zlink::message_t payload,
                         std::chrono::milliseconds timeout,
                         std::map<std::string, std::string> metadata)
{
    runtime::messaging::client_call_codec_t codec;
    auto header = codec.create_envelope (kind, route_channel_name, packet_name, timeout);
    header.metadata = std::move (metadata);
    runtime::messaging::envelope_codec_t envelope;
    return envelope.encode_raw_body_parts (header, std::move (payload));
}

framework_exception_t native_request_error (zlink::request_result_t result, std::string message)
{
    switch (result) {
        case zlink::request_result_t::timed_out:
        case zlink::request_result_t::not_connected:
            // Remote SPOT peers come and go while actors migrate, so both
            // conditions surface as the awaited-timeout boundary the caller can
            // retry through the resolver refresh path.
            return detail::make_boundary_exception (detail::boundary_error_t::timed_out,
                                                    std::move (message));
        default:
            return framework_exception_t (framework_error_kind_t::request_failed,
                                          std::move (message));
    }
}

result_t<runtime::messaging::message_parts_t>
request_spot_mesh_parts (const std::shared_ptr<detail::spot_context_state_t> &state,
                         node_rid_t node_rid,
                         spot_rid_t spot_rid,
                         runtime::messaging::message_parts_t parts,
                         std::chrono::milliseconds timeout)
{
    if (!state) {
        return result_t<runtime::messaging::message_parts_t>::failure (
          framework_error_kind_t::request_protocol_error, "SPOT context is not configured");
    }
    auto native = state->native_spot.lock ();
    if (!native) {
        return result_t<runtime::messaging::message_parts_t>::failure (
          framework_error_kind_t::spot_route_not_found,
          "SPOT mesh route requires a running native Spot");
    }
    try {
        auto native_parts = parts.items ();
        if (native_parts.empty ()) {
            return result_t<runtime::messaging::message_parts_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "SPOT mesh request requires at least one message part");
        }
        auto iterator = native_parts.begin ();
        auto submit =
          native
            ->request_to_spot (zlink::routing_id_t::from (std::string (node_rid.value ())),
                               zlink::routing_id_t::from (std::string (spot_rid.value ())))
            .message (*iterator);
        ++iterator;
        for (; iterator != native_parts.end (); ++iterator) {
            submit = std::move (submit).message (*iterator);
        }
        auto reply = std::move (submit).timeout (timeout).async ().get ();
        return result_t<runtime::messaging::message_parts_t>::success (
          runtime::messaging::message_parts_t (std::move (reply)));
    }
    catch (const framework_exception_t &error) {
        return detail::result_access_t::failure<runtime::messaging::message_parts_t> (error);
    }
    catch (const zlink::request_error_t &error) {
        return detail::result_access_t::failure<runtime::messaging::message_parts_t> (
          native_request_error (error.result (), error.what ()));
    }
    catch (const zlink::submit_error_t &error) {
        return detail::boundary_failure<runtime::messaging::message_parts_t> (detail::boundary_error_t::timed_out, error.what ());
    }
    catch (const std::exception &error) {
        return result_t<runtime::messaging::message_parts_t>::failure (
          framework_error_kind_t::request_failed, error.what ());
    }
}

task_t<zlink::message_t>
request_spot_mesh_message (const std::shared_ptr<detail::spot_context_state_t> &state,
                           node_rid_t node_rid,
                           spot_rid_t spot_rid,
                           runtime::messaging::message_parts_t parts,
                           std::chrono::milliseconds timeout)
{
    auto reply = request_spot_mesh_parts (state, std::move (node_rid), std::move (spot_rid),
                                          std::move (parts), timeout);
    if (!reply) {
        co_return result_t<zlink::message_t>::failure (reply.error_kind (),
                                                       reply.error () ? reply.error ()->what ()
                                                                      : "SPOT mesh request failed");
    }
    runtime::messaging::envelope_codec_t envelope;
    auto reply_header = envelope.decode_header (reply.value ());
    if (!reply_header) {
        co_return result_t<zlink::message_t>::failure (reply_header.error_kind (),
                                                       reply_header.error ()
                                                         ? reply_header.error ()->what ()
                                                         : "SPOT route reply header decode failed");
    }
    if (reply_header.value ().kind == runtime::messaging::message_kind_t::error) {
        co_return result_t<zlink::message_t>::failure (
          framework_error_kind_t::request_failed,
          reply_header.value ().error_message.value_or ("SPOT route request failed"));
    }
    auto body = envelope.decode_body (reply.value ());
    if (!body) {
        co_return detail::propagate_failure<zlink::message_t> (body, "SPOT route reply body decode failed");
    }
    co_return result_t<zlink::message_t>::success (body.value ());
}

} // namespace

namespace detail
{

void spot_context_state_t::enter_callback ()
{
    std::lock_guard<std::mutex> lock (callback_mutex);
    if (callback_depth == 0) {
        callback_thread = std::this_thread::get_id ();
    }
    ++callback_depth;
}

void spot_context_state_t::leave_callback ()
{
    bool should_close = false;
    {
        std::lock_guard<std::mutex> lock (callback_mutex);
        if (callback_depth > 0) {
            --callback_depth;
        }
        if (callback_depth == 0) {
            callback_thread = std::thread::id ();
            should_close = close_requested;
        }
    }
    if (should_close) {
        (void) close_now ();
    }
}

bool spot_context_state_t::is_current_callback_thread () const
{
    std::lock_guard<std::mutex> lock (callback_mutex);
    return callback_depth > 0 && callback_thread == std::this_thread::get_id ();
}

bool spot_context_state_t::try_post_serial (std::string name, std::function<void ()> work)
{
    if (!serial_queue) {
        work ();
        return true;
    }
    return serial_queue->try_post (std::move (name), std::move (work));
}

bool spot_context_state_t::try_post_serial_async (
  std::string name, runtime::serial_execution_queue_t::async_work_t work)
{
    if (!serial_queue) {
        work ([] (std::function<void ()> completion) {
            if (completion) {
                completion ();
            }
        });
        return true;
    }
    /* zlink.spot.queue.* (runtime-metrics §4.2): the enqueue timestamp is
     * taken only when a metric subscriber exists (§7.2 gate). */
    if (node && node->monitoring) {
        runtime::runtime_metrics_t metrics (node->monitoring);
        if (metrics.enabled ()) {
            const auto enqueued_at = std::chrono::steady_clock::now ();
            const std::string kind =
              node->snapshot.entry_spot_name && *node->snapshot.entry_spot_name == spot_name
                ? "entry"
                : "user";
            metrics.updown ("zlink.spot.queue.depth", "{item}", 1, {{"kind", kind}});
            auto inner = std::move (work);
            work = [metrics, enqueued_at, kind,
                    inner = std::move (inner)] (auto complete) mutable {
                metrics.updown ("zlink.spot.queue.depth", "{item}", -1, {{"kind", kind}});
                metrics.histogram (
                  "zlink.spot.queue.wait.duration", "s",
                  std::chrono::duration<double> (std::chrono::steady_clock::now () - enqueued_at)
                    .count (),
                  {{"kind", kind}});
                inner (std::move (complete));
            };
            const auto posted = serial_queue->try_post_async (std::move (name), std::move (work));
            if (!posted) {
                metrics.updown ("zlink.spot.queue.depth", "{item}", -1, {{"kind", kind}});
            }
            return posted;
        }
    }
    return serial_queue->try_post_async (std::move (name), std::move (work));
}

bool spot_context_state_t::run_serial_sync (std::string name, std::function<void ()> work)
{
    if (!work) {
        return true;
    }
    if (is_current_callback_thread ()) {
        work ();
        return true;
    }

    std::exception_ptr error;
    const bool posted = try_post_serial (std::move (name), [&] {
        enter_callback ();
        try {
            work ();
        }
        catch (...) {
            error = std::current_exception ();
        }
        leave_callback ();
    });
    if (!posted) {
        return false;
    }
    drain_serial ();
    if (error) {
        std::rethrow_exception (error);
    }
    return true;
}

void spot_context_state_t::drain_serial ()
{
    if (serial_queue) {
        serial_queue->drain ();
    }
}

} // namespace detail

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

spot_context_t::erased_request_call_t::erased_request_call_t (framework_exception_t error) :
    _error (std::move (error))
{
}

spot_context_t::erased_request_call_t::erased_request_call_t (
  std::string packet_name,
  serializer_registry_t *serializers,
  std::function<task_t<zlink::message_t> (const std::string &,
                                          std::chrono::milliseconds,
                                          const request_call_t<zlink::message_t>::metadata_map_t &)>
    submit) :
    _packet_name (std::move (packet_name)), _serializers (serializers), _submit (std::move (submit))
{
}

spot_context_t::spot_context_t () : _state (std::make_shared<detail::spot_context_state_t> ())
{
}

spot_context_t::spot_context_t (std::shared_ptr<detail::spot_context_state_t> state) :
    _state (std::move (state))
{
    if (_state) {
        _worker_scheduler = _state->worker_scheduler;
    }
}

spot_context_t::~spot_context_t () = default;
spot_context_t::spot_context_t (spot_context_t &&) noexcept = default;
spot_context_t &spot_context_t::operator= (spot_context_t &&) noexcept = default;

entry_spot_context_t::entry_spot_context_t () = default;

entry_spot_context_t::entry_spot_context_t (const spot_context_t &context) :
    spot_context_t (context._state)
{
}

entry_spot_context_t::entry_spot_context_t (std::shared_ptr<detail::spot_context_state_t> state) :
    spot_context_t (std::move (state))
{
}

entry_spot_context_t::~entry_spot_context_t () = default;
entry_spot_context_t::entry_spot_context_t (entry_spot_context_t &&) noexcept = default;
entry_spot_context_t &entry_spot_context_t::operator= (entry_spot_context_t &&) noexcept = default;

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

spot_node_manager_t spot_context_t::manager () const
{
    return spot_node_manager_t (_state->node);
}

channel_client_t spot_context_t::outbound () const
{
    if (!_state->channel_runtime) {
        throw framework_exception_t (framework_error_kind_t::request_failed,
                                     "SPOT channel outbound runtime is not configured");
    }
    return channel_client_t (message_bus_t (_state->channel_runtime));
}

task_t<bool> spot_context_t::close ()
{
    return close_erased ();
}

task_t<bool> spot_context_t::close_erased ()
{
    if (!_state || !_state->node) {
        co_return result_t<bool>::success (false);
    }
    {
        std::lock_guard<std::recursive_mutex> node_lock (_state->node->mutex);
        if (_state->closed || _state->actor_count != 0) {
            co_return result_t<bool>::success (false);
        }
        {
            std::lock_guard<std::mutex> callback_lock (_state->callback_mutex);
            if (_state->callback_depth != 0) {
                _state->close_requested = true;
                co_return result_t<bool>::success (true);
            }
        }
        co_return result_t<bool>::success (_state->close_now ());
    }
}

void detail::spot_context_state_t::cancel_timers () noexcept
{
    detail::timer_runtime_t::cancel_all (*this);
}

task_t<actor_ref_t> spot_context_t::leave_actor_erased (
  const actor_ref_t &actor_ref,
  std::type_index actor_type,
  void *actor,
  std::function<void (void *, const actor_ref_t &)> update_actor_ref)
{
    if (!_state || !_state->node || actor_ref.empty ()) {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
          framework_error_kind_t::actor_route_not_found, "actor ref is empty"));
    }
    std::unique_lock<std::recursive_mutex> node_lock (_state->node->mutex);
    const auto key =
      std::string (actor_ref.actor_type ()) + ":" + std::string (actor_ref.actor_id ());
    const auto found_location = _state->node->actor_spot_rids.find (key);
    if (found_location == _state->node->actor_spot_rids.end ()) {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::success (actor_ref));
    }
    if (found_location->second.value () != _state->spot_rid.value ()) {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
          framework_error_kind_t::actor_route_not_found, "actor is not joined to this SPOT"));
    }

    const auto found_generation = _state->node->actor_generations.find (key);
    if (found_generation != _state->node->actor_generations.end ()
        && found_generation->second != actor_ref.generation ()) {
        return task_t<actor_ref_t> (detail::boundary_failure<actor_ref_t> (detail::boundary_error_t::stale_generation, "actor generation is stale"));
    }

    if (_state->node_rid.empty () || actor_ref.node_rid ().value () != _state->node_rid.value ()) {
        try {
            auto entry_join = _state->node->actor_entry_spot_join;
            auto &source_state = *_state;
            decrement_actor_count_unlocked (source_state);
            erase_actor_route_unlocked (*_state->node, key);
            const auto source_admission = source_state.actor_admissions.find (actor_type);
            if (source_admission != source_state.actor_admissions.end ()
                && source_admission->second.on_leave_actor && source_state.spot_instance) {
                node_lock.unlock ();
                if (!source_state.run_serial_sync ("spot-lifecycle-leave", [&] {
                        source_admission->second.on_leave_actor (source_state.spot_instance.get (),
                                                               actor);
                    })) {
                    return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
                      framework_error_kind_t::request_rejected, "spot serial queue is full"));
                }
                node_lock.lock ();
            }
            if (!entry_join) {
                return task_t<actor_ref_t> (result_t<actor_ref_t>::success (actor_ref));
            }
            std::optional<zlink::message_t> actor_snapshot;
            const auto actor_factory =
              _state->node->actor_factories.find (std::string (actor_ref.actor_type ()));
            if (actor_factory != _state->node->actor_factories.end ()
                && source_state.channel_runtime && source_state.channel_runtime->serializers) {
                actor_snapshot = actor_factory->second.serialize_instance (
                  actor, *source_state.channel_runtime->serializers);
            }
            node_lock.unlock ();
            auto joined =
              entry_join (actor_ref, actor_ref.node_rid (), zlink::message_t{}, actor_snapshot);
            node_lock.lock ();
            if (!joined) {
                const auto *error = joined.error ();
                return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
                  joined.error_kind (),
                  error != nullptr ? error->what () : "remote entry spot join failed",
                  error != nullptr && error->is_retriable ()));
            }
            if (update_actor_ref) {
                update_actor_ref (actor, joined.value ().actor);
            }
            return task_t<actor_ref_t> (result_t<actor_ref_t>::success (joined.value ().actor));
        }
        catch (const framework_exception_t &error) {
            return task_t<actor_ref_t> (
              detail::result_access_t::failure<actor_ref_t> (error));
        }
        catch (const std::exception &error) {
            return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
              framework_error_kind_t::request_failed, error.what ()));
        }
        catch (...) {
            return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
              framework_error_kind_t::request_failed, "remote actor leave callback failed"));
        }
    }

    if (!_state->node->snapshot.entry_spot_name) {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
          framework_error_kind_t::spot_route_not_found, "entry spot is not registered"));
    }

    const auto entry_rid =
      _state->node->spot_rids_by_name.find (*_state->node->snapshot.entry_spot_name);
    if (entry_rid == _state->node->spot_rids_by_name.end ()) {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
          framework_error_kind_t::spot_route_not_found, "entry spot is not created"));
    }
    const auto entry_context =
      _state->node->spot_contexts_by_rid.find (std::string (entry_rid->second.value ()));
    if (entry_context == _state->node->spot_contexts_by_rid.end ()
        || !entry_context->second._state->spot_instance) {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
          framework_error_kind_t::spot_route_not_found, "entry spot context is not registered"));
    }

    try {
        auto &source_state = *_state;
        decrement_actor_count_unlocked (source_state);
        erase_actor_route_unlocked (*_state->node, key);
        const auto source_admission = source_state.actor_admissions.find (actor_type);
        if (source_admission != source_state.actor_admissions.end ()
            && source_admission->second.on_leave_actor && source_state.spot_instance) {
            node_lock.unlock ();
            if (!source_state.run_serial_sync ("spot-lifecycle-leave", [&] {
                    source_admission->second.on_leave_actor (source_state.spot_instance.get (),
                                                           actor);
                })) {
                return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
                  framework_error_kind_t::request_rejected, "spot serial queue is full"));
            }
            node_lock.lock ();
        } else {
            const auto source_left = source_state.on_leave_actor_callbacks.find (actor_type);
            if (source_left != source_state.on_leave_actor_callbacks.end ()
                && source_state.spot_instance) {
                node_lock.unlock ();
                if (!source_state.run_serial_sync ("spot-lifecycle-leave", [&] {
                        source_left->second (source_state.spot_instance.get (), actor);
                    })) {
                    return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
                      framework_error_kind_t::request_rejected, "spot serial queue is full"));
                }
                node_lock.lock ();
            }
        }

        auto &entry_state = *entry_context->second._state;
        const auto committed =
          actor_ref_t (node_rid_t::from_string (std::string (_state->node_rid.value ())),
                       std::string (actor_ref.actor_type ()), std::string (actor_ref.actor_id ()),
                       actor_ref.generation () + 1);
        (void) update_actor_location_after_move (*_state->node, committed, entry_state, true);
        detail::record_actor_context_route_unlocked (*_state->node, key,
                                                     std::string (_state->node_rid.value ()),
                                                     entry_state, committed.generation ());
        if (update_actor_ref) {
            update_actor_ref (actor, committed);
        }
        if (_state->node->update_actor_registry_ref) {
            auto updated = _state->node->update_actor_registry_ref (committed);
            if (!updated) {
                const auto *error = updated.error ();
                return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
                  updated.error_kind (),
                  error != nullptr ? error->what () : "actor registry ref update failed"));
            }
        }

        const auto entry_admission = entry_state.actor_admissions.find (actor_type);
        if (entry_admission != entry_state.actor_admissions.end ()
            && entry_admission->second.on_actor_joined && entry_state.spot_instance) {
            node_lock.unlock ();
            if (!entry_state.run_serial_sync ("spot-lifecycle-join", [&] {
                    entry_admission->second.on_actor_joined (entry_state.spot_instance.get (),
                                                             actor);
                })) {
                return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
                  framework_error_kind_t::request_rejected, "spot serial queue is full"));
            }
            node_lock.lock ();
        } else {
            const auto entry_joined = entry_state.on_actor_joined_callbacks.find (actor_type);
            if (entry_joined != entry_state.on_actor_joined_callbacks.end ()
                && entry_state.spot_instance) {
                node_lock.unlock ();
                if (!entry_state.run_serial_sync ("spot-lifecycle-join", [&] {
                        entry_joined->second (entry_state.spot_instance.get (), actor);
                    })) {
                    return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
                      framework_error_kind_t::request_rejected, "spot serial queue is full"));
                }
                node_lock.lock ();
            }
        }
        return task_t<actor_ref_t> (result_t<actor_ref_t>::success (committed));
    }
    catch (const framework_exception_t &error) {
        return task_t<actor_ref_t> (
          detail::result_access_t::failure<actor_ref_t> (error));
    }
    catch (const std::exception &error) {
        return task_t<actor_ref_t> (
          result_t<actor_ref_t>::failure (framework_error_kind_t::request_failed, error.what ()));
    }
    catch (...) {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
          framework_error_kind_t::request_failed, "actor leave callback failed"));
    }
}

task_t<void> entry_spot_context_t::destroy_actor_instance_erased (const void *instance)
{
    if (!_state || !_state->node || instance == nullptr) {
        return task_t<void> (result_t<void>::failure (framework_error_kind_t::actor_route_not_found,
                                                      "actor instance is not registered"));
    }
    /* Resolution and destruction stay under one node lock (the mutex is
     * recursive), so a concurrent transfer cannot move the actor between
     * the identity lookup and the location decision. */
    std::lock_guard<std::recursive_mutex> node_lock (_state->node->mutex);
    const auto found = _state->node->actor_instance_index.find (instance);
    if (found == _state->node->actor_instance_index.end ()) {
        /* Instance not registered on this node: already destroyed or
         * superseded — duplicate destroy is a successful no-op. */
        return task_t<void> (result_t<void>::success ());
    }
    const auto key = found->second.first + ":" + found->second.second;
    const auto found_generation = _state->node->actor_generations.find (key);
    return destroy_actor_erased (
      actor_ref_t (_state->node_rid, found->second.first, found->second.second,
                   found_generation != _state->node->actor_generations.end ()
                     ? found_generation->second
                     : 1));
}

task_t<void> entry_spot_context_t::destroy_actor_erased (const actor_ref_t &actor)
{
    if (!_state || !_state->node || actor.empty ()) {
        return task_t<void> (result_t<void>::failure (framework_error_kind_t::actor_route_not_found,
                                                      "actor ref is empty"));
    }
    std::lock_guard<std::recursive_mutex> node_lock (_state->node->mutex);
    if (actor.node_rid ().empty () || actor.node_rid ().value () != _state->node_rid.value ()) {
        return task_t<void> (result_t<void>::failure (framework_error_kind_t::actor_route_not_found,
                                                      "actor is not owned by this Entry SPOT"));
    }

    const auto key = std::string (actor.actor_type ()) + ":" + std::string (actor.actor_id ());
    const auto found_location = _state->node->actor_spot_rids.find (key);
    if (found_location != _state->node->actor_spot_rids.end ()
        && found_location->second.value () != _state->spot_rid.value ()) {
        return task_t<void> (
          result_t<void>::failure (framework_error_kind_t::actor_route_not_found,
                                   "actor must leave its current SPOT before destroy"));
    }

    const auto found_generation = _state->node->actor_generations.find (key);
    if (found_generation != _state->node->actor_generations.end ()
        && found_generation->second != actor.generation ()) {
        return task_t<void> (result_t<void>::success ());
    }
    if (_state->node->destroying_actors.contains (key)) {
        return task_t<void> (result_t<void>::success ());
    }

    if (found_location != _state->node->actor_spot_rids.end ()) {
        _state->node->destroying_actors.insert (key);
        release_actor_location (*_state->node, actor);
        erase_actor_route_unlocked (*_state->node, key);
        _state->node->actor_created_keys.erase (key);
        _state->node->destroyed_actor_keys.insert (key);
        _state->node->actor_instances.erase (key);
        detail::erase_actor_instance_index_unlocked (*_state->node, actor.actor_type (),
                                                     actor.actor_id ());
        _state->node->actor_mailboxes.erase (key);
        {
            const std::lock_guard<std::mutex> dedup_lock (
              _state->node->dispatched_request_replies_mutex);
            _state->node->dispatched_request_replies.erase (key);
        }
        decrement_actor_count_unlocked (*_state);
        if (_state->node->destroy_actor_registry) {
            auto cleanup = _state->node->destroy_actor_registry (actor);
            _state->node->destroying_actors.erase (key);
            if (!cleanup) {
                const auto *error = cleanup.error ();
                return task_t<void> (result_t<void>::failure (
                  cleanup.error_kind (),
                  error != nullptr ? error->what () : "actor registry cleanup failed"));
            }
        } else {
            _state->node->destroying_actors.erase (key);
        }
    }

    return task_t<void> (result_t<void>::success ());
}

send_call_t spot_context_t::publish_erased (std::string topic,
                                            std::string packet_name,
                                            zlink::message_t payload)
{
    auto state = _state;
    return send_call_t (
      std::move (packet_name), [state, topic = std::move (topic), payload = std::move (payload)] (
                                 const std::string &submitted_packet_name,
                                 std::chrono::milliseconds, const send_call_t::metadata_map_t &) {
          if (!state) {
              return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                              "spot context is not configured");
          }
          state->ordering_log.push_back ("publish:" + topic + ":" + submitted_packet_name + ":"
                                         + payload.to_string ());
          auto native = state->native_spot.lock ();
          if (native) {
              try {
                  /* Fan-out wire envelope (flow-correlation §4.1, .NET
                   * ZLinkSpotPublishEnvelope 동형): the header carries the
                   * ambient flow pair so every subscriber line shares one
                   * flow id across the tree. */
                  const bool capture_enabled =
                    state->node
                    && detail::message_flow_tracer_t (state->node->dispatch).capture_enabled ();
                  auto flow_scope = runtime::flow_context_t::enter_current_or_create (
                    flow_origin_t::application, capture_enabled);
                  runtime::messaging::envelope_header_t header;
                  header.kind = runtime::messaging::message_kind_t::publish;
                  header.channel_name = state->node ? state->node->snapshot.name : std::string{};
                  header.message_name = submitted_packet_name;
                  header.topic = topic;
                  header.source = header.channel_name;
                  /* Self-delimited single frame: ['Z''L''F''E'][u32 BE
                   * header_len][header JSON][body]. The node-attached fanout
                   * path does not keep multipart boundaries end to end, so
                   * the envelope frames itself; the decode side also accepts
                   * a true two-part frame from peers whose wire preserves
                   * parts. The magic makes the format discriminable from a
                   * legacy raw payload, so a validation failure after a
                   * magic match is definitively a corrupted framework frame. */
                  const auto header_message =
                    runtime::messaging::envelope_codec_t{}.encode_header (header);
                  const auto header_bytes = header_message.to_bytes ();
                  const auto body_bytes = payload.to_bytes ();
                  std::vector<std::uint8_t> frame;
                  frame.reserve (8 + header_bytes.size () + body_bytes.size ());
                  frame.push_back (static_cast<std::uint8_t> ('Z'));
                  frame.push_back (static_cast<std::uint8_t> ('L'));
                  frame.push_back (static_cast<std::uint8_t> ('F'));
                  frame.push_back (static_cast<std::uint8_t> ('E'));
                  const auto header_size = static_cast<std::uint32_t> (header_bytes.size ());
                  frame.push_back (static_cast<std::uint8_t> (header_size >> 24));
                  frame.push_back (static_cast<std::uint8_t> (header_size >> 16));
                  frame.push_back (static_cast<std::uint8_t> (header_size >> 8));
                  frame.push_back (static_cast<std::uint8_t> (header_size));
                  frame.insert (frame.end (), header_bytes.begin (), header_bytes.end ());
                  frame.insert (frame.end (), body_bytes.begin (), body_bytes.end ());
                  auto frame_part = zlink::message_t::from (frame);
                  if (!std::move (native->publish (topic)).message (frame_part).submit ()) {
                      return result_t<void>::failure (framework_error_kind_t::request_failed,
                                                      "spot publish failed");
                  }
              }
              catch (const std::exception &error) {
                  return result_t<void>::failure (framework_error_kind_t::request_failed,
                                                  error.what ());
              }
              if (state->node) {
                  detail::message_flow_tracer_t (state->node->dispatch)
                    .trace (message_flow_outcome_t::sent, [&] {
                        return message_flow_event_t{message_flow_outcome_t::sent,
                                                    dispatch_error_surface_t::spot_subscription,
                                                    dispatch_message_kind_t::publish,
                                                    submitted_packet_name,
                                                    std::nullopt,
                                                    topic,
                                                    std::nullopt,
                                                    std::nullopt,
                                                    std::string (state->spot_rid.value ()),
                                                    std::nullopt,
                                                    std::nullopt};
                    });
                  if (state->node->monitoring) {
                      runtime::runtime_metrics_t metrics (state->node->monitoring);
                      if (metrics.enabled ()) {
                          /* Declared topics are a closed set (runtime-metrics
                           * §4.4b/§5); dynamic topics would drop the label. */
                          metrics.counter ("zlink.fanout.published", "{message}", 1,
                                           {{"topic", topic}});
                      }
                  }
              }
          }
          return result_t<void>::success ();
      });
}

serializer_registry_t *spot_context_t::serializer_registry () const noexcept
{
    if (!_state || !_state->channel_runtime) {
        return nullptr;
    }
    return _state->channel_runtime->serializers;
}

send_call_t spot_context_t::send_to_erased (node_rid_t node_rid,
                                            spot_rid_t spot_rid,
                                            std::string packet_name,
                                            zlink::message_t payload)
{
    if (node_rid.empty () || spot_rid.empty ()) {
        return send_call_t (result_t<void>::failure (framework_error_kind_t::spot_route_not_found,
                                                     "target spot route is empty"));
    }
    auto state = _state;
    return send_call_t (
      std::move (packet_name),
      [state, node_rid = std::move (node_rid), spot_rid = std::move (spot_rid),
       payload = std::move (payload)] (
        const std::string &submitted_packet_name, std::chrono::milliseconds,
        const send_call_t::metadata_map_t &metadata) mutable -> result_t<void> {
          if (!state) {
              return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                              "SPOT context is not configured");
          }
          if (auto route_channel_name = optional_spot_route_channel_name (state)) {
              detail::channel_runtime_manager_t manager (state->channel_runtime);
              auto &runtime = manager.get_route_channel (*route_channel_name);
              auto parts = encode_spot_route_parts (
                runtime::messaging::message_kind_t::command, *route_channel_name,
                submitted_packet_name, payload, std::chrono::milliseconds::zero (), metadata);
              auto submitted = runtime.submit_spot_send_parts (
                zlink::routing_id_t::from (std::string (node_rid.value ())),
                zlink::routing_id_t::from (std::string (spot_rid.value ())), std::move (parts));
              if (submitted) {
                  state->ordering_log.push_back ("send_to:" + std::string (spot_rid.value ()));
              }
              return submitted;
          }
          auto native = state->native_spot.lock ();
          if (!native) {
              return result_t<void>::failure (framework_error_kind_t::spot_route_not_found,
                                              "SPOT mesh route requires a running native Spot");
          }
          try {
              const auto channel_name = spot_mesh_channel_name (state);
              auto parts = encode_spot_route_parts (runtime::messaging::message_kind_t::command,
                                                    channel_name, submitted_packet_name, payload,
                                                    std::chrono::milliseconds::zero (), metadata);
              auto native_parts = parts.items ();
              if (native_parts.empty ()) {
                  return result_t<void>::failure (
                    framework_error_kind_t::request_protocol_error,
                    "SPOT mesh send requires at least one message part");
              }
              auto iterator = native_parts.begin ();
              auto submit =
                native
                  ->send_to_spot (zlink::routing_id_t::from (std::string (node_rid.value ())),
                                  zlink::routing_id_t::from (std::string (spot_rid.value ())))
                  .message (*iterator);
              ++iterator;
              for (; iterator != native_parts.end (); ++iterator) {
                  submit = std::move (submit).message (*iterator);
              }
              if (!std::move (submit).submit ()) {
                  return result_t<void>::failure (framework_error_kind_t::request_failed,
                                                  "SPOT mesh send was not submitted");
              }
              if (state) {
                  state->ordering_log.push_back ("send_to:" + std::string (spot_rid.value ()));
              }
              return result_t<void>::success ();
          }
          catch (const framework_exception_t &error) {
              return detail::result_access_t::failure<void> (error);
          }
          catch (const std::exception &error) {
              return result_t<void>::failure (framework_error_kind_t::request_failed,
                                              error.what ());
          }
      });
}

spot_context_t::erased_request_call_t spot_context_t::request_to_erased (node_rid_t node_rid,
                                                                         spot_rid_t spot_rid,
                                                                         std::string packet_name,
                                                                         zlink::message_t payload)
{
    if (node_rid.empty () || spot_rid.empty ()) {
        return erased_request_call_t (framework_exception_t (
          framework_error_kind_t::spot_route_not_found, "target spot route is empty"));
    }
    auto state = _state;
    return erased_request_call_t (
      std::move (packet_name), serializer_registry (),
      [state, node_rid = std::move (node_rid), spot_rid = std::move (spot_rid),
       payload = std::move (payload)] (
        const std::string &submitted_packet_name, std::chrono::milliseconds timeout,
        const request_call_t<zlink::message_t>::metadata_map_t &metadata) mutable
      -> task_t<zlink::message_t> {
          if (!state) {
              return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
                framework_error_kind_t::request_protocol_error, "SPOT context is not configured"));
          }
          try {
              if (auto route_channel_name = optional_spot_route_channel_name (state)) {
                  detail::channel_runtime_manager_t manager (state->channel_runtime);
                  auto &runtime = manager.get_route_channel (*route_channel_name);
                  const auto effective_timeout = timeout > std::chrono::milliseconds::zero ()
                                                   ? timeout
                                                   : runtime.default_request_timeout ();
                  auto parts = encode_spot_route_parts (runtime::messaging::message_kind_t::request,
                                                        *route_channel_name, submitted_packet_name,
                                                        payload, effective_timeout, metadata);
                  state->ordering_log.push_back ("request_to:" + std::string (spot_rid.value ()));
                  auto reply = runtime.request_reply_spot_parts (
                    zlink::routing_id_t::from (std::string (node_rid.value ())),
                    zlink::routing_id_t::from (std::string (spot_rid.value ())), std::move (parts),
                    effective_timeout);
                  if (!reply) {
                      return task_t<zlink::message_t> (detail::propagate_failure<zlink::message_t> (reply, "SPOT route request failed"));
                  }
                  runtime::messaging::envelope_codec_t envelope;
                  auto body = envelope.decode_body (reply.value ());
                  if (!body) {
                      return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
                        body.error_kind (), body.error () ? body.error ()->what ()
                                                          : "SPOT route reply body decode failed"));
                  }
                  return task_t<zlink::message_t> (
                    result_t<zlink::message_t>::success (body.value ()));
              }
              auto native = state->native_spot.lock ();
              if (!native) {
                  return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
                    framework_error_kind_t::spot_route_not_found,
                    "SPOT mesh route requires a running native Spot"));
              }
              const auto effective_timeout =
                timeout > std::chrono::milliseconds::zero () ? timeout : native->request_timeout ();
              const auto channel_name = spot_mesh_channel_name (state);
              auto parts = encode_spot_route_parts (runtime::messaging::message_kind_t::request,
                                                    channel_name, submitted_packet_name, payload,
                                                    effective_timeout, metadata);
              state->ordering_log.push_back ("request_to:" + std::string (spot_rid.value ()));
              return request_spot_mesh_message (state, std::move (node_rid), std::move (spot_rid),
                                                std::move (parts), effective_timeout);
          }
          catch (const framework_exception_t &error) {
              return task_t<zlink::message_t> (detail::result_access_t::failure<zlink::message_t> (error));
          }
      });
}

spot_context_t &spot_context_t::register_packet_erased (std::string packet_name,
                                                        std::type_index payload_type)
{
    const auto duplicate = std::any_of (_state->packets.begin (), _state->packets.end (),
                                        [&] (const spot_packet_descriptor_t &descriptor) {
                                            return descriptor.packet_name == packet_name;
                                        });
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

spot_handler_registry_t::spot_handler_registry_t () :
    _state (std::make_shared<detail::spot_context_state_t> ())
{
}

spot_handler_registry_t::spot_handler_registry_t (
  std::shared_ptr<detail::spot_context_state_t> state) :
    _state (std::move (state))
{
}

spot_handler_registry_t::~spot_handler_registry_t () = default;
spot_handler_registry_t::spot_handler_registry_t (spot_handler_registry_t &&) noexcept = default;
spot_handler_registry_t &
spot_handler_registry_t::operator= (spot_handler_registry_t &&) noexcept = default;

spot_handler_registry_t &spot_handler_registry_t::add_handler_erased (spot_handler_kind_t kind,
                                                                      std::string packet_name,
                                                                      std::string topic,
                                                                      std::type_index handler_type,
                                                                      std::type_index payload_type,
                                                                      std::type_index actor_type,
                                                                      std::type_index reply_type,
                                                                      invoker_t invoker)
{
    const auto duplicate =
      std::any_of (_state->handlers.begin (), _state->handlers.end (),
                   [&] (const spot_handler_descriptor_t &descriptor) {
                       return descriptor.kind == kind && descriptor.packet_name == packet_name
                              && descriptor.topic == topic && descriptor.actor_type == actor_type;
                   });
    if (duplicate) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "duplicate spot handler registration");
    }

    _state->handlers.push_back (spot_handler_descriptor_t{kind, std::move (packet_name),
                                                          std::move (topic), handler_type,
                                                          payload_type, actor_type, reply_type});
    _state->handler_invokers.push_back (std::move (invoker));
    const auto &descriptor = _state->handlers.back ();
    if (descriptor.kind == spot_handler_kind_t::subscription && !descriptor.topic.empty ()) {
        if (auto native = _state->native_spot.lock ()) {
            native->set_subscription (descriptor.topic);
        }
    }
    return *this;
}

void spot_handler_registry_t::register_actor_admission_erased (
  std::type_index actor_type, detail::spot_actor_admission_callbacks_t callbacks)
{
    _state->actor_admissions[actor_type] = std::move (callbacks);
}

std::vector<spot_handler_descriptor_t> spot_handler_registry_t::descriptors () const
{
    return _state->handlers;
}

spot_handler_kind_t
spot_handler_registry_t::resolve_actor_packet_kind (std::string_view packet_name,
                                                    std::type_index actor_type) const
{
    for (const auto &descriptor : _state->handlers) {
        if (descriptor.kind == spot_handler_kind_t::actor_request
            && descriptor.packet_name == packet_name && descriptor.actor_type == actor_type) {
            return spot_handler_kind_t::actor_request;
        }
    }
    for (const auto &descriptor : _state->handlers) {
        if (descriptor.kind == spot_handler_kind_t::actor_send
            && descriptor.packet_name == packet_name && descriptor.actor_type == actor_type) {
            return spot_handler_kind_t::actor_send;
        }
    }
    return spot_handler_kind_t::actor_request;
}

task_t<zlink::message_t>
spot_handler_registry_t::invoke_erased (spot_handler_kind_t kind,
                                        std::string_view packet_name,
                                        std::string_view topic,
                                        std::type_index actor_type,
                                        void *spot,
                                        void *actor,
                                        service_provider_t &services,
                                        serializer_registry_t &serializers,
                                        const zlink::message_t &message,
                                        spot_actor_message_metadata_t metadata,
                                        bool serial_dispatch) const
{
    for (std::size_t index = 0; index < _state->handlers.size (); ++index) {
        const auto &descriptor = _state->handlers[index];
        if (descriptor.kind == kind && descriptor.packet_name == packet_name
            && descriptor.topic == topic && descriptor.actor_type == actor_type) {
            auto owned_message = message;
            const auto handler_index = index;
            detail::task_completion_source_t<zlink::message_t> completion;
            auto task = completion.task ();
            auto state = _state;
            if (!serial_dispatch) {
                auto direct_message = std::move (owned_message);
                state->enter_callback ();
                try {
                    auto handler_task = state->handler_invokers[handler_index](
                      spot, actor, services, serializers, direct_message, std::move (metadata));
                    detail::observe_task_completion (
                      handler_task,
                      [state, completion] (const result_t<zlink::message_t> &result) mutable {
                          state->leave_callback ();
                          if (result) {
                              completion.complete (
                                result_t<zlink::message_t>::success (result.value ()));
                              return;
                          }
                          completion.complete (result_t<zlink::message_t>::failure (
                            result.error_kind (),
                            result.error () != nullptr ? result.error ()->what ()
                                                       : "spot handler failed",
                            result.error () != nullptr && result.error ()->is_retriable ()));
                      });
                }
                catch (const framework_exception_t &error) {
                    state->leave_callback ();
                    completion.complete (detail::result_access_t::failure<zlink::message_t> (error));
                }
                catch (const std::exception &error) {
                    state->leave_callback ();
                    completion.complete (result_t<zlink::message_t>::failure (
                      framework_error_kind_t::request_failed, error.what ()));
                }
                catch (...) {
                    state->leave_callback ();
                    completion.complete (result_t<zlink::message_t>::failure (
                      framework_error_kind_t::request_failed, "spot handler threw an exception"));
                }
                return task;
            }
            auto dispatch_flow = runtime::flow_context_t::current ();
            const auto posted = state->try_post_serial_async (
              "spot-handler",
              [state, handler_index, spot, actor, &services, &serializers,
               owned_message = std::move (owned_message), metadata = std::move (metadata),
               completion, dispatch_flow = std::move (dispatch_flow)] (auto complete) mutable {
                  runtime::flow_context_t::scope_t callback_flow (std::move (dispatch_flow));
                  state->enter_callback ();
                  auto turn = detail::capture_current_serial_turn ();
                  try {
                      auto handler_task = state->handler_invokers[handler_index](
                        spot, actor, services, serializers, owned_message, metadata);
                      detail::observe_task_completion (
                        handler_task, [state, completion, turn, complete] (
                                        const result_t<zlink::message_t> &result) mutable {
                            result_t<zlink::message_t> final_result =
                              result
                                ? result_t<zlink::message_t>::success (result.value ())
                                : result_t<zlink::message_t>::failure (
                                    result.error_kind (),
                                    result.error () != nullptr ? result.error ()->what ()
                                                               : "spot handler failed",
                                    result.error () != nullptr && result.error ()->is_retriable ());
                            auto finish = [state, completion,
                                           final_result = std::move (final_result)] () mutable {
                                state->leave_callback ();
                                completion.complete (std::move (final_result));
                            };
                            if (turn && turn->released ()) {
                                finish ();
                                return;
                            }
                            complete (std::move (finish));
                        });
                  }
                  catch (const framework_exception_t &error) {
                      complete ([state, completion, error] () mutable {
                          state->leave_callback ();
                          completion.complete (detail::result_access_t::failure<zlink::message_t> (error));
                      });
                  }
                  catch (const std::exception &error) {
                      const auto message = std::string (error.what ());
                      complete ([state, completion, message = std::move (message)] () mutable {
                          state->leave_callback ();
                          completion.complete (result_t<zlink::message_t>::failure (
                            framework_error_kind_t::request_failed, std::move (message)));
                      });
                  }
                  catch (...) {
                      complete ([state, completion] () mutable {
                          state->leave_callback ();
                          completion.complete (result_t<zlink::message_t>::failure (
                            framework_error_kind_t::request_failed,
                            "spot handler threw an exception"));
                      });
                  }
              });
            if (!posted) {
                return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
                  framework_error_kind_t::request_rejected, "spot serial queue is full"));
            }
            return task;
        }
    }
    std::ostringstream error_message;
    error_message << "spot handler is not registered: packet='" << packet_name << "', topic='"
                  << topic << "', actor_type='" << actor_type.name () << "', registered=[";
    for (std::size_t index = 0; index < _state->handlers.size (); ++index) {
        if (index > 0) {
            error_message << "; ";
        }
        const auto &descriptor = _state->handlers[index];
        error_message << "packet='" << descriptor.packet_name << "', topic='" << descriptor.topic
                      << "', actor_type='" << descriptor.actor_type.name () << "'";
    }
    error_message << "]";
    return task_t<zlink::message_t> (result_t<zlink::message_t>::failure (
      framework_error_kind_t::handler_not_found, error_message.str ()));
}

spot_node_builder_t::spot_node_builder_t () :
    _state (std::make_shared<detail::spot_node_builder_state_t> (""))
{
}

spot_node_builder_t::spot_node_builder_t (
  std::shared_ptr<detail::spot_node_builder_state_t> state) :
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

spot_node_builder_t &spot_node_builder_t::set_routing_id (zlink::routing_id_t routing_id)
{
    _state->snapshot.routing_id = std::move (routing_id);
    return *this;
}

spot_node_builder_t &
spot_node_builder_t::set_actor_transfer_forward_window (std::chrono::milliseconds window)
{
    _state->actor_transfer_forward_window = window;
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

spot_node_builder_t &spot_node_builder_t::connect_router (std::string endpoint)
{
    if (endpoint.empty () || is_blank (endpoint)) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "SPOT router manual endpoint is required");
    }
    _state->snapshot.router_manual_connections.push_back (std::move (endpoint));
    return *this;
}

spot_node_builder_t &spot_node_builder_t::connect_router (zlink::routing_id_t peer_rid,
                                                          std::string endpoint)
{
    if (peer_rid.size () == 0u) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "SPOT router manual peer routing id is required");
    }
    if (endpoint.empty () || is_blank (endpoint)) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "SPOT router manual endpoint is required");
    }
    _state->snapshot.router_manual_rid_connections.push_back (
      {std::move (peer_rid), std::move (endpoint)});
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

spot_node_builder_t &spot_node_builder_t::connect_pub_sub (std::string endpoint)
{
    if (endpoint.empty () || is_blank (endpoint)) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "SPOT pub/sub manual endpoint is required");
    }
    _state->snapshot.pub_sub_manual_connections.push_back (std::move (endpoint));
    return *this;
}

spot_node_builder_t &spot_node_builder_t::connect_peer_pub (std::string endpoint)
{
    return connect_pub_sub (std::move (endpoint));
}

spot_node_builder_t &spot_node_builder_t::set_spot_route_channel (std::string route_channel_name)
{
    if (route_channel_name.empty () || is_blank (route_channel_name)) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "SPOT route channel name is required");
    }
    _state->snapshot.spot_route_channel_name = std::move (route_channel_name);
    return *this;
}

spot_node_builder_t &
spot_node_builder_t::accept_implicit_route_mesh (std::string route_channel_name,
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
      std::any_of (_state->snapshot.accepted_route_channels.begin (),
                   _state->snapshot.accepted_route_channels.end (), [&] (const auto &accepted) {
                       return accepted.channel_name == route_channel_name;
                   });
    if (duplicate) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "duplicate accepted SPOT route channel");
    }
    _state->snapshot.accepted_route_channels.push_back (accepted_spot_route_channel_t{
      std::move (route_channel_name), std::move (manual_connections)});
    return *this;
}

spot_node_builder_t &spot_node_builder_t::add_spot_factory (std::string spot_name,
                                                            std::type_index spot_type,
                                                            bool entry_spot)
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

spot_node_builder_t &spot_node_builder_t::add_actor_factory_erased (
  std::string actor_type,
  std::type_index actor_instance_type,
  std::function<std::shared_ptr<void> (std::string)> create_instance,
  std::function<void (void *, const actor_ref_t &, void *)> configure_instance,
  std::function<std::optional<zlink::message_t> (void *, serializer_registry_t &)>
    serialize_instance,
  std::function<void (void *, const zlink::message_t &, serializer_registry_t &)>
    deserialize_instance)
{
    if (!create_instance || !configure_instance || !serialize_instance || !deserialize_instance) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "actor factory callback must not be empty");
    }
    const auto [_, inserted] = _state->actor_factories.emplace (
      actor_type,
      detail::spot_node_builder_state_t::actor_factory_registration_t{
        actor_instance_type, std::move (create_instance), std::move (configure_instance),
        std::move (serialize_instance), std::move (deserialize_instance)});
    if (!inserted) {
        throw framework_exception_t (framework_error_kind_t::actor_already_exists,
                                     "duplicate actor factory registration");
    }
    _state->snapshot.actor_types.push_back (std::move (actor_type));
    return *this;
}

spot_node_builder_t &spot_node_builder_t::add_actor_transfer_erased (
  std::string actor_type,
  std::type_index actor_instance_type,
  std::function<task_t<message_t> (const void *)> transfer_out,
  std::function<task_t<std::shared_ptr<void>> (std::string, message_t)> transfer_in)
{
    if (actor_type.empty () || is_blank (actor_type)) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "actor transfer name must not be empty");
    }
    if (!transfer_out || !transfer_in) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "actor transfer adapter callbacks must not be empty");
    }
    const auto [_, inserted] = _state->actor_transfers.emplace (
      actor_type,
      detail::spot_node_builder_state_t::actor_transfer_registration_t{
        actor_instance_type, std::move (transfer_out), std::move (transfer_in)});
    if (!inserted) {
        throw framework_exception_t (framework_error_kind_t::actor_already_exists,
                                     "duplicate actor transfer registration");
    }
    return *this;
}

void spot_node_builder_t::register_lifecycle_erased (std::string spot_name,
                                                     detail::spot_lifecycle_callbacks_t callbacks)
{
    _state->spot_lifecycles[std::move (spot_name)] = std::move (callbacks);
}

spot_node_builder_t &spot_node_builder_t::add_spot_resolver (
  std::string name, std::function<std::optional<spot_route_t> (spot_rid_t)> resolver)
{
    if (name.empty () || !resolver) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "spot resolver requires a name and callback");
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

spot_create_result_t spot_node_builder_t::create_spot (std::string spot_name)
{
    return detail::spot_node_runtime_t (_state).create_spot (std::move (spot_name));
}

spot_create_result_t spot_node_builder_t::create_spot_raw (std::string spot_name,
                                                           zlink::message_t request)
{
    return detail::spot_node_runtime_t (_state).create_spot (std::move (spot_name),
                                                             std::move (request));
}

spot_create_result_t spot_node_builder_t::create_spot (std::string spot_name,
                                                       const message_t &request)
{
    if (!_state || !_state->channel_runtime || !_state->channel_runtime->serializers) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "spot create requires a serializer registry");
    }
    return create_spot_raw (
      std::move (spot_name),
      detail::message_to_raw (request, *_state->channel_runtime->serializers));
}

spot_create_result_t spot_node_builder_t::get_or_create_spot (std::string spot_name,
                                                              spot_rid_t spot_rid)
{
    return detail::spot_node_runtime_t (_state).get_or_create_spot (std::move (spot_name),
                                                                    std::move (spot_rid));
}

spot_create_result_t spot_node_builder_t::get_or_create_spot_raw (std::string spot_name,
                                                                  spot_rid_t spot_rid,
                                                                  zlink::message_t request)
{
    return detail::spot_node_runtime_t (_state).get_or_create_spot (
      std::move (spot_name), std::move (spot_rid), std::move (request));
}

spot_create_result_t spot_node_builder_t::get_or_create_spot (std::string spot_name,
                                                              spot_rid_t spot_rid,
                                                              const message_t &request)
{
    if (!_state || !_state->channel_runtime || !_state->channel_runtime->serializers) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "spot get or create requires a serializer registry");
    }
    return get_or_create_spot_raw (
      std::move (spot_name), std::move (spot_rid),
      detail::message_to_raw (request, *_state->channel_runtime->serializers));
}

task_t<std::optional<spot_info_t>> spot_node_builder_t::find_spot (spot_rid_t spot_rid) const
{
    return task_t<std::optional<spot_info_t>> (result_t<std::optional<spot_info_t>>::success (
      detail::spot_node_runtime_t (_state).find_spot (std::move (spot_rid))));
}

task_t<std::vector<spot_info_t>> spot_node_builder_t::list_spots () const
{
    return task_t<std::vector<spot_info_t>> (result_t<std::vector<spot_info_t>>::success (
      detail::spot_node_runtime_t (_state).list_spots ()));
}

task_t<bool> spot_node_builder_t::close_spot (spot_rid_t spot_rid)
{
    return detail::spot_node_runtime_t (_state).close_spot (std::move (spot_rid));
}

std::optional<std::string> spot_node_builder_t::spot_name_for (spot_rid_t spot_rid) const
{
    return detail::spot_node_runtime_t (_state).spot_name_for (std::move (spot_rid));
}

std::optional<spot_route_t> spot_node_builder_t::resolve_spot (spot_rid_t spot_rid) const
{
    return detail::spot_node_runtime_t (_state).resolve_spot (std::move (spot_rid));
}

spot_node_builder_t zlink_builder_t::add_spot_node (std::string spot_node_name)
{
    auto state = std::make_shared<detail::spot_node_builder_state_t> (std::move (spot_node_name));
    state->channel_runtime = _state->runtime;
    // Spot nodes created after apply_dispatch_options (framework appliers run
    // last) inherit the already-applied dispatch options; without this the
    // spot dispatch surface would silently trace nothing.
    state->dispatch = _state->runtime->dispatch;
    state->snapshot.discovery_channel_name = state->snapshot.name;
    _state->spot_nodes[state->snapshot.name] = state;
    return spot_node_builder_t (state);
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

spot_node_runtime_t::spot_node_runtime_t (std::shared_ptr<spot_node_builder_state_t> state) :
    _state (std::move (state))
{
}

} // namespace zlink::framework::detail

namespace zlink::framework
{

spot_node_manager_t::spot_node_manager_t () :
    _state (std::make_shared<detail::spot_node_builder_state_t> (""))
{
}

spot_node_manager_t::spot_node_manager_t (
  std::shared_ptr<detail::spot_node_builder_state_t> state) :
    _state (std::move (state))
{
}

spot_node_manager_t::~spot_node_manager_t () = default;
spot_node_manager_t::spot_node_manager_t (spot_node_manager_t &&) noexcept = default;
spot_node_manager_t &spot_node_manager_t::operator= (spot_node_manager_t &&) noexcept = default;

spot_create_result_t spot_node_manager_t::create_spot (std::string spot_name)
{
    return detail::spot_node_runtime_t (_state).create_spot (std::move (spot_name));
}

spot_create_result_t spot_node_manager_t::create_spot_raw (std::string spot_name,
                                                           zlink::message_t request)
{
    return detail::spot_node_runtime_t (_state).create_spot (std::move (spot_name),
                                                             std::move (request));
}

spot_create_result_t spot_node_manager_t::create_spot (std::string spot_name,
                                                       const message_t &request)
{
    if (!_state || !_state->channel_runtime || !_state->channel_runtime->serializers) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "spot create requires a serializer registry");
    }
    return create_spot_raw (
      std::move (spot_name),
      detail::message_to_raw (request, *_state->channel_runtime->serializers));
}

spot_create_result_t spot_node_manager_t::get_or_create_spot (std::string spot_name,
                                                              spot_rid_t spot_rid)
{
    return detail::spot_node_runtime_t (_state).get_or_create_spot (std::move (spot_name),
                                                                    std::move (spot_rid));
}

spot_create_result_t spot_node_manager_t::get_or_create_spot_raw (std::string spot_name,
                                                                  spot_rid_t spot_rid,
                                                                  zlink::message_t request)
{
    return detail::spot_node_runtime_t (_state).get_or_create_spot (
      std::move (spot_name), std::move (spot_rid), std::move (request));
}

spot_create_result_t spot_node_manager_t::get_or_create_spot (std::string spot_name,
                                                              spot_rid_t spot_rid,
                                                              const message_t &request)
{
    if (!_state || !_state->channel_runtime || !_state->channel_runtime->serializers) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "spot get or create requires a serializer registry");
    }
    return get_or_create_spot_raw (
      std::move (spot_name), std::move (spot_rid),
      detail::message_to_raw (request, *_state->channel_runtime->serializers));
}

zlink::message_t spot_node_manager_t::serialize_request (std::type_index request_type,
                                                         const void *request) const
{
    if (!_state || !_state->channel_runtime || !_state->channel_runtime->serializers) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "spot request requires a serializer registry");
    }
    return detail::encoded_payload_to_raw (
      _state->channel_runtime->serializers->serialize (request_type, request));
}

task_t<std::optional<spot_info_t>> spot_node_manager_t::find_spot (spot_rid_t spot_rid) const
{
    return task_t<std::optional<spot_info_t>> (result_t<std::optional<spot_info_t>>::success (
      detail::spot_node_runtime_t (_state).find_spot (std::move (spot_rid))));
}

task_t<std::vector<spot_info_t>> spot_node_manager_t::list_spots () const
{
    return task_t<std::vector<spot_info_t>> (result_t<std::vector<spot_info_t>>::success (
      detail::spot_node_runtime_t (_state).list_spots ()));
}

task_t<bool> spot_node_manager_t::close_spot (spot_rid_t spot_rid)
{
    return detail::spot_node_runtime_t (_state).close_spot (std::move (spot_rid));
}

std::optional<std::string> spot_node_manager_t::spot_name_for (spot_rid_t spot_rid) const
{
    return detail::spot_node_runtime_t (_state).spot_name_for (std::move (spot_rid));
}

std::optional<spot_route_t> spot_node_manager_t::resolve_spot (spot_rid_t spot_rid) const
{
    return detail::spot_node_runtime_t (_state).resolve_spot (std::move (spot_rid));
}

std::optional<actor_ref_t>
spot_node_manager_t::current_actor_ref (const actor_ref_t &actor_ref) const
{
    return detail::spot_node_runtime_t (_state).current_actor_ref (actor_ref);
}

result_t<std::optional<zlink::message_t>>
spot_node_manager_t::relay_actor_packet (const actor_ref_t &actor_ref,
                                         actor_context_t actor_context,
                                         std::string_view packet_name,
                                         const zlink::message_t &message,
                                         service_provider_t &services,
                                         serializer_registry_t &serializers,
                                         spot_actor_message_metadata_t metadata)
{
    return relay_actor_packet (actor_ref, std::move (actor_context),
                               detail::stream_message_kind_t::request, packet_name, message,
                               services, serializers, std::move (metadata));
}

result_t<std::optional<zlink::message_t>>
spot_node_manager_t::relay_actor_packet (const actor_ref_t &actor_ref,
                                         actor_context_t actor_context,
                                         detail::stream_message_kind_t message_kind,
                                         std::string_view packet_name,
                                         const zlink::message_t &message,
                                         service_provider_t &services,
                                         serializer_registry_t &serializers,
                                         spot_actor_message_metadata_t metadata)
{
    if (_state->actor_packet_relay) {
        return _state->actor_packet_relay (actor_ref, std::move (actor_context), message_kind,
                                           packet_name, message, services, serializers,
                                           std::move (metadata));
    }
    return detail::spot_node_runtime_t (_state).relay_actor_packet (
      actor_ref, std::move (actor_context), message_kind, packet_name, message, services,
      serializers, std::move (metadata));
}

spot_publisher_client_t::spot_publisher_client_t (spot_node_manager_t manager,
                                                  serializer_registry_t &serializers) :
    _manager (std::move (manager)), _serializers (&serializers)
{
}

task_t<void> spot_publisher_client_t::publish_raw (std::string channel_name,
                                                   std::string topic,
                                                   zlink::message_t payload) const
{
    if (!_serializers) {
        return task_t<void> (
          result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                   "spot publisher client has no serializer registry"));
    }
    if (channel_name.empty () || is_blank (channel_name)) {
        return task_t<void> (
          result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                   "SPOT publisher channel name is required"));
    }
    if (topic.empty ()) {
        return task_t<void> (result_t<void>::failure (
          framework_error_kind_t::request_protocol_error, "spot publish topic is required"));
    }

    std::shared_ptr<zlink::service::spot_node_t> native_node;
    {
        std::lock_guard<std::recursive_mutex> node_lock (_manager._state->mutex);
        native_node = _manager._state->native_node.lock ();
    }
    if (!native_node) {
        return task_t<void> (detail::boundary_failure<void> (detail::boundary_error_t::disconnected,
                                                      "SPOT publisher client node is not running"));
    }

    try {
        std::vector<zlink::message_t> parts{std::move (payload)};
        auto publisher = native_node->create_publisher ();
        if (!publisher.valid ()) {
            return task_t<void> (detail::boundary_failure<void> (detail::boundary_error_t::disconnected, "SPOT publisher client is not available"));
        }
        (void) publisher.publish (topic, parts);
        return task_t<void> (result_t<void>::success ());
    }
    catch (const framework_exception_t &error) {
        return task_t<void> (
          detail::result_access_t::failure<void> (error));
    }
    catch (const std::exception &error) {
        return task_t<void> (
          result_t<void>::failure (framework_error_kind_t::request_failed, error.what ()));
    }
}

} // namespace zlink::framework

namespace zlink::framework::detail
{

spot_node_manager_t spot_node_runtime_t::manager () const
{
    return spot_node_manager_t (_state);
}

result_t<spot_context_t>
spot_node_runtime_t::actor_join_context_unlocked (spot_rid_t spot_rid,
                                                  const zlink::message_t &request)
{
    auto context = find_context (spot_rid);
    if (!context || !context->_state->spot_instance) {
        std::optional<std::string> dynamic_spot_name;
        for (const auto &[spot_name, _] : _state->spot_factories) {
            if (_state->snapshot.entry_spot_name
                && spot_name == *_state->snapshot.entry_spot_name) {
                continue;
            }
            if (dynamic_spot_name) {
                dynamic_spot_name.reset ();
                break;
            }
            dynamic_spot_name = spot_name;
        }
        if (dynamic_spot_name) {
            (void) get_or_create_spot (*dynamic_spot_name, spot_rid, request);
            context = find_context (spot_rid);
        }
    }
    if (!context || !context->_state->spot_instance) {
        return result_t<spot_context_t>::failure (framework_error_kind_t::spot_route_not_found,
                                                  "target spot is not registered");
    }
    return result_t<spot_context_t>::success (std::move (*context));
}

result_t<std::reference_wrapper<spot_node_builder_state_t::actor_factory_registration_t>>
spot_node_runtime_t::actor_factory_unlocked (const actor_ref_t &actor_ref) const
{
    const auto found = _state->actor_factories.find (std::string (actor_ref.actor_type ()));
    if (found == _state->actor_factories.end ()) {
        return result_t<
          std::reference_wrapper<spot_node_builder_state_t::actor_factory_registration_t>>::
          failure (framework_error_kind_t::actor_route_not_found,
                   "actor factory is not registered");
    }
    return result_t<std::reference_wrapper<
      spot_node_builder_state_t::actor_factory_registration_t>>::success (found->second);
}

result_t<std::reference_wrapper<spot_actor_admission_callbacks_t>>
spot_node_runtime_t::actor_admission_unlocked (spot_context_t &context,
                                               std::type_index actor_type,
                                               spot_rid_t spot_rid,
                                               const actor_ref_t &actor_ref)
{
    const auto admission = context._state->actor_admissions.find (actor_type);
    if (admission == context._state->actor_admissions.end () || !admission->second.join) {
        report_spot_dispatch_error (
          _state, dispatch_error_surface_t::spot_actor, dispatch_message_kind_t::actor_request,
          dispatch_error_reason_t::handler_missing, dispatch_error_action_t::reply_error,
          "actor.join", std::nullopt, std::string (spot_rid.value ()),
          std::string (actor_ref.actor_id ()));
        return result_t<std::reference_wrapper<spot_actor_admission_callbacks_t>>::failure (
          framework_error_kind_t::handler_not_found, "spot actor join callback is not registered");
    }
    return result_t<std::reference_wrapper<spot_actor_admission_callbacks_t>>::success (
      admission->second);
}

void spot_node_runtime_t::leave_previous_actor_route (
  const std::string &key,
  std::type_index actor_type,
  void *actor,
  std::unique_lock<std::recursive_mutex> &node_lock)
{
    const auto previous = _state->actor_spot_rids.find (key);
    if (previous == _state->actor_spot_rids.end ()) {
        return;
    }
    if (auto previous_context = find_context (previous->second)) {
        auto &previous_state = *previous_context->_state;
        if (const auto previous_admission = previous_state.actor_admissions.find (actor_type);
            previous_admission != previous_state.actor_admissions.end ()
            && previous_admission->second.on_leave_actor && previous_state.spot_instance) {
            // The leave callback is user code on the spot serial queue. It may call back
            // into the framework (sends, joins) that need the node mutex from the serial
            // thread, so the node mutex must not be held across this wait.
            node_lock.unlock ();
            bool posted = false;
            try {
                posted = previous_state.run_serial_sync ("spot-actor-leave", [&] {
                    previous_admission->second.on_leave_actor (previous_state.spot_instance.get (),
                                                             actor);
                });
            }
            catch (...) {
                node_lock.lock ();
                throw;
            }
            node_lock.lock ();
            if (!posted) {
                throw framework_exception_t (framework_error_kind_t::request_rejected,
                                             "spot serial queue is full");
            }
        }
        decrement_actor_count_unlocked (previous_state);
    }
    erase_actor_route_unlocked (*_state, key);
}

void spot_node_runtime_t::commit_accepted_actor_join_unlocked (
  const std::string &key,
  spot_context_t &context,
  const actor_ref_t &committed,
  std::type_index actor_type,
  void *actor,
  const spot_actor_admission_callbacks_t &admission,
  bool create_entry_actor,
  const zlink::message_t &create_request,
  bool &source_left)
{
    source_left = false;
    std::optional<spot_context_t> previous_context;
    if (const auto previous = _state->actor_spot_rids.find (key);
        previous != _state->actor_spot_rids.end ()) {
        previous_context = find_context (previous->second);
    }
    if (previous_context) {
        auto &previous_state = *previous_context->_state;
        if (const auto previous_admission = previous_state.actor_admissions.find (actor_type);
            previous_admission != previous_state.actor_admissions.end ()
            && previous_admission->second.on_leave_actor && previous_state.spot_instance) {
            if (!previous_state.run_serial_sync ("spot-actor-leave", [&] {
                    previous_admission->second.on_leave_actor (previous_state.spot_instance.get (),
                                                             actor);
                })) {
                throw framework_exception_t (framework_error_kind_t::request_rejected,
                                             "spot serial queue is full");
            }
        }
        source_left = true;
    }
    auto &target_state = *context._state;
    if (create_entry_actor && admission.entry_spot && !_state->actor_created_keys.contains (key)
        && admission.on_create_actor) {
        auto &serializers = *target_state.channel_runtime->serializers;
        if (!target_state.run_serial_sync ("spot-actor-create", [&] {
                admission.on_create_actor (target_state.spot_instance.get (), actor, create_request,
                                         serializers);
            })) {
            throw framework_exception_t (framework_error_kind_t::request_rejected,
                                         "spot serial queue is full");
        }
        _state->actor_created_keys.insert (key);
    }
    if (admission.on_actor_joined) {
        if (!target_state.run_serial_sync ("spot-actor-joined", [&] {
                admission.on_actor_joined (target_state.spot_instance.get (), actor);
            })) {
            throw framework_exception_t (framework_error_kind_t::request_rejected,
                                         "spot serial queue is full");
        }
    }
    if (previous_context) {
        decrement_actor_count_unlocked (*previous_context->_state);
    }
    erase_actor_route_unlocked (*_state, key);
    _state->destroyed_actor_keys.erase (key);
    if (auto native = _state->native_node.lock ();
        native && !_state->native_actors.contains (key)) {
        _state->native_actors.emplace (
          key, std::make_unique<zlink::service::actor_t> (
                 native->create_actor (std::string (committed.actor_id ()))));
    }
    detail::record_actor_context_route_unlocked (*_state, key,
                                                 detail::effective_spot_node_rid (_state->snapshot),
                                                 target_state, committed.generation ());
    const auto location_updated =
      update_actor_location_after_move (*_state, committed, target_state, false);
    if (!location_updated) {
        throw framework_exception_t (location_updated.error_kind (),
                                     location_updated.error ()
                                       ? location_updated.error ()->what ()
                                       : "actor committed location update failed");
    }
    if (_state->update_actor_registry_ref) {
        const auto updated = _state->update_actor_registry_ref (committed);
        if (!updated) {
            throw framework_exception_t (updated.error_kind (), updated.error ()
                                                                  ? updated.error ()->what ()
                                                                  : "actor ref update failed");
        }
    }
}

result_t<actor_join_reply_t> spot_node_runtime_t::join_actor_to_spot_erased (
  const actor_ref_t &actor_ref,
  spot_rid_t spot_rid,
  const zlink::message_t &request,
  const std::optional<zlink::message_t> &actor_snapshot)
{
    if (actor_ref.empty ()) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::actor_route_not_found,
                                                      "actor ref is empty");
    }
    auto context = actor_join_context_unlocked (spot_rid, request);
    if (!context) {
        return detail::propagate_failure<actor_join_reply_t> (context, "target spot is not registered");
    }
    auto actor_factory = actor_factory_unlocked (actor_ref);
    if (!actor_factory) {
        return detail::propagate_failure<actor_join_reply_t> (actor_factory, "actor factory failed");
    }
    const auto key = actor_key (actor_ref);
    /* Registration is double-checked: an already-registered actor is taken
     * under the node mutex without touching the factory, and only a first
     * registration constructs — outside the mutex, because the factory is user
     * code and must not be able to invert lock order. The map entry and its
     * identity index entry are then installed together under the mutex, so a
     * concurrent destroy never sees one without the other. */
    std::shared_ptr<void> actor_instance = registered_actor_instance (actor_ref, key);
    if (!actor_instance) {
        auto created_instance =
          actor_factory.value ().get ().create_instance (std::string (actor_ref.actor_id ()));
        if (!created_instance) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::actor_route_not_found, "actor factory returned null");
        }
        actor_instance = install_actor_instance (actor_ref, key, std::move (created_instance));
        if (!actor_instance) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::actor_route_not_found, "actor has been destroyed");
        }
    }
    if (actor_snapshot) {
        auto &serializers = *context.value ()._state->channel_runtime->serializers;
        actor_factory.value ().get ().deserialize_instance (actor_instance.get (), *actor_snapshot,
                                                            serializers);
    }
    auto admission = actor_admission_unlocked (
      context.value (), actor_factory.value ().get ().actor_type, spot_rid, actor_ref);
    if (!admission) {
        return detail::propagate_failure<actor_join_reply_t> (admission, "actor admission failed");
    }

    auto committed =
      actor_ref_t (node_rid_t::from_string (detail::effective_spot_node_rid (_state->snapshot)),
                   std::string (actor_ref.actor_type ()), std::string (actor_ref.actor_id ()),
                   actor_ref.generation () + 1);
    const auto source_spot = _state->actor_spot_rids.find (key);
    const auto source_spot_rid =
      source_spot == _state->actor_spot_rids.end () ? spot_rid_t{} : source_spot->second;
    auto &serializers = *context.value ()._state->channel_runtime->serializers;
    const auto response = admission.value ().get ().join (
      context.value ()._state->spot_instance.get (), actor_ref.actor_id (), request, serializers);
    if (!response.accepted) {
        return result_t<actor_join_reply_t>::success (
          actor_join_reply_t{1, actor_ref, framework_reply_or_empty (response.reply, serializers)});
    }

    if (!_state->actor_transfer_coordinator.try_begin_local (key)) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::request_rejected,
                                                      "actor move is already in progress");
    }
    bool claimed_location = false;
    const auto location_claim = claim_pending_actor_location_before_activation (
      _state, actor_ref, source_spot_rid, committed, *context.value ()._state,
      claimed_location);
    if (!location_claim) {
        _state->actor_transfer_coordinator.cancel_move (key);
        return result_t<actor_join_reply_t>::failure (
          location_claim.error_kind (), location_claim.error () ? location_claim.error ()->what ()
                                                                : "actor location claim failed");
    }
    bool source_left = false;
    auto fail_local_commit = [&] {
        if (source_left || source_spot_rid.empty ()) {
            release_actor_location (*_state, committed);
        }
        if (source_left) {
            _state->actor_transfer_coordinator.mark_reconcile (key);
        } else {
            _state->actor_transfer_coordinator.cancel_move (key);
        }
    };
    try {
        actor_factory.value ().get ().configure_instance (actor_instance.get (), committed,
                                                          nullptr);
        commit_accepted_actor_join_unlocked (
          key, context.value (), committed, actor_factory.value ().get ().actor_type,
          actor_instance.get (), admission.value ().get (), true, request, source_left);
        _state->actor_transfer_coordinator.complete_move (key);
    }
    catch (const framework_exception_t &error) {
        fail_local_commit ();
        return detail::result_access_t::failure<actor_join_reply_t> (error);
    }
    catch (const std::exception &error) {
        fail_local_commit ();
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::request_failed,
                                                      error.what ());
    }
    catch (...) {
        fail_local_commit ();
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::request_failed,
                                                      "actor join commit failed");
    }
    return result_t<actor_join_reply_t>::success (
      actor_join_reply_t{0, committed, framework_reply_or_empty (response.reply, serializers)});
}

result_t<actor_join_reply_t>
spot_node_runtime_t::join_remote_actor_to_spot_erased (const actor_ref_t &actor_ref,
                                                       spot_rid_t spot_rid,
                                                       const zlink::message_t &request,
                                                       actor_context_t actor_context)
{
    /* graceful-drain-handoff §4-2/§5.2: a draining node rejects new actor
    * admission and joins; already-admitted transfer commits stay accepted. */
    if (_state->drain_flag && _state->drain_flag->load (std::memory_order_acquire)) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::request_rejected,
                                                      "spot node is draining and rejects new actor joins");
    }
    if (actor_ref.empty ()) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::actor_route_not_found,
                                                      "actor ref is empty");
    }
    auto context = actor_join_context_unlocked (spot_rid, request);
    if (!context) {
        return detail::propagate_failure<actor_join_reply_t> (context, "target spot is not registered");
    }

    auto actor_factory = actor_factory_unlocked (actor_ref);
    if (!actor_factory) {
        return detail::propagate_failure<actor_join_reply_t> (actor_factory, "actor factory failed");
    }

    const auto key = actor_key (actor_ref);
    /* Registration is double-checked: an already-registered actor is taken
     * under the node mutex without touching the factory, and only a first
     * registration constructs — outside the mutex, because the factory is user
     * code and must not be able to invert lock order. The map entry and its
     * identity index entry are then installed together under the mutex, so a
     * concurrent destroy never sees one without the other. */
    std::shared_ptr<void> actor_instance = registered_actor_instance (actor_ref, key);
    if (!actor_instance) {
        auto created_instance =
          actor_factory.value ().get ().create_instance (std::string (actor_ref.actor_id ()));
        if (!created_instance) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::actor_route_not_found, "actor factory returned null");
        }
        actor_instance = install_actor_instance (actor_ref, key, std::move (created_instance));
        if (!actor_instance) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::actor_route_not_found, "actor has been destroyed");
        }
    }
    auto committed_context = actor_context_t (actor_context._state, actor_ref);
    actor_factory.value ().get ().configure_instance (actor_instance.get (), actor_ref,
                                                      &committed_context);

    auto admission = actor_admission_unlocked (
      context.value (), actor_factory.value ().get ().actor_type, spot_rid, actor_ref);
    if (!admission) {
        return detail::propagate_failure<actor_join_reply_t> (admission, "actor admission failed");
    }

    auto &serializers = *context.value ()._state->channel_runtime->serializers;
    const auto response = admission.value ().get ().join (
      context.value ()._state->spot_instance.get (), actor_ref.actor_id (), request, serializers);
    if (!response.accepted) {
        actor_factory.value ().get ().configure_instance (actor_instance.get (), actor_ref,
                                                          &actor_context);
        return result_t<actor_join_reply_t>::success (
          actor_join_reply_t{1, actor_ref, framework_reply_or_empty (response.reply, serializers)});
    }

    bool claimed_location = false;
    const auto location_claim = claim_pending_actor_location_before_activation (
      _state, actor_ref, spot_rid_t{}, actor_ref, *context.value ()._state, claimed_location);
    if (!location_claim) {
        return result_t<actor_join_reply_t>::failure (
          location_claim.error_kind (), location_claim.error () ? location_claim.error ()->what ()
                                                                : "actor location claim failed");
    }
    bool source_left = false;
    try {
        commit_accepted_actor_join_unlocked (
          key, context.value (), actor_ref, actor_factory.value ().get ().actor_type,
          actor_instance.get (), admission.value ().get (), false, request, source_left);
    }
    catch (...) {
        if (claimed_location) {
            release_actor_location (*_state, actor_ref);
        }
        throw;
    }
    return result_t<actor_join_reply_t>::success (
      actor_join_reply_t{0, actor_ref, framework_reply_or_empty (response.reply, serializers)});
}

std::size_t spot_node_runtime_t::cleanup_expired_actor_admissions ()
{
    return cleanup_expired_actor_admissions_at (std::chrono::steady_clock::now ());
}

std::size_t spot_node_runtime_t::cleanup_expired_actor_admissions_at (
  std::chrono::steady_clock::time_point now)
{
    const auto expired = _state->actor_transfer_coordinator.cleanup_expired (now);
    for (const auto &entry : expired) {
        emit_actor_transfer_marker ("pending_admission_expired", entry.admission.source_actor,
                                    entry.transfer_id, entry.admission.target_spot_rid);
    }
    std::size_t removed = expired.size ();
    std::vector<spot_node_builder_state_t::pending_remote_source_cleanup_t> cleaned_sources;
    {
        std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
        for (auto found = _state->pending_remote_source_cleanups.begin ();
             found != _state->pending_remote_source_cleanups.end ();) {
            if (found->not_before > now) {
                ++found;
                continue;
            }
            const auto key = actor_key (found->source_actor);
            _state->actor_instances.erase (key);
            detail::erase_actor_instance_index_unlocked (
              *_state, found->source_actor.actor_type (), found->source_actor.actor_id ());
            _state->actor_mailboxes.erase (key);
            release_actor_location (*_state, found->source_actor);
            cleaned_sources.push_back (std::move (*found));
            found = _state->pending_remote_source_cleanups.erase (found);
            ++removed;
        }
    }
    for (const auto &cleanup : cleaned_sources) {
        emit_actor_transfer_marker ("source_cleanup", cleanup.source_actor,
                                    cleanup.transfer_id, cleanup.target_spot_rid);
    }
    const auto evicted = _state->actor_transfer_coordinator.evict_expired_forwarding (now);
    if (!evicted.empty ()) {
        std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
        for (const auto &entry : evicted) {
            const auto &key = entry.actor_key;
            // The forwarding window ended (§10.4-3): drop the retained route so
            // this node stops forwarding. The generation record stays behind as
            // a tombstone so stale refs keep failing fast instead of
            // re-materializing the actor here.
            _state->actor_routes.erase (key);
            _state->native_actors.erase (key);
            const auto separator = key.find (':');
            if (separator != std::string::npos) {
                const auto actor_ref = actor_ref_t (
                  node_rid (), key.substr (0, separator), key.substr (separator + 1),
                  entry.old_generation);
                emit_actor_transfer_marker (
                  "mapping_evicted", actor_ref,
                  entry.transfer_id.empty () ? key : entry.transfer_id);
            }
            ++removed;
        }
    }
    return removed;
}

std::vector<handoff_packet_t>
spot_node_runtime_t::take_actor_handoff_backlog (const actor_ref_t &actor_ref)
{
    return _state->actor_transfer_coordinator.take_backlog (actor_key (actor_ref));
}

void spot_node_runtime_t::set_actor_transfer_forward_window (std::chrono::milliseconds window)
{
    _state->actor_transfer_forward_window = window;
}

result_t<spot_actor_join_response_t>
spot_node_runtime_t::admit_remote_actor_to_spot (std::string transfer_id,
                                                 const actor_ref_t &actor_ref,
                                                 spot_rid_t source_spot_rid,
                                                 spot_rid_t target_spot_rid,
                                                 const zlink::message_t &request)
{
    /* graceful-drain-handoff §4-2/§5.2: a draining node rejects new actor
    * admission and joins; already-admitted transfer commits stay accepted. */
    if (_state->drain_flag && _state->drain_flag->load (std::memory_order_acquire)) {
        return result_t<spot_actor_join_response_t>::failure (
          framework_error_kind_t::request_rejected,
          "spot node is draining and rejects new actor admission");
    }
    std::unique_lock<std::recursive_mutex> node_lock (_state->mutex);
    cleanup_expired_actor_admissions ();
    if (transfer_id.empty () || actor_ref.empty ()) {
        return result_t<spot_actor_join_response_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "remote actor admission requires transfer and actor identity");
    }
    auto context = actor_join_context_unlocked (target_spot_rid, request);
    if (!context) {
        return detail::propagate_failure<spot_actor_join_response_t> (context, "target spot is not registered");
    }
    auto actor_factory = actor_factory_unlocked (actor_ref);
    if (!actor_factory) {
        return detail::propagate_failure<spot_actor_join_response_t> (actor_factory, "actor factory failed");
    }
    auto admission = actor_admission_unlocked (context.value (),
                                               actor_factory.value ().get ().actor_type,
                                               target_spot_rid, actor_ref);
    if (!admission) {
        return detail::propagate_failure<spot_actor_join_response_t> (admission, "actor admission failed");
    }

    auto &target = *context.value ()._state;
    auto &serializers = *target.channel_runtime->serializers;
    spot_actor_join_response_t response;
    // The admission callback is user code on the spot serial queue. It may call back
    // into the framework (sends, joins) that need the node mutex from the serial
    // thread, so the node mutex must not be held across this wait.
    node_lock.unlock ();
    if (!target.run_serial_sync ("spot-actor-admission", [&] {
            response = admission.value ().get ().join (target.spot_instance.get (),
                                                       actor_ref.actor_id (),
                                                       request, serializers);
        })) {
        return result_t<spot_actor_join_response_t>::failure (
          framework_error_kind_t::request_rejected, "spot serial queue is full");
    }
    node_lock.lock ();
    if (response.accepted) {
        const auto timeout =
          _state->channel_runtime
            ? _state->channel_runtime->default_request_timeout
            : std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::seconds (30));
        if (!_state->actor_transfer_coordinator.try_add_admission (
              std::move (transfer_id),
              pending_actor_admission_t{actor_key (actor_ref), actor_ref,
                                        std::move (source_spot_rid), std::move (target_spot_rid),
                                        std::chrono::steady_clock::now () + timeout})) {
            return result_t<spot_actor_join_response_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "remote actor admission is already pending");
        }
    }
    return result_t<spot_actor_join_response_t>::success (std::move (response));
}

result_t<spot_node_runtime_t::remote_actor_transfer_t>
spot_node_runtime_t::transfer_actor_out (const actor_ref_t &actor_ref,
                                         std::string transfer_id)
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto key = actor_key (actor_ref);
    const auto transfer = _state->actor_transfers.find (std::string (actor_ref.actor_type ()));
    const auto actor = _state->actor_instances.find (key);
    const auto source_spot = _state->actor_spot_rids.find (key);
    if (actor == _state->actor_instances.end () || !actor->second
        || source_spot == _state->actor_spot_rids.end ()) {
        return result_t<remote_actor_transfer_t>::failure (
          framework_error_kind_t::actor_route_not_found,
          "source actor is not joined to a local spot");
    }
    if (transfer_id.empty ()) {
        transfer_id = key;
    }
    if (!_state->actor_transfer_coordinator.try_begin_source_remote (key,
                                                                     std::move (transfer_id))) {
        return result_t<remote_actor_transfer_t>::failure (framework_error_kind_t::request_rejected,
                                                           "actor transfer is already in progress");
    }
    // One histogram sample per transfer, taken right at the moving transition
    // (runtime-metrics §4.3): the coordinator now blocks new dispatches, so the
    // counter is exactly the requests still in flight across the move.
    {
        runtime::runtime_metrics_t metrics (_state->monitoring);
        if (metrics.enabled ()) {
            std::size_t pending = 0;
            {
                const std::lock_guard<std::mutex> pending_lock (
                  _state->actor_pending_requests_mutex);
                const auto found = _state->actor_pending_requests.find (key);
                if (found != _state->actor_pending_requests.end ()) {
                    pending = found->second;
                }
            }
            metrics.histogram ("zlink.actor.transfer.pending_requests.count", "{request}",
                               static_cast<double> (pending));
        }
    }
    if (transfer == _state->actor_transfers.end ()) {
        return result_t<remote_actor_transfer_t>::success (
          remote_actor_transfer_t{source_spot->second, zlink::message_t{}});
    }
    try {
        auto state = transfer->second.transfer_out (actor->second.get ()).result ();
        if (!state) {
            _state->actor_transfer_coordinator.cancel_move (key);
            return detail::propagate_failure<remote_actor_transfer_t> (state, "actor transfer-out failed");
        }
        return result_t<remote_actor_transfer_t>::success (remote_actor_transfer_t{
          source_spot->second,
          detail::message_to_raw (state.value (), *_state->channel_runtime->serializers)});
    }
    catch (const framework_exception_t &error) {
        _state->actor_transfer_coordinator.cancel_move (key);
        return detail::result_access_t::failure<remote_actor_transfer_t> (error);
    }
    catch (const std::exception &error) {
        _state->actor_transfer_coordinator.cancel_move (key);
        return result_t<remote_actor_transfer_t>::failure (framework_error_kind_t::request_failed,
                                                           error.what ());
    }
    catch (...) {
        _state->actor_transfer_coordinator.cancel_move (key);
        return result_t<remote_actor_transfer_t>::failure (framework_error_kind_t::request_failed,
                                                           "actor transfer-out failed");
    }
}

std::string spot_node_runtime_t::next_actor_transfer_id ()
{
    return _state->actor_transfer_coordinator.next_transfer_id (
      detail::effective_spot_node_rid (_state->snapshot));
}

result_t<void> spot_node_runtime_t::leave_actor_for_remote_transfer (const actor_ref_t &actor_ref)
{
    std::unique_lock<std::recursive_mutex> node_lock (_state->mutex);
    const auto key = actor_key (actor_ref);
    const auto actor = _state->actor_instances.find (key);
    const auto factory = _state->actor_factories.find (std::string (actor_ref.actor_type ()));
    if (actor == _state->actor_instances.end () || !actor->second
        || factory == _state->actor_factories.end ()
        || _state->actor_spot_rids.find (key) == _state->actor_spot_rids.end ()) {
        return result_t<void>::failure (framework_error_kind_t::actor_route_not_found,
                                        "source actor is not joined to a local spot");
    }
    if (_state->actor_transfer_coordinator.phase (key)
        != std::make_optional (actor_move_phase_t::source_remote)) {
        return result_t<void>::failure (framework_error_kind_t::request_rejected,
                                        "actor transfer has not been prepared");
    }
    try {
        leave_previous_actor_route (key, factory->second.actor_type, actor->second.get (),
                                    node_lock);
        return result_t<void>::success ();
    }
    catch (const framework_exception_t &error) {
        _state->actor_transfer_coordinator.cancel_move (key);
        return detail::result_access_t::failure<void> (error);
    }
    catch (const std::exception &error) {
        _state->actor_transfer_coordinator.cancel_move (key);
        return result_t<void>::failure (framework_error_kind_t::request_failed, error.what ());
    }
    catch (...) {
        _state->actor_transfer_coordinator.cancel_move (key);
        return result_t<void>::failure (framework_error_kind_t::request_failed,
                                        "source actor leave failed");
    }
}

void spot_node_runtime_t::fail_remote_actor_transfer (const actor_ref_t &actor_ref, bool reconcile)
{
    const auto key = actor_key (actor_ref);
    if (reconcile) {
        _state->actor_transfer_coordinator.mark_reconcile (key);
    } else {
        _state->actor_transfer_coordinator.cancel_move (key);
    }
}

void spot_node_runtime_t::complete_remote_actor_transfer (const actor_ref_t &source_actor,
                                                          const actor_ref_t &target_actor,
                                                          spot_route_t target_route,
                                                          std::string transfer_id)
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto key = actor_key (source_actor);
    if (transfer_id.empty ()) {
        transfer_id = key;
    }
    const auto target_spot_rid = target_route.spot_rid;
    const auto transfer_elapsed = _state->actor_transfer_coordinator.complete_move (key);
    if (transfer_elapsed) {
        // Commit ack confirmed: one transfers count and one duration sample per
        // completed out→commit-ack move (runtime-metrics §4.3, RMETRIC-004).
        runtime::runtime_metrics_t metrics (_state->monitoring);
        if (metrics.enabled ()) {
            metrics.counter ("zlink.actor.transfers", "{transfer}", 1.0);
            metrics.histogram ("zlink.actor.transfer.duration", "s",
                               std::chrono::duration<double> (*transfer_elapsed).count ());
        }
    }
    detail::record_actor_route_unlocked (*_state, key, std::move (target_route),
                                         target_actor.generation ());
    // The route recorded above is the forwarding mapping of §10.4: stragglers
    // that still carry the old generation follow it to the next hop. Arm its
    // eviction window so the retained state cannot outlive the transfer.
    _state->actor_transfer_coordinator.activate_forwarding (
      key, source_actor.generation (),
      std::chrono::steady_clock::now () + _state->actor_transfer_forward_window,
      transfer_id);
    // Commit acknowledgement fixes the new owner and forwarding route first.
    // Releasing stale source ownership is post-commit housekeeping: losing the
    // source process now cannot roll back the accepted target generation.
    _state->pending_remote_source_cleanups.push_back (
      spot_node_builder_state_t::pending_remote_source_cleanup_t{
        source_actor, std::move (transfer_id), target_spot_rid,
        std::chrono::steady_clock::now () + std::chrono::seconds (1)});
}

void spot_node_runtime_t::emit_actor_transfer_marker (
  std::string marker,
  const actor_ref_t &actor_ref,
  std::string transfer_id,
  std::optional<spot_rid_t> spot_rid,
  std::optional<node_rid_t> target_node_rid) const
{
    message_flow_tracer_t (_state->dispatch).trace (
      message_flow_outcome_t::dispatched,
      [marker = std::move (marker), actor_ref, transfer_id = std::move (transfer_id),
       spot_rid = std::move (spot_rid), target_node_rid = std::move (target_node_rid),
       node_rid = node_rid ()] () mutable {
          return message_flow_event_t{
            .outcome = message_flow_outcome_t::dispatched,
            .surface = dispatch_error_surface_t::spot_actor,
            .message_kind = dispatch_message_kind_t::actor_request,
            .packet_name = std::move (marker),
            .channel_name = target_node_rid
                              ? std::make_optional (
                                  std::string (target_node_rid->value ()))
                              : std::nullopt,
            .correlation_id = transfer_id,
            .source_rid = std::string (node_rid.value ()),
            .spot_rid = spot_rid ? std::make_optional (std::string (spot_rid->value ()))
                                 : std::nullopt,
            .actor_id = std::string (actor_ref.actor_id ()),
            .flow_id = std::move (transfer_id),
            .flow_origin = flow_origin_t::lifecycle};
      });
}

result_t<actor_join_reply_t>
spot_node_runtime_t::prepare_remote_actor_to_spot (std::string transfer_id,
                                                   const actor_ref_t &actor_ref,
                                                   spot_rid_t target_spot_rid,
                                                   zlink::message_t transfer_state,
                                                   actor_context_t actor_context)
{
    std::unique_lock<std::recursive_mutex> node_lock (_state->mutex);
    cleanup_expired_actor_admissions ();
    const auto pending =
      _state->actor_transfer_coordinator.begin_commit (transfer_id, actor_ref, target_spot_rid);
    if (!pending) {
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "remote actor commit has no matching admission");
    }
    const auto transfer = _state->actor_transfers.find (std::string (actor_ref.actor_type ()));
    auto factory = actor_factory_unlocked (actor_ref);
    auto context = find_context (target_spot_rid);
    if (!factory || !context || !context->_state->spot_instance) {
        _state->actor_transfer_coordinator.fail_commit (transfer_id, false);
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::actor_route_not_found,
          "remote actor commit dependencies are not registered");
    }

    const auto committed =
      actor_ref_t (node_rid_t::from_string (detail::effective_spot_node_rid (_state->snapshot)),
                   std::string (actor_ref.actor_type ()), std::string (actor_ref.actor_id ()),
                   actor_ref.generation () + 1);
    bool claimed_location = false;
    auto location_claim = claim_pending_actor_location_before_activation (
      _state, pending->source_actor, pending->source_spot_rid, committed, *context->_state,
      claimed_location);
    if (!location_claim) {
        _state->actor_transfer_coordinator.fail_commit (transfer_id, false);
        return result_t<actor_join_reply_t>::failure (
          location_claim.error_kind (), location_claim.error () ? location_claim.error ()->what ()
                                                                : "actor location claim failed");
    }

    auto fail_target_commit = [&] (bool reconcile) {
        if (claimed_location) {
            release_actor_location (*_state, committed);
        }
        _state->actor_transfer_coordinator.fail_commit (transfer_id, reconcile);
    };

    std::shared_ptr<void> actor;
    try {
        if (transfer == _state->actor_transfers.end ()) {
            actor = factory.value ().get ().create_instance (std::string (actor_ref.actor_id ()));
        } else {
            auto materialized = transfer->second
                                  .transfer_in (std::string (actor_ref.actor_id ()),
                                                message_t::from_raw (
                                                  std::move (transfer_state),
                                                  context->_state->channel_runtime->serializers))
                                  .result ();
            if (!materialized) {
                fail_target_commit (false);
                return result_t<actor_join_reply_t>::failure (
                  materialized.error_kind (), materialized.error () ? materialized.error ()->what ()
                                                                    : "actor transfer-in failed");
            }
            actor = std::move (materialized.value ());
        }
    }
    catch (const framework_exception_t &error) {
        fail_target_commit (false);
        return detail::result_access_t::failure<actor_join_reply_t> (error);
    }
    catch (const std::exception &error) {
        fail_target_commit (false);
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::request_failed,
                                                      error.what ());
    }
    catch (...) {
        fail_target_commit (false);
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::request_failed,
                                                      "actor transfer-in failed");
    }
    if (!actor) {
        fail_target_commit (false);
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::actor_route_not_found,
                                                      "actor materialization returned no actor");
    }

    try {
        auto committed_context = actor_context_t (actor_context._state, committed);
        factory.value ().get ().configure_instance (actor.get (), committed, &committed_context);
    }
    catch (const std::exception &error) {
        fail_target_commit (false);
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::request_failed,
                                                      error.what ());
    }
    catch (...) {
        fail_target_commit (false);
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::request_failed,
                                                      "target actor configuration failed");
    }
    const auto admission =
      context->_state->actor_admissions.find (factory.value ().get ().actor_type);
    if (admission == context->_state->actor_admissions.end ()) {
        fail_target_commit (false);
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::handler_not_found,
          "target spot actor lifecycle is not registered");
    }

    auto &target = *context->_state;
    // The joined callback is user code on the spot serial queue. It may call back
    // into the framework (sends, joins) that need the node mutex from the serial
    // thread, so the node mutex must not be held across this wait.
    node_lock.unlock ();
    try {
        if (admission->second.on_actor_joined
            && !target.run_serial_sync ("spot-actor-transfer-joined", [&] {
                   const auto updated =
                     actor_gateway_runtime_t (actor_context._state).update_actor_ref (committed);
                   if (!updated) {
                       throw framework_exception_t (
                         updated.error_kind (),
                         updated.error () ? updated.error ()->what ()
                                          : "target actor gateway ref update failed");
                   }
                   admission->second.on_actor_joined (target.spot_instance.get (), actor.get ());
               })) {
            node_lock.lock ();
            fail_target_commit (false);
            return result_t<actor_join_reply_t>::failure (framework_error_kind_t::request_rejected,
                                                          "spot serial queue is full");
        }
        node_lock.lock ();
    }
    catch (const framework_exception_t &error) {
        node_lock.lock ();
        detail::record_actor_instance_index_unlocked (*_state, committed, actor.get ());
        _state->actor_instances[actor_key (committed)] = actor;
        fail_target_commit (true);
        return detail::result_access_t::failure<actor_join_reply_t> (error);
    }
    catch (const std::exception &error) {
        node_lock.lock ();
        detail::record_actor_instance_index_unlocked (*_state, committed, actor.get ());
        _state->actor_instances[actor_key (committed)] = actor;
        fail_target_commit (true);
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::request_failed,
                                                      error.what ());
    }
    catch (...) {
        node_lock.lock ();
        detail::record_actor_instance_index_unlocked (*_state, committed, actor.get ());
        _state->actor_instances[actor_key (committed)] = actor;
        fail_target_commit (true);
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::request_failed,
                                                      "target joined callback failed");
    }

    const auto key = actor_key (committed);
    detail::record_actor_instance_index_unlocked (*_state, committed, actor.get ());
    _state->actor_instances[key] = std::move (actor);
    _state->destroyed_actor_keys.erase (key);
    if (auto native = _state->native_node.lock ();
        native && !_state->native_actors.contains (key)) {
        _state->native_actors.emplace (
          key, std::make_unique<zlink::service::actor_t> (
                 native->create_actor (std::string (committed.actor_id ()))));
    }
    detail::record_actor_context_route_unlocked (*_state, key,
                                                 detail::effective_spot_node_rid (_state->snapshot),
                                                 target, committed.generation ());
    return result_t<actor_join_reply_t>::success (
      actor_join_reply_t{0, committed, zlink::message_t{}});
}

result_t<actor_join_reply_t>
spot_node_runtime_t::commit_remote_actor_to_spot (
  std::string transfer_id,
  const actor_ref_t &actor_ref,
  spot_rid_t target_spot_rid,
  zlink::message_t transfer_state,
  actor_context_t actor_context,
  std::vector<handoff_packet_t> handoff_backlog,
  service_provider_t *services)
{
    auto prepared = prepare_remote_actor_to_spot (
      transfer_id, actor_ref, target_spot_rid, std::move (transfer_state),
      std::move (actor_context));
    if (!prepared) {
        return prepared;
    }
    if (!handoff_backlog.empty () && services == nullptr) {
        _state->actor_transfer_coordinator.fail_commit (transfer_id, true);
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "remote actor handoff backlog requires a service provider");
    }
    if (services != nullptr) {
        return finalize_remote_actor_to_spot (
          std::move (transfer_id), actor_ref, std::move (target_spot_rid),
          std::move (handoff_backlog), *services);
    }
    service_collection_t empty_services;
    auto empty_provider = empty_services.build_provider ();
    return finalize_remote_actor_to_spot (
      std::move (transfer_id), actor_ref, std::move (target_spot_rid), {}, empty_provider);
}

result_t<actor_join_reply_t>
spot_node_runtime_t::finalize_remote_actor_to_spot (
  std::string transfer_id,
  const actor_ref_t &actor_ref,
  spot_rid_t target_spot_rid,
  std::vector<handoff_packet_t> handoff_backlog,
  service_provider_t &services)
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto pending = _state->actor_transfer_coordinator.pending_commit (
      transfer_id, actor_ref, target_spot_rid);
    if (!pending) {
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::request_protocol_error,
          "remote actor finalize has no matching prepared commit");
    }
    const auto committed =
      actor_ref_t (node_rid_t::from_string (detail::effective_spot_node_rid (_state->snapshot)),
                   std::string (actor_ref.actor_type ()), std::string (actor_ref.actor_id ()),
                   actor_ref.generation () + 1);
    const auto key = actor_key (committed);
    auto context = find_context (target_spot_rid);
    auto factory = actor_factory_unlocked (committed);
    const auto actor = _state->actor_instances.find (key);
    if (!context || !context->_state->spot_instance || !factory
        || actor == _state->actor_instances.end () || !actor->second) {
        _state->actor_transfer_coordinator.fail_commit (transfer_id, true);
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::actor_route_not_found,
          "prepared remote actor commit dependencies are unavailable");
    }

    auto &target = *context->_state;
    auto &target_serializers = *target.channel_runtime->serializers;
    for (auto &packet : handoff_backlog) {
        emit_actor_transfer_marker ("backlog_enqueued", committed, transfer_id,
                                    target_spot_rid);
        const auto message = zlink::message_t::from (packet.payload);
        spot_actor_message_metadata_t metadata;
        metadata.content_type = std::move (packet.content_type);
        metadata.values = std::move (packet.metadata);
        std::string replay_request_id;
        if (packet.is_request) {
            const auto id_it = metadata.values.find ("__zlink.actorRequestId");
            if (id_it != metadata.values.end () && !id_it->second.empty ()) {
                replay_request_id = id_it->second;
                const std::lock_guard<std::mutex> dedup_lock (
                  _state->dispatched_request_replies_mutex);
                if (!_state->dispatched_request_replies[key]
                       .emplace (replay_request_id, std::nullopt)
                       .second) {
                    continue;
                }
            }
        }
        const auto handler_kind = packet.is_request ? spot_handler_kind_t::actor_request
                                                    : spot_handler_kind_t::actor_send;
        auto task = spot_handler_registry_t (context->_state)
                      .invoke_erased (handler_kind, packet.packet_name, {},
                                      factory.value ().get ().actor_type,
                                      target.spot_instance.get (), actor->second.get (), services,
                                      target_serializers, message, std::move (metadata));
        auto task_holder = std::make_shared<task_t<zlink::message_t>> (std::move (task));
        detail::observe_task_completion (
          *task_holder, [task_holder, node_state = _state, key, replay_request_id] (
                          const result_t<zlink::message_t> &completed) {
              if (replay_request_id.empty ()) {
                  return;
              }
              const std::lock_guard<std::mutex> lock (
                node_state->dispatched_request_replies_mutex);
              auto replies = node_state->dispatched_request_replies.find (key);
              if (replies == node_state->dispatched_request_replies.end ()) {
                  return;
              }
              const auto entry = replies->second.find (replay_request_id);
              if (entry == replies->second.end ()) {
                  return;
              }
              if (completed) {
                  entry->second = completed.value ();
              } else {
                  replies->second.erase (entry);
              }
          });
    }

    const auto location_updated =
      update_actor_location_after_move (*_state, committed, target, false);
    if (!location_updated) {
        _state->actor_transfer_coordinator.fail_commit (transfer_id, true);
        return result_t<actor_join_reply_t>::failure (location_updated.error_kind (),
                                                      location_updated.error ()
                                                        ? location_updated.error ()->what ()
                                                        : "actor committed location update failed");
    }
    emit_actor_transfer_marker ("location_committed", committed, transfer_id,
                                target_spot_rid);
    if (_state->update_actor_registry_ref) {
        const auto updated = _state->update_actor_registry_ref (committed);
        if (!updated) {
            _state->actor_transfer_coordinator.fail_commit (transfer_id, true);
            return detail::propagate_failure<actor_join_reply_t> (updated, "actor ref update failed");
        }
    }
    _state->actor_transfer_coordinator.complete_commit (transfer_id);
    return result_t<actor_join_reply_t>::success (
      actor_join_reply_t{0, committed, zlink::message_t{}});
}

result_t<actor_join_reply_t> spot_node_runtime_t::join_actor_to_entry_spot_erased (
  const actor_ref_t &actor_ref,
  node_rid_t spot_node_rid,
  const zlink::message_t &request,
  const std::optional<zlink::message_t> &actor_snapshot)
{
    /* graceful-drain-handoff §4-2/§5.2: a draining node rejects new actor
    * admission and joins; already-admitted transfer commits stay accepted. */
    if (_state->drain_flag && _state->drain_flag->load (std::memory_order_acquire)) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::request_rejected,
                                                      "spot node is draining and rejects new actor joins");
    }
    if (spot_node_rid.empty ()
        || spot_node_rid.value () != detail::effective_spot_node_rid (_state->snapshot)) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::spot_route_not_found,
                                                      "spot node rid does not match this node");
    }
    if (!_state->snapshot.entry_spot_name) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::spot_route_not_found,
                                                      "entry spot is not registered");
    }
    const auto entry_rid = _state->spot_rids_by_name.find (*_state->snapshot.entry_spot_name);
    if (entry_rid == _state->spot_rids_by_name.end ()) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::spot_route_not_found,
                                                      "entry spot is not created");
    }
    return join_actor_to_spot_erased (actor_ref, entry_rid->second, request, actor_snapshot);
}

void spot_node_runtime_t::on_destroy_actor (
  std::function<result_t<void> (const actor_ref_t &)> destroy_actor)
{
    _state->destroy_actor_registry = std::move (destroy_actor);
}

void spot_node_runtime_t::on_actor_ref_updated (
  std::function<result_t<void> (const actor_ref_t &)> update_actor)
{
    _state->update_actor_registry_ref = std::move (update_actor);
}

void spot_node_runtime_t::on_actor_entry_spot_join (
  std::function<result_t<actor_join_reply_t> (const actor_ref_t &,
                                              node_rid_t,
                                              const zlink::message_t &,
                                              const std::optional<zlink::message_t> &)> join)
{
    _state->actor_entry_spot_join = std::move (join);
}

void spot_node_runtime_t::on_actor_packet_relay (
  std::function<result_t<std::optional<zlink::message_t>> (const actor_ref_t &,
                                                           actor_context_t,
                                                           stream_message_kind_t,
                                                           std::string_view,
                                                           const zlink::message_t &,
                                                           service_provider_t &,
                                                           serializer_registry_t &,
                                                           spot_actor_message_metadata_t)> relay)
{
    _state->actor_packet_relay = std::move (relay);
}

result_t<std::optional<zlink::message_t>>
spot_node_runtime_t::relay_actor_packet (const actor_ref_t &actor_ref,
                                         actor_context_t actor_context,
                                         std::string_view packet_name,
                                         const zlink::message_t &message,
                                         service_provider_t &services,
                                         serializer_registry_t &serializers,
                                         spot_actor_message_metadata_t metadata)
{
    return relay_actor_packet (actor_ref, std::move (actor_context), stream_message_kind_t::request,
                               packet_name, message, services, serializers, std::move (metadata));
}

result_t<std::optional<zlink::message_t>>
spot_node_runtime_t::relay_actor_packet (const actor_ref_t &actor_ref,
                                         actor_context_t actor_context,
                                         stream_message_kind_t message_kind,
                                         std::string_view packet_name,
                                         const zlink::message_t &message,
                                         service_provider_t &services,
                                         serializer_registry_t &serializers,
                                         spot_actor_message_metadata_t metadata)
{
    if (actor_ref.empty ()) {
        return result_t<std::optional<zlink::message_t>>::failure (
          framework_error_kind_t::actor_route_not_found, "actor ref is empty");
    }

    const auto key = actor_key (actor_ref);
    const auto handoff = metadata.values.find ("__zlink.actorHandoffBacklog");
    const auto handoff_transfer_id = metadata.values.find ("__zlink.actorTransferId");
    if (handoff != metadata.values.end () && handoff->second == "true"
        && handoff_transfer_id != metadata.values.end ()
        && !handoff_transfer_id->second.empty ()) {
        emit_actor_transfer_marker ("backlog_enqueued", actor_ref,
                                    handoff_transfer_id->second,
                                    actor_spot (actor_ref));
    }
    if (_state->actor_transfer_coordinator.blocks_dispatch (key)) {
        // In-flight handoff (§10.2-1): actor packets that arrive while the actor
        // is moving are preserved in arrival order and travel to the target with
        // the commit — never dropped. Sends return the empty success shape so
        // preservation is indistinguishable from immediate dispatch. A request's
        // reply channel cannot move with the actor, so it additionally fails fast
        // as retriable and the caller re-resolves (§10.2-5) or times out; the
        // preserved copy still reaches the committed target's handler (§10.5 late
        // reply) with a best-effort reply.
        const bool is_request = message_kind == stream_message_kind_t::request;
        if (_state->actor_transfer_coordinator.try_append_backlog (
              key, detail::handoff_packet_t{std::string (packet_name), message.to_bytes (),
                                            metadata.content_type, metadata.values, is_request})) {
            emit_actor_transfer_marker (
              "handoff_backlog", actor_ref,
              _state->actor_transfer_coordinator.transfer_id (key).value_or (key));
            if (!is_request) {
                return result_t<std::optional<zlink::message_t>>::success (zlink::message_t{});
            }
        }
        return result_t<std::optional<zlink::message_t>>::failure (
          framework_error_kind_t::actor_location_stale, "actor transfer is in progress", true);
    }

    const auto actor_type_key = std::string (actor_ref.actor_type ());
    const auto found_factory = _state->actor_factories.find (actor_type_key);
    if (found_factory == _state->actor_factories.end ()) {
        return result_t<std::optional<zlink::message_t>>::failure (
          framework_error_kind_t::actor_route_not_found, "actor factory is not registered");
    }

    auto &mailbox_slot = _state->actor_mailboxes[key];
    if (!mailbox_slot) {
        mailbox_slot = std::make_shared<std::mutex> ();
    }
    auto actor_mailbox = mailbox_slot;
    auto found_location = _state->actor_spot_rids.find (key);
    if (found_location == _state->actor_spot_rids.end ()
        && _state->destroyed_actor_keys.contains (key)) {
        return result_t<std::optional<zlink::message_t>>::failure (
          framework_error_kind_t::actor_route_not_found, "actor has been destroyed");
    }
    /* Same double-checked registration as the join paths. */
    std::shared_ptr<void> actor_instance = registered_actor_instance (actor_ref, key);
    if (!actor_instance) {
        auto created_instance =
          found_factory->second.create_instance (std::string (actor_ref.actor_id ()));
        if (!created_instance) {
            return result_t<std::optional<zlink::message_t>>::failure (
              framework_error_kind_t::actor_route_not_found, "actor factory returned null");
        }
        actor_instance = install_actor_instance (actor_ref, key, std::move (created_instance), true);
        if (!actor_instance) {
            return result_t<std::optional<zlink::message_t>>::failure (
              framework_error_kind_t::actor_route_not_found, "actor has been destroyed");
        }
    }

    if (found_location == _state->actor_spot_rids.end ()) {
        if (!_state->snapshot.entry_spot_name) {
            return result_t<std::optional<zlink::message_t>>::failure (
              framework_error_kind_t::spot_route_not_found, "entry spot is not registered");
        }
        const auto entry_rid = _state->spot_rids_by_name.find (*_state->snapshot.entry_spot_name);
        if (entry_rid == _state->spot_rids_by_name.end ()) {
            return result_t<std::optional<zlink::message_t>>::failure (
              framework_error_kind_t::spot_route_not_found, "entry spot is not created");
        }
        auto entry_context = find_context (entry_rid->second);
        if (!entry_context || !entry_context->_state->spot_instance) {
            return result_t<std::optional<zlink::message_t>>::failure (
              framework_error_kind_t::spot_route_not_found, "entry spot context is not registered");
        }
        auto &entry_state = *entry_context->_state;
        detail::record_actor_context_route_unlocked (
          *_state, key, detail::effective_spot_node_rid (_state->snapshot), entry_state,
          actor_ref.generation ());
        if (const auto admission =
              entry_state.actor_admissions.find (found_factory->second.actor_type);
            admission != entry_state.actor_admissions.end ()) {
            if (admission->second.on_create_actor && _state->actor_created_keys.insert (key).second) {
                const auto create_request =
                  actor_context.create_payload ().value_or (zlink::message_t{});
                admission->second.on_create_actor (entry_state.spot_instance.get (),
                                                 actor_instance.get (), create_request,
                                                 serializers);
            }
            if (admission->second.on_actor_joined) {
                admission->second.on_actor_joined (entry_state.spot_instance.get (),
                                                   actor_instance.get ());
            }
        }
        found_location = _state->actor_spot_rids.find (key);
    }

    const auto found_generation = _state->actor_generations.find (key);
    if (found_generation != _state->actor_generations.end ()
        && found_generation->second != actor_ref.generation ()) {
        // The dispatched ref's generation does not match the actor's current
        // incarnation (§10.4-3). Retriable: for a still-committing local move the
        // published record lags and re-resolving lands the committed generation
        // (ST-A3); for a genuinely stale record the client re-resolves the same
        // answer and eventually surfaces this stale on its own budget timeout.
        emit_actor_transfer_marker (
          "stale_fail_fast", actor_ref,
          std::string (actor_ref.actor_type ()) + ":" + std::string (actor_ref.actor_id ()),
          found_location != _state->actor_spot_rids.end ()
            ? std::make_optional (found_location->second)
            : std::nullopt);
        return result_t<std::optional<zlink::message_t>>::failure (
          framework_error_kind_t::actor_location_stale,
          "actor generation is stale. actor=" + std::string (actor_ref.actor_id ())
            + ", current=" + std::to_string (found_generation->second)
            + ", received=" + std::to_string (actor_ref.generation ()),
          true);
    }
    const auto current_actor_node_rid = actor_ref.node_rid ().empty ()
                                          ? detail::effective_spot_node_rid (_state->snapshot)
                                          : std::string (actor_ref.node_rid ().value ());
    const auto current_actor_ref =
      actor_ref_t (node_rid_t::from_string (current_actor_node_rid),
                   std::string (actor_ref.actor_type ()), std::string (actor_ref.actor_id ()),
                   found_generation != _state->actor_generations.end () ? found_generation->second
                                                                        : actor_ref.generation ());
    auto current_actor_context = actor_context_t (actor_context._state, current_actor_ref);
    found_factory->second.configure_instance (actor_instance.get (), current_actor_ref,
                                              &current_actor_context);

    auto context = find_context (found_location->second);
    if (!context || !context->_state->spot_instance) {
        return result_t<std::optional<zlink::message_t>>::failure (
          framework_error_kind_t::spot_route_not_found,
          "actor spot context is not registered. node=" + _state->snapshot.name
            + ", actor=" + std::string (actor_ref.actor_id ())
            + ", spot=" + std::string (found_location->second.value ()));
    }
    bool dispatch_on_spot_serial = true;
    if (_state->snapshot.entry_spot_name) {
        const auto entry_rid = _state->spot_rids_by_name.find (*_state->snapshot.entry_spot_name);
        dispatch_on_spot_serial = entry_rid == _state->spot_rids_by_name.end ()
                                  || entry_rid->second.value () != found_location->second.value ();
    }

    const auto handler_kind = message_kind == stream_message_kind_t::send
                                ? spot_handler_kind_t::actor_send
                                : spot_handler_kind_t::actor_request;
    const auto dispatch_kind = message_kind == stream_message_kind_t::send
                                 ? dispatch_message_kind_t::actor_send
                                 : dispatch_message_kind_t::actor_request;

    // §10.2-1 exactly-once: a request preserved during the move and also retried
    // by the sender (or replayed by the commit) carries a stable id. The first
    // arrival dispatches; a repeat returns the cached reply (or fails retriable
    // while that first dispatch is still in flight so the sender re-polls).
    std::string dedup_request_id;
    if (message_kind == stream_message_kind_t::request) {
        const auto id_it = metadata.values.find ("__zlink.actorRequestId");
        if (id_it != metadata.values.end () && !id_it->second.empty ()) {
            dedup_request_id = id_it->second;
            const std::lock_guard<std::mutex> dedup_lock (
              _state->dispatched_request_replies_mutex);
            auto &replies = _state->dispatched_request_replies[key];
            const auto existing = replies.find (dedup_request_id);
            if (existing != replies.end ()) {
                if (existing->second) {
                    return result_t<std::optional<zlink::message_t>>::success (
                      *existing->second);
                }
                return result_t<std::optional<zlink::message_t>>::failure (
                  framework_error_kind_t::actor_location_stale,
                  "actor request dispatch is in flight", true);
            }
            replies.emplace (dedup_request_id, std::nullopt);
        }
    }
    // In-flight request window for the transfer pending sample (runtime-metrics
    // §4.3): counted from dispatch start until the reply (or error) is produced,
    // so a moving transition that lands mid-dispatch sees this request.
    struct pending_request_scope_t
    {
        std::shared_ptr<detail::spot_node_builder_state_t> state;
        std::string key;

        pending_request_scope_t (std::shared_ptr<detail::spot_node_builder_state_t> state_,
                                 std::string key_) :
            state (std::move (state_)), key (std::move (key_))
        {
        }
        pending_request_scope_t (const pending_request_scope_t &) = delete;
        pending_request_scope_t &operator= (const pending_request_scope_t &) = delete;
        ~pending_request_scope_t ()
        {
            const std::lock_guard<std::mutex> lock (state->actor_pending_requests_mutex);
            const auto found = state->actor_pending_requests.find (key);
            if (found != state->actor_pending_requests.end () && --found->second == 0) {
                state->actor_pending_requests.erase (found);
            }
        }
    };
    std::optional<pending_request_scope_t> pending_request_scope;
    if (message_kind == stream_message_kind_t::request) {
        {
            const std::lock_guard<std::mutex> lock (_state->actor_pending_requests_mutex);
            _state->actor_pending_requests[key]++;
        }
        pending_request_scope.emplace (_state, key);
    }
    report_spot_dispatch_trace (_state, message_flow_outcome_t::received,
                                dispatch_error_surface_t::spot_actor, dispatch_kind, packet_name,
                                {}, found_location->second.value (), actor_ref.actor_id ());
    std::unique_lock actor_mailbox_lock (*actor_mailbox);
    auto reply =
      spot_handler_registry_t (context->_state)
        .invoke_erased (handler_kind, packet_name, {}, found_factory->second.actor_type,
                        context->_state->spot_instance.get (), actor_instance.get (), services,
                        serializers, message, std::move (metadata), dispatch_on_spot_serial)
        .result ();
    if (!reply) {
        if (!dedup_request_id.empty ()) {
            const std::lock_guard<std::mutex> dedup_lock (
              _state->dispatched_request_replies_mutex);
            _state->dispatched_request_replies[key].erase (dedup_request_id);
        }
        const auto *error = reply.error ();
        const framework_exception_t exception (
          reply.error_kind (), error != nullptr ? error->what () : "actor packet relay failed",
          error != nullptr && error->is_retriable ());
        report_spot_dispatch_error (
          _state, dispatch_error_surface_t::spot_actor, dispatch_kind,
          dispatch_reason_from_error (exception.kind ()), dispatch_error_action_t::reply_error,
          std::string (packet_name), std::nullopt, std::string (found_location->second.value ()),
          std::string (actor_ref.actor_id ()), std::make_exception_ptr (exception));
        return detail::result_access_t::failure<std::optional<zlink::message_t>> (exception);
    }
    report_spot_dispatch_trace (_state, message_flow_outcome_t::replied,
                                dispatch_error_surface_t::spot_actor, dispatch_kind, packet_name,
                                {}, found_location->second.value (), actor_ref.actor_id ());
    if (!dedup_request_id.empty ()) {
        const std::lock_guard<std::mutex> dedup_lock (_state->dispatched_request_replies_mutex);
        _state->dispatched_request_replies[key][dedup_request_id] = reply.value ();
    }
    return result_t<std::optional<zlink::message_t>>::success (std::move (reply.value ()));
}

result_t<void>
spot_node_runtime_t::notify_actor_disconnected_erased (const actor_ref_t &actor_ref) const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    if (actor_ref.empty ()) {
        return result_t<void>::failure (framework_error_kind_t::actor_route_not_found,
                                        "actor ref is empty");
    }

    const auto key = actor_key (actor_ref);
    const auto found_generation = _state->actor_generations.find (key);
    if (found_generation != _state->actor_generations.end ()
        && found_generation->second != actor_ref.generation ()) {
        return detail::boundary_failure<void> (detail::boundary_error_t::stale_generation,
                                        "actor generation is stale");
    }

    const auto found_location = _state->actor_spot_rids.find (key);
    if (found_location == _state->actor_spot_rids.end ()) {
        return result_t<void>::success ();
    }
    auto context = find_context (found_location->second);
    if (!context || !context->_state->spot_instance) {
        return result_t<void>::success ();
    }

    const auto actor_factory = actor_factory_unlocked (actor_ref);
    if (!actor_factory) {
        return result_t<void>::failure (actor_factory.error_kind (),
                                        actor_factory.error () ? actor_factory.error ()->what ()
                                                               : "actor factory failed");
    }
    const auto actor = _state->actor_instances.find (key);
    if (actor == _state->actor_instances.end () || !actor->second) {
        return result_t<void>::success ();
    }

    const auto admission =
      context->_state->actor_admissions.find (actor_factory.value ().get ().actor_type);
    if (admission == context->_state->actor_admissions.end ()
        || !admission->second.on_disconnect_actor) {
        return result_t<void>::success ();
    }

    try {
        if (!context->_state->run_serial_sync ("spot-lifecycle-disconnect", [&] {
                admission->second.on_disconnect_actor (context->_state->spot_instance.get (),
                                                     actor->second.get ());
            })) {
            return result_t<void>::failure (framework_error_kind_t::request_rejected,
                                            "spot serial queue is full");
        }
        return result_t<void>::success ();
    }
    catch (const framework_exception_t &error) {
        return detail::result_access_t::failure<void> (error);
    }
    catch (const std::exception &error) {
        return result_t<void>::failure (framework_error_kind_t::request_failed, error.what ());
    }
    catch (...) {
        return result_t<void>::failure (framework_error_kind_t::request_failed,
                                        "spot actor disconnected callback failed");
    }
}

spot_node_runtime_t spot_node_runtime_t::from (const spot_node_builder_t &builder)
{
    return spot_node_runtime_t (builder._state);
}

std::optional<spot_node_runtime_t> spot_node_runtime_t::from (const zlink_builder_t &builder,
                                                              const std::string &spot_node_name)
{
    const auto found = builder._state->spot_nodes.find (spot_node_name);
    if (found == builder._state->spot_nodes.end ()) {
        return std::nullopt;
    }
    return spot_node_runtime_t (found->second);
}

spot_create_result_t spot_node_runtime_t::create_spot (std::string spot_name)
{
    return create_spot (std::move (spot_name), zlink::message_t{});
}

spot_create_result_t spot_node_runtime_t::create_spot_context_unlocked (
  std::string spot_name,
  spot_rid_t spot_rid,
  zlink::message_t request,
  std::unique_lock<std::recursive_mutex> &node_lock)
{
    /* graceful-drain-handoff §4-2: a draining node blocks new spot creation.
     * Existing spots (and in-progress transfer commits) keep running. */
    if (_state->drain_flag && _state->drain_flag->load (std::memory_order_acquire)) {
        throw framework_exception_t (framework_error_kind_t::request_rejected,
                                     "spot node is draining and rejects new spot creation");
    }
    const auto found = _state->spot_factories.find (spot_name);
    if (found == _state->spot_factories.end ()) {
        throw framework_exception_t (framework_error_kind_t::spot_create_failed,
                                     "spot factory is not registered");
    }
    const auto lifecycle =
      _state->spot_lifecycles.find (spot_name) != _state->spot_lifecycles.end ()
        ? _state->spot_lifecycles.at (spot_name)
        : spot_lifecycle_callbacks_t{};

    auto context_state = std::make_shared<spot_context_state_t> ();
    context_state->node = _state;
    context_state->channel_runtime = _state->channel_runtime;
    context_state->node_rid =
      node_rid_t::from_string (detail::effective_spot_node_rid (_state->snapshot));
    context_state->spot_rid = spot_rid;
    context_state->spot_name = spot_name;
    context_state->lifecycle = lifecycle;
    configure_spot_execution (context_state);
    spot_context_t context (context_state);
    entry_spot_context_t entry_context (context_state);
    std::optional<message_t> create_reply;
    const auto rid_value = std::string (spot_rid.value ());

    if (lifecycle.create_instance) {
        context_state->spot_instance = lifecycle.create_instance ();
        if (!context_state->spot_instance) {
            throw framework_exception_t (framework_error_kind_t::spot_create_failed,
                                         "SPOT factory returned null");
        }
        if (lifecycle.configure_entry) {
            lifecycle.configure_entry (context_state->spot_instance.get (), entry_context);
        } else if (lifecycle.configure) {
            lifecycle.configure (context_state->spot_instance.get (), context);
        }
    }

    attach_native_spot_locked (context_state);
    auto remove_activation = [&] {
        auto native = context_state->native_spot.lock ();
        context_state->native_spot.reset ();
        _state->native_spots_by_rid.erase (rid_value);
        if (native) {
            node_lock.unlock ();
            try {
                native->close ();
            }
            catch (...) {
            }
            node_lock.lock ();
        }
    };

    if (lifecycle.create_instance) {
        auto &serializers = *context_state->channel_runtime->serializers;
        spot_create_response_t response;
        try {
            node_lock.unlock ();
            response =
              lifecycle.on_create
                ? lifecycle.on_create (context_state->spot_instance.get (), request, serializers)
                    .result ()
                    .value ()
                : spot_create_response_t::accept ();
            node_lock.lock ();
        }
        catch (...) {
            if (!node_lock.owns_lock ()) {
                node_lock.lock ();
            }
            remove_activation ();
            throw;
        }
        if (!response.accepted) {
            remove_activation ();
            return spot_create_result_t{spot_rid, spot_create_state_t::rejected, response.reply,
                                        context};
        }
        create_reply = response.reply;
        if (lifecycle.on_initialize) {
            try {
                node_lock.unlock ();
                lifecycle.on_initialize (context_state->spot_instance.get ());
                node_lock.lock ();
            }
            catch (...) {
                if (!node_lock.owns_lock ()) {
                    node_lock.lock ();
                }
                remove_activation ();
                throw;
            }
        }
    }

    _state->spot_rids_by_name[spot_name] = spot_rid;
    _state->spot_names_by_rid[rid_value] = spot_name;
    _state->spot_contexts_by_rid[rid_value] = context;
    auto remove_ready_activation = [&] {
        _state->spot_rids_by_name.erase (spot_name);
        _state->spot_names_by_rid.erase (rid_value);
        _state->spot_contexts_by_rid.erase (rid_value);
        remove_activation ();
    };

    if (_state->location_lifecycle) {
        const auto claimed = _state->location_lifecycle->claim_spot (
          make_spot_location (*_state, spot_name, spot_rid));
        if (claimed.status != location_write_status_t::stored) {
            remove_ready_activation ();
            return spot_create_result_t{spot_rid, spot_create_state_t::rejected, std::nullopt,
                                        context};
        }
    }

    if (_state->monitoring) {
        monitoring_runtime_t (_state->monitoring)
          .publish_spot_snapshot (spot_event_payload_t{runtime_event_base_t{_state->snapshot.name},
                                                       spot_event_kind_t::subjects_changed,
                                                       _state->snapshot.name,
                                                       {},
                                                       {spot_name},
                                                       std::nullopt});
        runtime::runtime_metrics_t metrics (_state->monitoring);
        if (metrics.enabled ()) {
            const auto kind = _state->snapshot.entry_spot_name
                                  && *_state->snapshot.entry_spot_name == spot_name
                                ? "entry"
                                : "user";
            metrics.counter ("zlink.spot.created", "{spot}", 1, {{"kind", kind}});
            metrics.updown ("zlink.spot.count", "{spot}", 1, {{"kind", kind}});
        }
    }
    return spot_create_result_t{spot_rid, spot_create_state_t::created, create_reply, context};
}

spot_create_result_t spot_node_runtime_t::create_spot (std::string spot_name,
                                                       zlink::message_t request)
{
    std::unique_lock<std::recursive_mutex> node_lock (_state->mutex);
    const auto is_entry_spot =
      _state->snapshot.entry_spot_name && *_state->snapshot.entry_spot_name == spot_name;
    auto rid = spot_rid_t::from_string (
      is_entry_spot ? detail::effective_spot_node_rid (_state->snapshot)
                    : detail::effective_spot_node_rid (_state->snapshot) + ":" + spot_name + ":"
                        + std::to_string (_state->next_spot_id++));
    return create_spot_context_unlocked (std::move (spot_name), std::move (rid),
                                         std::move (request), node_lock);
}

spot_create_result_t spot_node_runtime_t::get_or_create_spot (std::string spot_name,
                                                              spot_rid_t spot_rid)
{
    return get_or_create_spot (std::move (spot_name), std::move (spot_rid), zlink::message_t{});
}

spot_create_result_t spot_node_runtime_t::get_or_create_spot (std::string spot_name,
                                                              spot_rid_t spot_rid,
                                                              zlink::message_t request)
{
    std::unique_lock<std::recursive_mutex> node_lock (_state->mutex);
    const auto rid_value = std::string (spot_rid.value ());
    auto same_spot_type = [&] (const std::string &existing_name) {
        const auto existing_factory = _state->spot_factories.find (existing_name);
        const auto requested_factory = _state->spot_factories.find (spot_name);
        return existing_factory == _state->spot_factories.end ()
               || requested_factory == _state->spot_factories.end ()
               || existing_factory->second == requested_factory->second;
    };
    if (const auto existing = _state->spot_contexts_by_rid.find (rid_value);
        existing != _state->spot_contexts_by_rid.end ()) {
        const auto existing_name = _state->spot_names_by_rid.find (rid_value);
        if (existing_name != _state->spot_names_by_rid.end ()
            && !same_spot_type (existing_name->second)) {
            throw framework_exception_t (framework_error_kind_t::spot_type_mismatch,
                                         "spot rid is already bound to a different spot type");
        }
        return spot_create_result_t{spot_rid, spot_create_state_t::existing, std::nullopt,
                                    existing->second};
    }
    if (const auto pending = _state->pending_spot_creations_by_rid.find (rid_value);
        pending != _state->pending_spot_creations_by_rid.end ()) {
        if (!same_spot_type (pending->second.spot_name)) {
            throw framework_exception_t (framework_error_kind_t::spot_type_mismatch,
                                         "spot rid is already bound to a different spot type");
        }
        auto future = pending->second.future;
        node_lock.unlock ();
        auto result = future.get ();
        if (result.state == spot_create_state_t::created) {
            return spot_create_result_t{result.spot_rid, spot_create_state_t::existing,
                                        std::nullopt, result.context};
        }
        return result;
    }

    auto promise = std::make_shared<std::promise<spot_create_result_t>> ();
    _state->pending_spot_creations_by_rid.emplace (
      rid_value, detail::spot_node_builder_state_t::pending_spot_creation_t{
                   spot_name, promise->get_future ().share ()});
    try {
        auto result = create_spot_context_unlocked (std::move (spot_name), std::move (spot_rid),
                                                    std::move (request), node_lock);
        _state->pending_spot_creations_by_rid.erase (rid_value);
        promise->set_value (result);
        return result;
    }
    catch (...) {
        if (!node_lock.owns_lock ()) {
            node_lock.lock ();
        }
        _state->pending_spot_creations_by_rid.erase (rid_value);
        promise->set_exception (std::current_exception ());
        throw;
    }
}

std::optional<spot_info_t> spot_node_runtime_t::find_spot (spot_rid_t spot_rid) const
{
    if (const auto name = spot_name_for (spot_rid)) {
        return spot_info_t{std::move (spot_rid), *name};
    }
    return std::nullopt;
}

std::vector<spot_info_t> spot_node_runtime_t::list_spots () const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    std::vector<spot_info_t> spots;
    spots.reserve (_state->spot_names_by_rid.size ());
    for (const auto &[rid, name] : _state->spot_names_by_rid) {
        spots.push_back (spot_info_t{spot_rid_t::from_string (rid), name});
    }
    return spots;
}

task_t<bool> spot_node_runtime_t::close_spot (spot_rid_t spot_rid)
{
    std::optional<spot_context_t> context;
    {
        std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
        const auto found = _state->spot_contexts_by_rid.find (std::string (spot_rid.value ()));
        if (found == _state->spot_contexts_by_rid.end ()) {
            co_return result_t<bool>::success (false);
        }
        context = found->second;
    }
    const bool closed = context->close ().result ().value ();
    if (closed && _state->monitoring) {
        runtime::runtime_metrics_t metrics (_state->monitoring);
        if (metrics.enabled ()) {
            const auto spot_name = spot_name_for (spot_rid);
            const auto kind = _state->snapshot.entry_spot_name && spot_name
                                  && *_state->snapshot.entry_spot_name == *spot_name
                                ? "entry"
                                : "user";
            metrics.counter ("zlink.spot.closed", "{spot}", 1, {{"kind", kind}});
            metrics.updown ("zlink.spot.count", "{spot}", -1, {{"kind", kind}});
        }
    }
    if (closed && _state->monitoring) {
        monitoring_runtime_t (_state->monitoring)
          .publish_spot_snapshot (spot_event_payload_t{runtime_event_base_t{_state->snapshot.name},
                                                       spot_event_kind_t::subjects_changed,
                                                       _state->snapshot.name,
                                                       {},
                                                       {},
                                                       std::nullopt});
    }
    co_return result_t<bool>::success (closed);
}

node_rid_t spot_node_runtime_t::node_rid () const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    return node_rid_t::from_string (detail::effective_spot_node_rid (_state->snapshot));
}

std::optional<std::string> spot_node_runtime_t::spot_name_for (spot_rid_t spot_rid) const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto found = _state->spot_names_by_rid.find (std::string (spot_rid.value ()));
    if (found == _state->spot_names_by_rid.end ()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<spot_route_t> spot_node_runtime_t::resolve_spot (spot_rid_t spot_rid) const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto found = _state->spot_names_by_rid.find (std::string (spot_rid.value ()));
    if (found != _state->spot_names_by_rid.end ()) {
        return spot_route_t{
          node_rid_t::from_string (detail::effective_spot_node_rid (_state->snapshot)),
          std::move (spot_rid), found->second};
    }
    for (const auto &[_, resolver] : _state->resolvers) {
        if (auto route = resolver (spot_rid)) {
            return route;
        }
    }
    if (_state->spot_location_resolver) {
        const auto address =
          _state->spot_location_resolver
            ->resolve_spot_address (_state->snapshot.name,
                                zlink::routing_id_t::from (std::string (spot_rid.value ())))
            .result ()
            .value ();
        if (address) {
            return spot_route_t{node_rid_t::from_string (address->node_rid.to_string ()),
                                spot_rid_t::from_string (address->spot_rid.to_string ()),
                                {}};
        }
    }
    return std::nullopt;
}

std::optional<spot_rid_t> spot_node_runtime_t::actor_spot (const actor_ref_t &actor_ref) const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto found = _state->actor_spot_rids.find (actor_key (actor_ref));
    if (found == _state->actor_spot_rids.end ()) {
        return std::nullopt;
    }
    return found->second;
}

void spot_node_runtime_t::record_actor_spot (const actor_ref_t &actor_ref, spot_rid_t spot_rid)
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto key = actor_key (actor_ref);
    auto name = spot_name_for (spot_rid).value_or ("");
    detail::record_actor_route_unlocked (
      *_state, key,
      spot_route_t{node_rid_t::from_string (detail::effective_spot_node_rid (_state->snapshot)),
                   std::move (spot_rid), std::move (name)},
      actor_ref.generation ());
}

std::optional<spot_route_t> spot_node_runtime_t::actor_route (const actor_ref_t &actor_ref) const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto found = _state->actor_routes.find (actor_key (actor_ref));
    if (found == _state->actor_routes.end ()) {
        return std::nullopt;
    }
    return found->second;
}

void spot_node_runtime_t::record_actor_route (const actor_ref_t &actor_ref, spot_route_t route)
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto key = actor_key (actor_ref);
    detail::record_actor_route_unlocked (*_state, key, std::move (route), actor_ref.generation ());
}

std::optional<std::string> spot_node_runtime_t::actor_route_transport_name () const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    if (_state->snapshot.spot_route_channel_name
        && !_state->snapshot.spot_route_channel_name->empty ()) {
        return _state->snapshot.spot_route_channel_name;
    }
    if (_state->snapshot.accepted_route_channels.size () == 1) {
        return _state->snapshot.accepted_route_channels.front ().channel_name;
    }
    if (_state->snapshot.discovery_channel_name
        && !_state->snapshot.discovery_channel_name->empty ()) {
        return _state->snapshot.discovery_channel_name;
    }
    return std::nullopt;
}

void spot_node_runtime_t::cancel_pending_work () noexcept
{
    try {
        detail::drain_spot_node_executors (*_state);
    }
    catch (...) {
    }
}

void spot_node_runtime_t::release_native_handles () noexcept
{
    try {
        std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
        _state->native_actors.clear ();
        _state->native_spots_by_rid.clear ();
        _state->routed_control_spot.reset ();
    }
    catch (...) {
    }
}

void spot_node_runtime_t::request_stop () noexcept
{
    _state->stopping.store (true, std::memory_order_release);
}

bool spot_node_runtime_t::stopping () const noexcept
{
    return _state->stopping.load (std::memory_order_acquire);
}

void spot_node_runtime_t::cancel_pending_dispatch () noexcept
{
    try {
        detail::cancel_spot_node_dispatch_queues (*_state);
    }
    catch (...) {
    }
}

void spot_node_runtime_t::cancel_timers () noexcept
{
    try {
        for (auto &[_, context] : _state->spot_contexts_by_rid) {
            if (context._state) {
                detail::timer_runtime_t (context._state).cancel_all ();
            }
        }
    }
    catch (...) {
    }
}

std::optional<actor_ref_t>
spot_node_runtime_t::current_actor_ref (const actor_ref_t &actor_ref) const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto found = _state->actor_generations.find (actor_key (actor_ref));
    if (found == _state->actor_generations.end ()) {
        return std::nullopt;
    }
    return actor_ref_t (
      node_rid_t::from_string (detail::effective_spot_node_rid (_state->snapshot)),
      std::string (actor_ref.actor_type ()), std::string (actor_ref.actor_id ()), found->second);
}

const std::vector<std::string> &
spot_node_runtime_t::ordering_log (const spot_context_t &context) const
{
    return context._state->ordering_log;
}

void spot_node_runtime_t::attach_native_node (std::shared_ptr<zlink::service::spot_node_t> node)
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    _state->stopping.store (false, std::memory_order_release);
    _state->native_node = std::move (node);
    if (_state->spot_contexts_by_rid.empty ()) {
        if (auto native = _state->native_node.lock ()) {
            _state->routed_control_spot =
              std::make_shared<zlink::service::spot_t> (native->entry_spot ());
        }
    }
    for (auto &[_, context] : _state->spot_contexts_by_rid) {
        attach_native_spot_locked (context._state);
    }
    if (_state->monitoring) {
        monitoring_runtime_t (_state->monitoring)
          .publish_spot_snapshot (spot_event_payload_t{runtime_event_base_t{_state->snapshot.name},
                                                       spot_event_kind_t::status_changed,
                                                       _state->snapshot.name,
                                                       {},
                                                       {},
                                                       std::nullopt});
    }
}

void spot_node_runtime_t::detach_native_node ()
{
    std::vector<std::shared_ptr<zlink::service::spot_t>> native_spots;
    {
        std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
        native_spots.reserve (_state->native_spots_by_rid.size ());
        for (const auto &[_, native] : _state->native_spots_by_rid) {
            if (native) {
                native_spots.push_back (native);
            }
        }
        _state->native_node.reset ();
        _state->native_spots_by_rid.clear ();
        _state->routed_control_spot.reset ();
        _state->last_monitoring_peers.clear ();
        for (auto &[_, context] : _state->spot_contexts_by_rid) {
            context._state->native_spot.reset ();
        }
        if (_state->monitoring) {
            monitoring_runtime_t (_state->monitoring)
              .publish_spot_snapshot (
                spot_event_payload_t{runtime_event_base_t{_state->snapshot.name,
                                                          std::chrono::system_clock::now (),
                                                          runtime_event_severity_t::warning,
                                                          {},
                                                          {},
                                                          health_status_t::degraded},
                                     spot_event_kind_t::status_changed,
                                     _state->snapshot.name,
                                     {},
                                     {},
                                     std::nullopt});
        }
    }

    std::exception_ptr close_error;
    for (const auto &native : native_spots) {
        try {
            native->close ();
        }
        catch (...) {
            if (!close_error) {
                close_error = std::current_exception ();
            }
        }
    }
    if (close_error) {
        std::rethrow_exception (close_error);
    }
}

void spot_node_runtime_t::bind_location_lifecycle (runtime::location_lifecycle_t &lifecycle)
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    _state->location_lifecycle = &lifecycle;
}

bool spot_node_runtime_t::has_active_callbacks () const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    for (const auto &[_, context] : _state->spot_contexts_by_rid) {
        if (context._state && context._state->has_active_callback ()) {
            return true;
        }
    }
    return false;
}

std::vector<actor_ref_t> spot_node_runtime_t::local_actor_refs () const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    std::vector<actor_ref_t> refs;
    refs.reserve (_state->actor_spot_rids.size ());
    const auto node_rid = detail::effective_spot_node_rid (_state->snapshot);
    for (const auto &[key, spot_rid] : _state->actor_spot_rids) {
        const auto split = key.find (':');
        if (split == std::string::npos) {
            continue;
        }
        const auto generation = _state->actor_generations.find (key);
        refs.emplace_back (node_rid_t::from_string (node_rid), key.substr (0, split),
                           key.substr (split + 1),
                           generation != _state->actor_generations.end () ? generation->second
                                                                          : 0);
    }
    return refs;
}

std::optional<zlink::message_t>
spot_node_runtime_t::serialize_actor_snapshot (const actor_ref_t &actor_ref) const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    const auto actor = _state->actor_instances.find (actor_key (actor_ref));
    const auto factory = _state->actor_factories.find (std::string (actor_ref.actor_type ()));
    if (actor == _state->actor_instances.end () || !actor->second
        || factory == _state->actor_factories.end () || !_state->channel_runtime
        || !_state->channel_runtime->serializers) {
        return std::nullopt;
    }
    return factory->second.serialize_instance (actor->second.get (),
                                               *_state->channel_runtime->serializers);
}

void spot_node_runtime_t::bind_drain_flag (std::shared_ptr<std::atomic_bool> flag)
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    _state->drain_flag = std::move (flag);
}

void spot_node_runtime_t::bind_spot_location_resolver (runtime::spot_address_resolver_t &resolver)
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    _state->spot_location_resolver = &resolver;
}

std::shared_ptr<zlink::service::spot_node_t> spot_node_runtime_t::native_node () const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    return _state->native_node.lock ();
}

result_t<void> spot_node_runtime_t::send_spot_mesh_parts (
  const zlink::routing_id_t &target_node_rid,
  const zlink::routing_id_t &target_spot_rid,
  runtime::messaging::message_parts_t parts) const
{
    auto node = native_node ();
    if (!node) {
        return result_t<void>::failure (framework_error_kind_t::spot_route_not_found,
                                        "SPOT mesh route requires a running native node");
    }
    try {
        auto native_parts = parts.items ();
        if (native_parts.empty ()) {
            return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                            "SPOT mesh send requires at least one message part");
        }
        auto egress = node->entry_spot ();
        auto iterator = native_parts.begin ();
        auto submit = egress.send_to_spot (target_node_rid, target_spot_rid).message (*iterator);
        for (++iterator; iterator != native_parts.end (); ++iterator) {
            submit = std::move (submit).message (*iterator);
        }
        if (!std::move (submit).submit ()) {
            return result_t<void>::failure (framework_error_kind_t::request_failed,
                                            "SPOT mesh send was not submitted");
        }
        return result_t<void>::success ();
    }
    catch (const framework_exception_t &error) {
        return detail::result_access_t::failure<void> (error);
    }
    catch (const std::exception &error) {
        return result_t<void>::failure (framework_error_kind_t::request_failed, error.what ());
    }
}

result_t<runtime::messaging::message_parts_t> spot_node_runtime_t::request_spot_mesh_parts (
  const zlink::routing_id_t &target_node_rid,
  const zlink::routing_id_t &target_spot_rid,
  runtime::messaging::message_parts_t parts,
  std::chrono::milliseconds timeout) const
{
    auto node = native_node ();
    if (!node) {
        return result_t<runtime::messaging::message_parts_t>::failure (
          framework_error_kind_t::spot_route_not_found,
          "SPOT mesh route requires a running native node");
    }
    try {
        auto native_parts = parts.items ();
        if (native_parts.empty ()) {
            return result_t<runtime::messaging::message_parts_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "SPOT mesh request requires at least one message part");
        }
        auto egress = node->entry_spot ();
        auto iterator = native_parts.begin ();
        auto submit = egress.request_to_spot (target_node_rid, target_spot_rid).message (*iterator);
        for (++iterator; iterator != native_parts.end (); ++iterator) {
            submit = std::move (submit).message (*iterator);
        }
        auto reply = std::move (submit).timeout (timeout).async ().get ();
        return result_t<runtime::messaging::message_parts_t>::success (
          runtime::messaging::message_parts_t (std::move (reply)));
    }
    catch (const framework_exception_t &error) {
        return detail::result_access_t::failure<runtime::messaging::message_parts_t> (error);
    }
    catch (const zlink::request_error_t &error) {
        return detail::result_access_t::failure<runtime::messaging::message_parts_t> (
          native_request_error (error.result (), error.what ()));
    }
    catch (const zlink::submit_error_t &error) {
        return detail::boundary_failure<runtime::messaging::message_parts_t> (detail::boundary_error_t::timed_out, error.what ());
    }
    catch (const std::exception &error) {
        return result_t<runtime::messaging::message_parts_t>::failure (
          framework_error_kind_t::request_failed, error.what ());
    }
}

void spot_node_runtime_t::set_route_client (route_client_t route_client)
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    _state->route_client = std::move (route_client);
}

void spot_node_runtime_t::publish_peer_snapshot_if_changed ()
{
    if (!_state->monitoring) {
        return;
    }

    std::vector<std::string> peers;
    {
        std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
        auto native = _state->native_node.lock ();
        if (!native) {
            return;
        }
        for (const auto &peer : native->peers ()) {
            if (!peer.peer_endpoint ().empty ()) {
                peers.push_back (peer.peer_endpoint ());
            }
        }
        std::sort (peers.begin (), peers.end ());
        peers.erase (std::unique (peers.begin (), peers.end ()), peers.end ());
        if (peers == _state->last_monitoring_peers) {
            return;
        }
        _state->last_monitoring_peers = peers;
    }

    monitoring_runtime_t (_state->monitoring)
      .publish_spot_snapshot (spot_event_payload_t{runtime_event_base_t{_state->snapshot.name},
                                                   spot_event_kind_t::peers_changed,
                                                   _state->snapshot.name,
                                                   std::move (peers),
                                                   {},
                                                   std::nullopt});
}

std::vector<spot_context_t> spot_node_runtime_t::active_contexts () const
{
    std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
    std::vector<spot_context_t> contexts;
    contexts.reserve (_state->spot_contexts_by_rid.size ());
    for (const auto &[_, context] : _state->spot_contexts_by_rid) {
        if (!context._state->closed && !context._state->native_spot.expired ()) {
            contexts.push_back (context);
        }
    }
    return contexts;
}

std::size_t spot_node_runtime_t::drain_actor_packets (service_provider_t &services,
                                                      serializer_registry_t &serializers)
{
    auto native = native_node ();
    if (!native) {
        return 0;
    }

    auto find_actor_ref =
      [&] (const zlink::actor_ref_t &native_actor) -> std::optional<actor_ref_t> {
        std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
        const auto actor_id = native_actor.actor_id ();
        const auto suffix = ":" + actor_id;
        for (const auto &[key, generation] : _state->actor_generations) {
            if (key.size () <= suffix.size ()
                || key.compare (key.size () - suffix.size (), suffix.size (), suffix) != 0) {
                continue;
            }
            const auto separator = key.find (':');
            if (separator == std::string::npos || separator == 0) {
                continue;
            }
            return actor_ref_t (
              node_rid_t::from_string (detail::effective_spot_node_rid (_state->snapshot)),
              key.substr (0, separator), actor_id,
              generation == 0 ? native_actor.generation () : generation);
        }
        for (const auto &[actor_type, _] : _state->actor_factories) {
            const auto key = actor_type + ":" + actor_id;
            if (_state->actor_instances.find (key) != _state->actor_instances.end ()
                || _state->actor_spot_rids.find (key) != _state->actor_spot_rids.end ()) {
                return actor_ref_t (
                  node_rid_t::from_string (detail::effective_spot_node_rid (_state->snapshot)),
                  actor_type, actor_id, native_actor.generation ());
            }
        }
        return std::nullopt;
    };

    auto reply_no_bind = [&] (const zlink::actor_recv_info_t &info,
                              runtime::messaging::message_parts_t reply_parts) {
        auto parts = reply_parts.items ();
        if (parts.empty () || info.request_id == 0) {
            return;
        }
        native->reply_actor_no_bind (info, parts);
    };

    std::size_t dispatched = 0;
    runtime::messaging::envelope_codec_t codec;
    detail::channel_reply_writer_t replies;
    std::vector<spot_node_builder_state_t::queued_actor_packet_t> queued_packets;
    {
        std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
        queued_packets.swap (_state->queued_actor_packets);
    }
    for (auto &packet : queued_packets) {
        const zlink::actor_recv_info_t info = packet.info;
        auto parts = runtime::messaging::message_parts_t (std::move (packet.parts));
        auto header = codec.decode_header (parts);
        if (!header) {
            report_spot_dispatch_error (
              _state, dispatch_error_surface_t::spot_actor, dispatch_message_kind_t::actor_request,
              dispatch_error_reason_t::invalid_frame, dispatch_error_action_t::reply_error,
              std::nullopt, std::nullopt, std::nullopt, info.actor.actor_id ());
            if (info.request_id != 0) {
                runtime::messaging::envelope_header_t request_header;
                request_header.kind = runtime::messaging::message_kind_t::request;
                request_header.channel_name = "actor";
                reply_no_bind (info, replies.reply_raw_envelope (
                                       replies.create_error_header (
                                         "actor", request_header,
                                         framework_exception_t (
                                           header.error_kind (),
                                           header.error () ? header.error ()->what ()
                                                           : "actor request header decode failed")),
                                       zlink::message_t::from ("")));
            }
            ++dispatched;
            continue;
        }

        const auto &request_header = header.value ();
        auto route_client = [&] () -> std::optional<route_client_t> {
            std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
            return _state->route_client;
        }();
        if (route_client) {
            auto &actor_gateway = services.get_required<actor_gateway_runtime_t> ();
            spot_route_internal_dispatcher_t internal_dispatcher (*this, actor_gateway,
                                                                  *route_client, serializers);
            if (request_header.kind == runtime::messaging::message_kind_t::request
                && internal_dispatcher.can_handle_request (request_header.message_name)) {
                auto reply = internal_dispatcher.dispatch_request (
                  route_received_packet_t{info.source_node_rid, info.request_id,
                                          runtime::messaging::message_parts_t (parts.items ()),
                                          info.source_session_rid.size () > 0
                                            ? std::make_optional (info.source_session_rid)
                                            : std::nullopt},
                  request_header, services);
                if (reply) {
                    reply_no_bind (info, replies.reply_raw_envelope (
                                           replies.create_reply_header (
                                             runtime::messaging::message_kind_t::response,
                                             request_header.channel_name, request_header),
                                           std::move (reply.value ())));
                } else {
                    reply_no_bind (
                      info,
                      replies.reply_raw_envelope (
                        replies.create_error_header (
                          request_header.channel_name, request_header,
                          framework_exception_t (reply.error_kind (),
                                                 reply.error () ? reply.error ()->what ()
                                                                : "SPOT route request failed")),
                        zlink::message_t::from ("")));
                }
                ++dispatched;
                continue;
            }
        }
        auto actor_ref = find_actor_ref (info.actor);
        auto body = codec.decode_body (parts);
        auto reply_error = [&] (const framework_exception_t &error,
                                const std::optional<actor_ref_t> &resolved_actor) {
            report_spot_dispatch_error (
              _state, dispatch_error_surface_t::spot_actor, dispatch_message_kind_t::actor_request,
              dispatch_reason_from_error (error.kind ()), dispatch_error_action_t::reply_error,
              request_header.message_name, std::nullopt, std::nullopt,
              resolved_actor
                ? std::optional<std::string> (std::string (resolved_actor->actor_id ()))
                : std::optional<std::string> (info.actor.actor_id ()),
              std::make_exception_ptr (error));
            if (request_header.kind == runtime::messaging::message_kind_t::request
                && info.request_id != 0) {
                reply_no_bind (info, replies.reply_raw_envelope (
                                       replies.create_error_header (request_header.channel_name,
                                                                    request_header, error),
                                       zlink::message_t::from ("")));
            }
        };

        if (!body) {
            reply_error (framework_exception_t (body.error_kind (),
                                                body.error () ? body.error ()->what ()
                                                              : "actor request body missing"),
                         actor_ref);
            ++dispatched;
            continue;
        }
        if (!actor_ref) {
            reply_error (framework_exception_t (framework_error_kind_t::actor_route_not_found,
                                                "actor route is not available"),
                         std::nullopt);
            ++dispatched;
            continue;
        }
        const bool is_no_bind =
          info.request_id != 0 && (info.flags & actor_recv_info_no_bind_flag) != 0;
        if (!is_no_bind && info.source_node_rid.size () > 0
            && info.source_session_rid.size () > 0) {
            try {
                auto &actor_gateway = services.get_required<actor_gateway_runtime_t> ();
                const auto transport_name = actor_route_transport_name ();
                if (route_client && transport_name) {
                    actor_gateway.bind_session_route (
                      *actor_ref, *route_client, *transport_name, info.source_node_rid,
                      stream_codec_t::message_pack, false);
                    actor_gateway.record_bound_session_route (
                      *actor_ref, info.source_node_rid, info.source_session_rid);
                } else {
                    native->bind_remote_actor_bound_session (
                      info.actor, info.source_node_rid, info.source_session_rid);
                    actor_gateway.bind_session_sink (
                      *actor_ref,
                      [native, native_actor = info.actor] (
                        std::string packet_name, const zlink::message_t &payload) {
                          auto frame = detail::encode_actor_bound_session_frame (
                            stream_codec_t::message_pack, std::move (packet_name), payload);
                          if (!frame) {
                              return task_t<void> (result_t<void>::failure (
                                frame.error_kind (), frame.error ()
                                                       ? frame.error ()->what ()
                                                       : "actor bound session frame encode failed"));
                          }
                          try {
                              auto submitted = native->send_bound_session_msg (native_actor)
                                                 .message (std::move (frame.value ()))
                                                 .submit ();
                              if (!submitted) {
                                  return task_t<void> (result_t<void>::failure (
                                    framework_error_kind_t::request_failed,
                                    "actor bound session send was backpressured", true));
                              }
                              return task_t<void> (result_t<void>::success ());
                          }
                          catch (const zlink::submit_error_t &error) {
                              return task_t<void> (result_t<void>::failure (
                                submit_result_error_kind (error.result ()), error.what ()));
                          }
                          catch (const std::exception &error) {
                              return task_t<void> (result_t<void>::failure (
                                framework_error_kind_t::request_failed, error.what ()));
                          }
                      },
                      stream_codec_t::message_pack);
                }
            }
            catch (const framework_exception_t &error) {
                reply_error (error, actor_ref);
                ++dispatched;
                continue;
            }
            catch (const zlink::config_error_t &error) {
                reply_error (
                  framework_exception_t (framework_error_kind_t::request_failed, error.what ()),
                  actor_ref);
                ++dispatched;
                continue;
            }
            catch (const std::exception &error) {
                reply_error (
                  framework_exception_t (framework_error_kind_t::request_failed, error.what ()),
                  actor_ref);
                ++dispatched;
                continue;
            }
        }

        auto dispatch_actor = [this, native, info, request_header, actor_ref = *actor_ref,
                               body = body.value (), &services, &serializers] () mutable {
            detail::channel_reply_writer_t replies;
            auto reply_no_bind = [&] (runtime::messaging::message_parts_t reply_parts) {
                auto parts = reply_parts.items ();
                if (parts.empty () || info.request_id == 0) {
                    return;
                }
                native->reply_actor_no_bind (info, parts);
            };
            auto reply_error = [&] (const framework_exception_t &error) {
                report_spot_dispatch_error (
                  _state, dispatch_error_surface_t::spot_actor,
                  dispatch_message_kind_t::actor_request,
                  dispatch_reason_from_error (error.kind ()), dispatch_error_action_t::reply_error,
                  request_header.message_name, std::nullopt, std::nullopt,
                  std::optional<std::string> (std::string (actor_ref.actor_id ())),
                  std::make_exception_ptr (error));
                if (request_header.kind == runtime::messaging::message_kind_t::request
                    && info.request_id != 0) {
                    reply_no_bind (replies.reply_raw_envelope (
                      replies.create_error_header (request_header.channel_name, request_header,
                                                   error),
                      zlink::message_t::from ("")));
                }
            };

            auto relayed = [&] {
                try {
                    return relay_actor_packet (
                      actor_ref, actor_context_t{},
                      request_header.kind == runtime::messaging::message_kind_t::command
                        ? stream_message_kind_t::send
                        : stream_message_kind_t::request,
                      request_header.message_name, body, services, serializers,
                      spot_actor_message_metadata_t{.content_type = request_header.content_type,
                                                    .values = request_header.metadata});
                }
                catch (const framework_exception_t &error) {
                    return detail::result_access_t::failure<std::optional<zlink::message_t>> (error);
                }
                catch (const std::exception &error) {
                    return result_t<std::optional<zlink::message_t>>::failure (
                      framework_error_kind_t::request_failed, error.what ());
                }
                catch (...) {
                    return result_t<std::optional<zlink::message_t>>::failure (
                      framework_error_kind_t::request_failed,
                      "actor request handler threw an exception");
                }
            }();
            if (!relayed) {
                const auto *error = relayed.error ();
                reply_error (framework_exception_t (
                  relayed.error_kind (), error != nullptr ? error->what () : "actor request failed",
                  error != nullptr && error->is_retriable ()));
                return;
            }
            if (request_header.kind == runtime::messaging::message_kind_t::request
                && relayed.value ()) {
                reply_no_bind (replies.reply_raw_envelope (
                  replies.create_reply_header (runtime::messaging::message_kind_t::response,
                                               request_header.channel_name, request_header),
                  std::move (*relayed.value ())));
            }
        };
        if (request_header.kind == runtime::messaging::message_kind_t::request
            && info.request_id != 0) {
            auto workers = framework_worker_executor (_state);
            if (!workers
                || !workers->try_submit ([dispatch_actor = std::move (dispatch_actor)] () mutable {
                       dispatch_actor ();
                   })) {
                dispatch_actor ();
            }
            ++dispatched;
            continue;
        }
        dispatch_actor ();
        ++dispatched;
    }
    return dispatched;
}

result_t<void> spot_node_runtime_t::dispatch_subscription (const spot_context_t &context,
                                                           std::string topic,
                                                           const zlink::message_t &message,
                                                           service_provider_t &services,
                                                           serializer_registry_t &serializers) const
{
    return dispatch_subscription (context, std::move (topic),
                                  std::vector<zlink::message_t>{message}, services, serializers);
}

result_t<void>
spot_node_runtime_t::dispatch_subscription (const spot_context_t &context,
                                            std::string topic,
                                            const std::vector<zlink::message_t> &parts,
                                            service_provider_t &services,
                                            serializer_registry_t &serializers) const
{
    if (!context._state || !context._state->spot_instance) {
        return result_t<void>::failure (framework_error_kind_t::spot_route_not_found,
                                        "spot context is not registered");
    }
    if (parts.empty ()) {
        return result_t<void>::failure (framework_error_kind_t::request_protocol_error,
                                        "spot subscription frame is empty");
    }
    /* Fan-out wire envelope (flow-correlation §4.1): the decoded header
     * carries the publisher's flow pair, so every subscriber line — including
     * skip/drop lines — shares the tree's flow id. The frame is either the
     * framework's self-delimited single part (['Z''L''F''E'][u32 BE
     * header_len][header JSON][body]), a true two-part envelope from a
     * parts-preserving wire, or a bare payload from a non-framework publisher
     * (dispatched without a flow pair). */
    const runtime::messaging::envelope_codec_t codec;
    zlink::message_t body = parts.front ();
    std::optional<std::string> flow_id;
    std::optional<flow_origin_t> flow_origin;
    std::optional<std::string> packet_name;
    bool report_decode_failure = false;
    if (parts.size () >= 2) {
        const runtime::messaging::message_parts_t envelope_parts{std::vector (parts)};
        auto header = codec.decode_header (envelope_parts);
        auto decoded_body = codec.decode_body (envelope_parts);
        if (header && decoded_body) {
            body = decoded_body.value ();
            flow_id = header.value ().flow_id;
            flow_origin = header.value ().flow_origin;
            if (!header.value ().message_name.empty ()) {
                packet_name = header.value ().message_name;
            }
        } else {
            report_decode_failure = true;
        }
    } else {
        const auto &frame = parts.front ();
        const auto bytes = frame.to_bytes ();
        const bool framed = bytes.size () >= 8 && bytes[0] == static_cast<std::uint8_t> ('Z')
                            && bytes[1] == static_cast<std::uint8_t> ('L')
                            && bytes[2] == static_cast<std::uint8_t> ('F')
                            && bytes[3] == static_cast<std::uint8_t> ('E');
        if (framed) {
            /* The 'ZLFE' prefix is reserved by the fanout wire contract for
             * framework frames — a raw publisher whose payload begins with
             * it is out of contract (CPP-FANOUT-WIRE-001 tracks the final
             * cross-language wire). On a magic match any failure past this
             * point drops the message instead of handing a corrupted body
             * to the application handler. header_size is compared against
             * the remainder (never added to the prefix width) so an
             * adversarial length cannot wrap std::size_t. */
            const std::size_t header_size = (static_cast<std::size_t> (bytes[4]) << 24)
                                            | (static_cast<std::size_t> (bytes[5]) << 16)
                                            | (static_cast<std::size_t> (bytes[6]) << 8)
                                            | static_cast<std::size_t> (bytes[7]);
            report_decode_failure = true;
            if (header_size > 0 && header_size <= bytes.size () - 8) {
                auto header = codec.decode_header (zlink::message_t::from (
                  std::vector<std::uint8_t> (bytes.begin () + 8,
                                             bytes.begin () + 8
                                               + static_cast<std::ptrdiff_t> (header_size))));
                if (header) {
                    body = zlink::message_t::from (std::vector<std::uint8_t> (
                      bytes.begin () + 8 + static_cast<std::ptrdiff_t> (header_size),
                      bytes.end ()));
                    flow_id = header.value ().flow_id;
                    flow_origin = header.value ().flow_origin;
                    if (!header.value ().message_name.empty ()) {
                        packet_name = header.value ().message_name;
                    }
                    report_decode_failure = false;
                }
            }
        }
    }
    if (report_decode_failure) {
        report_spot_dispatch_error (
          _state, dispatch_error_surface_t::spot_subscription, dispatch_message_kind_t::publish,
          dispatch_error_reason_t::payload_decode_failed, dispatch_error_action_t::drop,
          std::nullopt, topic, std::string (context._state->spot_rid.value ()));
        return result_t<void>::success ();
    }
    const bool capture_enabled =
      detail::message_flow_tracer_t (_state->dispatch).capture_enabled ();
    auto flow_scope = runtime::flow_context_t::enter (std::move (flow_id), flow_origin,
                                                      capture_enabled, flow_origin_t::inbound);
    const auto &message = body;
    report_spot_dispatch_trace (
      _state, message_flow_outcome_t::received, dispatch_error_surface_t::spot_subscription,
      dispatch_message_kind_t::publish, {}, topic, context._state->spot_rid.value ());
    bool handler_found = false;
    for (const auto &descriptor : context._state->handlers) {
        if (packet_name && descriptor.kind == spot_handler_kind_t::subscription
            && descriptor.topic == topic && descriptor.packet_name == *packet_name) {
            handler_found = true;
            break;
        }
    }
    if (!handler_found) {
        report_spot_dispatch_error (
          _state, dispatch_error_surface_t::spot_subscription, dispatch_message_kind_t::publish,
          dispatch_error_reason_t::handler_missing, dispatch_error_action_t::drop, packet_name,
          topic, std::string (context._state->spot_rid.value ()));
        return result_t<void>::success ();
    }
    auto result =
      spot_handler_registry_t (context._state)
        .invoke_erased (spot_handler_kind_t::subscription, *packet_name, topic,
                        std::type_index (typeid (void)), context._state->spot_instance.get (),
                        nullptr, services, serializers, message)
        .result ();
    if (!result) {
        const auto *error = result.error ();
        const framework_exception_t exception (
          result.error_kind (), error != nullptr ? error->what () : "spot subscription failed",
          error != nullptr && error->is_retriable ());
        report_spot_dispatch_error (
          _state, dispatch_error_surface_t::spot_subscription, dispatch_message_kind_t::publish,
          dispatch_reason_from_error (exception.kind ()), dispatch_error_action_t::drop,
          *packet_name, topic, std::string (context._state->spot_rid.value ()), std::nullopt,
          std::make_exception_ptr (exception));
        return detail::result_access_t::failure<void> (exception);
    }
    report_spot_dispatch_trace (
      _state, message_flow_outcome_t::dispatched, dispatch_error_surface_t::spot_subscription,
      dispatch_message_kind_t::publish, *packet_name, topic, context._state->spot_rid.value ());
    if (_state->monitoring) {
        runtime::runtime_metrics_t metrics (_state->monitoring);
        if (metrics.enabled ()) {
            metrics.counter ("zlink.fanout.received", "{message}", 1, {{"topic", topic}});
        }
    }
    return result_t<void>::success ();
}

std::size_t spot_node_runtime_t::drain_routed_packets (service_provider_t &services,
                                                       serializer_registry_t &serializers) const
{
    std::size_t dispatched = 0;
    runtime::messaging::envelope_codec_t codec;
    detail::channel_reply_writer_t replies;
    auto contexts = active_contexts ();
    if (contexts.empty ()) {
        std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
        if (_state->routed_control_spot) {
            auto context_state = std::make_shared<spot_context_state_t> ();
            context_state->node = _state;
            context_state->channel_runtime = _state->channel_runtime;
            context_state->node_rid =
              node_rid_t::from_string (detail::effective_spot_node_rid (_state->snapshot));
            context_state->spot_rid =
              spot_rid_t::from_string (detail::effective_spot_node_rid (_state->snapshot));
            context_state->spot_name = "__zlink-routed-control";
            context_state->native_spot = _state->routed_control_spot;
            context_state->spot_instance = _state->routed_control_spot;
            contexts.push_back (spot_context_t (std::move (context_state)));
        }
    }
    for (const auto &context : contexts) {
        auto native = context._state->native_spot.lock ();
        if (!native || !context._state->spot_instance) {
            continue;
        }
        std::vector<zlink::received_t> queued_packets;
        {
            std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
            queued_packets.swap (context._state->queued_routed_packets);
        }
        auto queued_packet = queued_packets.begin ();
        while (true) {
            zlink::received_t inbound;
            if (queued_packet != queued_packets.end ()) {
                inbound = std::move (*queued_packet);
                ++queued_packet;
            } else {
                const int rc = native->recv_routed (inbound, zlink::recv_flags_t::dontwait);
                if (rc == static_cast<int> (zlink::recv_result_t::no_data)) {
                    break;
                }
                if (rc != static_cast<int> (zlink::recv_result_t::ok)) {
                    break;
                }
            }
            struct routed_reply_target_t
            {
                std::optional<zlink::routing_id_t> routing_id;
                std::optional<zlink::routing_id_t> spot_rid;
                std::uint64_t request_seq = 0;
                zlink::received_t received;
            };
            auto submit_reply = [] (zlink::service::spot_t &spot,
                                    const routed_reply_target_t &target,
                                    const runtime::messaging::message_parts_t &reply_parts) {
                auto parts = reply_parts.items ();
                if (parts.empty () || !target.routing_id || target.request_seq == 0) {
                    return;
                }
                auto received = target.received;
                auto iterator = parts.begin ();
                if (target.spot_rid) {
                    try {
                        auto submit = received.reply ().message (*iterator);
                        ++iterator;
                        for (; iterator != parts.end (); ++iterator) {
                            submit = std::move (submit).message (*iterator);
                        }
                        std::move (submit).submit ();
                        return;
                    }
                    catch (const std::exception &) {
                    }
                }
                try {
                    auto fallback =
                      target.spot_rid
                        ? spot
                            .reply_to_spot (*target.routing_id, *target.spot_rid,
                                            target.request_seq)
                            .message (parts[0])
                        : spot.reply_to_router (*target.routing_id, target.request_seq)
                            .message (parts[0]);
                    for (std::size_t index = 1; index < parts.size (); ++index) {
                        fallback = std::move (fallback).message (parts[index]);
                    }
                    std::move (fallback).submit ();
                }
                catch (const std::exception &error) {
                    std::cerr << "zlink framework spot route reply ignored: " << error.what ()
                              << '\n';
                }
                catch (...) {
                    std::cerr << "zlink framework spot route reply ignored\n";
                }
            };
            auto parts = runtime::messaging::message_parts_t (inbound.parts ());
            auto header = codec.decode_header (parts);
            if (!header) {
                report_spot_dispatch_error (
                  _state, dispatch_error_surface_t::spot_route, dispatch_message_kind_t::request,
                  dispatch_error_reason_t::invalid_frame, dispatch_error_action_t::drop,
                  std::nullopt, std::nullopt, std::string (context._state->spot_rid.value ()));
                continue;
            }
            const auto message_kind =
              header.value ().kind == runtime::messaging::message_kind_t::request
                ? dispatch_message_kind_t::request
                : dispatch_message_kind_t::send;
            /* Spot internal dispatch is a flow hop (flow-correlation §4):
             * the envelope pair is entered before the received line so
             * every spot line of this packet carries flow=. */
            auto flow_scope = framework::runtime::flow_context_t::enter (
              header.value ().flow_id, header.value ().flow_origin,
              detail::message_flow_tracer_t (_state->dispatch).capture_enabled (),
              flow_origin_t::inbound);
            report_spot_dispatch_trace (
              _state, message_flow_outcome_t::received, dispatch_error_surface_t::spot_route,
              message_kind, header.value ().message_name, {}, context._state->spot_rid.value ());
            const routed_reply_target_t reply_target{inbound.routing_id (), inbound.spot_rid (),
                                                     inbound.request_seq ().value_or (0), inbound};
            auto route_client = [&] () -> std::optional<route_client_t> {
                std::lock_guard<std::recursive_mutex> node_lock (_state->mutex);
                return _state->route_client;
            }();
            if (route_client
                && header.value ().kind == runtime::messaging::message_kind_t::command) {
                auto &actor_gateway = services.get_required<actor_gateway_runtime_t> ();
                spot_route_internal_dispatcher_t internal_dispatcher (*this, actor_gateway,
                                                                      *route_client, serializers);
                if (internal_dispatcher.can_handle_send (header.value ().message_name)) {
                    auto sent = internal_dispatcher.dispatch_send (
                      route_received_packet_t{inbound.routing_id ().value_or (
                                                zlink::routing_id_t::from (std::uint32_t{0})),
                                              inbound.request_seq (), std::move (parts)},
                      services);
                    if (!sent) {
                        const auto *error = sent.error ();
                        const framework_exception_t exception (
                          sent.error_kind (),
                          error != nullptr ? error->what () : "SPOT route send failed",
                          error != nullptr && error->is_retriable ());
                        report_spot_dispatch_error (
                          _state, dispatch_error_surface_t::spot_route,
                          dispatch_message_kind_t::send,
                          dispatch_reason_from_error (exception.kind ()),
                          dispatch_error_action_t::drop, header.value ().message_name, std::nullopt,
                          std::string (context._state->spot_rid.value ()), std::nullopt,
                          std::make_exception_ptr (exception));
                    }
                    ++dispatched;
                    continue;
                }
            }
            if (route_client
                && header.value ().kind == runtime::messaging::message_kind_t::request) {
                auto &actor_gateway = services.get_required<actor_gateway_runtime_t> ();
                spot_route_internal_dispatcher_t internal_dispatcher (*this, actor_gateway,
                                                                      *route_client, serializers);
                if (internal_dispatcher.can_handle_request (header.value ().message_name)) {
                    if (!inbound.routing_id ()) {
                        report_spot_dispatch_error (
                          _state, dispatch_error_surface_t::spot_route,
                          dispatch_message_kind_t::request, dispatch_error_reason_t::invalid_frame,
                          dispatch_error_action_t::drop, header.value ().message_name, std::nullopt,
                          std::string (context._state->spot_rid.value ()));
                        ++dispatched;
                        continue;
                    }
                    auto dispatch_internal_request =
                      [this, native, actor_gateway, route_client = *route_client,
                       header_value = header.value (), reply_target,
                       received =
                         route_received_packet_t{*inbound.routing_id (), inbound.request_seq (),
                                                 std::move (parts)},
                       spot_rid = std::string (context._state->spot_rid.value ()), &services,
                       &serializers, submit_reply] () mutable {
                          detail::channel_reply_writer_t replies;
                          spot_route_internal_dispatcher_t dispatcher (*this, actor_gateway,
                                                                       route_client, serializers);
                          auto reply =
                            dispatcher.dispatch_request (received, header_value, services);
                          if (reply && reply_target.request_seq != 0) {
                              auto reply_parts = replies.reply_raw_envelope (
                                replies.create_reply_header (
                                  runtime::messaging::message_kind_t::response,
                                  header_value.channel_name, header_value),
                                std::move (reply.value ()));
                              submit_reply (*native, reply_target, reply_parts);
                              report_spot_dispatch_trace (_state, message_flow_outcome_t::replied,
                                                          dispatch_error_surface_t::spot_route,
                                                          dispatch_message_kind_t::response,
                                                          header_value.message_name, {}, spot_rid);
                          } else if (!reply && reply_target.request_seq != 0) {
                              const auto *error = reply.error ();
                              const framework_exception_t exception (
                                reply.error_kind (),
                                error != nullptr ? error->what () : "SPOT route request failed",
                                error != nullptr && error->is_retriable ());
                              auto reply_parts = replies.reply_raw_envelope (
                                replies.create_error_header (header_value.channel_name,
                                                             header_value, exception),
                                zlink::message_t::from (""));
                              submit_reply (*native, reply_target, reply_parts);
                              report_spot_dispatch_error (
                                _state, dispatch_error_surface_t::spot_route,
                                dispatch_message_kind_t::request,
                                dispatch_reason_from_error (exception.kind ()),
                                dispatch_error_action_t::reply_error, header_value.message_name,
                                std::nullopt, spot_rid, std::nullopt,
                                std::make_exception_ptr (exception));
                          }
                      };
                    auto workers = framework_worker_executor (_state);
                    if (!workers
                        || !workers->try_submit ([dispatch_internal_request = std::move (
                                                    dispatch_internal_request)] () mutable {
                               dispatch_internal_request ();
                           })) {
                        dispatch_internal_request ();
                    }
                    ++dispatched;
                    continue;
                }
            }
            auto body = codec.decode_body (parts);
            auto reply_error = [&] (const framework_exception_t &error) {
                report_spot_dispatch_error (
                  _state, dispatch_error_surface_t::spot_route, dispatch_message_kind_t::request,
                  dispatch_reason_from_error (error.kind ()), dispatch_error_action_t::reply_error,
                  header.value ().message_name, std::nullopt,
                  std::string (context._state->spot_rid.value ()), std::nullopt,
                  std::make_exception_ptr (error));
                if (!inbound.request_seq ()) {
                    return;
                }
                auto reply_parts = replies.reply_raw_envelope (
                  replies.create_error_header (header.value ().channel_name, header.value (),
                                               error),
                  zlink::message_t::from (""));
                submit_reply (*native, reply_target, reply_parts);
            };
            if (!body) {
                reply_error (framework_exception_t (body.error_kind (),
                                                    body.error () ? body.error ()->what ()
                                                                  : "spot routed body missing"));
                continue;
            }
            auto handler_task =
              spot_handler_registry_t (context._state)
                .invoke_erased (
                  spot_handler_kind_t::packet, header.value ().message_name, {},
                  std::type_index (typeid (void)), context._state->spot_instance.get (), nullptr,
                  services, serializers, body.value (),
                  spot_actor_message_metadata_t{.content_type = header.value ().content_type,
                                                .values = header.value ().metadata});
            auto task_holder =
              std::make_shared<task_t<zlink::message_t>> (std::move (handler_task));
            auto native_for_reply = native;
            auto header_value = header.value ();
            auto node_state = _state;
            auto spot_rid = std::string (context._state->spot_rid.value ());
            detail::observe_task_completion (
              *task_holder,
              [task_holder, native_for_reply, reply_target, header_value, node_state, spot_rid,
               replies, submit_reply] (const result_t<zlink::message_t> &result) mutable {
                  if (!result) {
                      const auto *error = result.error ();
                      const framework_exception_t exception (
                        result.error_kind (),
                        error != nullptr ? error->what () : "spot routed handler failed",
                        error != nullptr && error->is_retriable ());
                      if (header_value.kind == runtime::messaging::message_kind_t::request) {
                          report_spot_dispatch_error (
                            node_state, dispatch_error_surface_t::spot_route,
                            dispatch_message_kind_t::request,
                            dispatch_reason_from_error (exception.kind ()),
                            dispatch_error_action_t::reply_error, header_value.message_name,
                            std::nullopt, spot_rid, std::nullopt,
                            std::make_exception_ptr (exception));
                          if (reply_target.request_seq != 0) {
                              auto reply_parts = replies.reply_raw_envelope (
                                replies.create_error_header (header_value.channel_name,
                                                             header_value, exception),
                                zlink::message_t::from (""));
                              submit_reply (*native_for_reply, reply_target, reply_parts);
                          }
                      } else {
                          report_spot_dispatch_error (
                            node_state, dispatch_error_surface_t::spot_route,
                            dispatch_message_kind_t::send,
                            dispatch_reason_from_error (exception.kind ()),
                            dispatch_error_action_t::drop, header_value.message_name, std::nullopt,
                            spot_rid, std::nullopt, std::make_exception_ptr (exception));
                      }
                      return;
                  }
                  if (header_value.kind == runtime::messaging::message_kind_t::request) {
                      auto reply_parts = replies.reply_raw_envelope (
                        replies.create_reply_header (runtime::messaging::message_kind_t::response,
                                                     header_value.channel_name, header_value),
                        result.value ());
                      submit_reply (*native_for_reply, reply_target, reply_parts);
                      report_spot_dispatch_trace (node_state, message_flow_outcome_t::replied,
                                                  dispatch_error_surface_t::spot_route,
                                                  dispatch_message_kind_t::response,
                                                  header_value.message_name, {}, spot_rid);
                  } else {
                      report_spot_dispatch_trace (node_state, message_flow_outcome_t::dispatched,
                                                  dispatch_error_surface_t::spot_route,
                                                  dispatch_message_kind_t::send,
                                                  header_value.message_name, {}, spot_rid);
                  }
              });
            ++dispatched;
        }
    }
    return dispatched;
}

std::size_t spot_node_runtime_t::drain_subscriptions (service_provider_t &services,
                                                      serializer_registry_t &serializers) const
{
    std::size_t dispatched = 0;
    for (const auto &context : active_contexts ()) {
        auto native = context._state->native_spot.lock ();
        if (!native) {
            continue;
        }
        while (true) {
            zlink::topic_message_t inbound;
            const int rc = native->subscribe (inbound, zlink::recv_flags_t::dontwait);
            if (rc == static_cast<int> (zlink::recv_result_t::no_data)) {
                break;
            }
            if (rc != static_cast<int> (zlink::recv_result_t::ok)) {
                break;
            }
            if (inbound.parts ().empty ()) {
                continue;
            }
            auto dispatched_result = dispatch_subscription (context, inbound.topic (),
                                                            inbound.parts (), services,
                                                            serializers);
            if (dispatched_result) {
                ++dispatched;
            }
        }
    }
    return dispatched;
}

} // namespace zlink::framework::detail
