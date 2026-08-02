/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/actors/actor_gateway_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

int relay_dispatch_scope_restores_nested_and_exception_state ()
{
    using namespace zlink::framework;
    using namespace zlink::framework::detail;

    const stream_header_t outer (
      stream_message_kind_t::send, stream_codec_t::json,
      stream_header_flags_t::none, std::nullopt, "outer");
    const stream_header_t inner (
      stream_message_kind_t::request, stream_codec_t::json,
      stream_header_flags_t::none, std::nullopt, "inner");
    {
        const stream_relay_dispatch_scope_t outer_scope (outer);
        try {
            {
                const stream_relay_dispatch_scope_t inner_scope (inner);
                const auto current = current_stream_relay_dispatch ();
                if (!current || current->packet_name () != "inner") {
                    return 1;
                }
                throw std::runtime_error ("relay failure");
            }
        }
        catch (const std::runtime_error &) {
        }
        const auto restored = current_stream_relay_dispatch ();
        if (!restored || restored->packet_name () != "outer") {
            return 2;
        }
    }
    return current_stream_relay_dispatch () ? 3 : 0;
}

class recording_actor_client_t final : public zlink::framework::actor_client_t
{
  public:
    std::atomic_int attempts{0};

  protected:
    zlink::framework::task_t<void> send_to_actor_erased (
      zlink::framework::actor_ref_t,
      std::string,
      zlink::framework::message_t,
      const zlink::framework::actor_send_call_t::metadata_map_t &) override
    {
        ++attempts;
        return zlink::framework::task_t<void> (
          zlink::framework::result_t<void>::success ());
    }

    zlink::framework::task_t<zlink::framework::message_t> request_to_actor_erased (
      zlink::framework::actor_ref_t,
      std::string,
      zlink::framework::message_t,
      std::optional<std::chrono::milliseconds>,
      const zlink::framework::actor_request_call_t::metadata_map_t &) override
    {
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

int stale_session_unbind_preserves_rebind ()
{
    using namespace zlink::framework;
    using namespace zlink::framework::detail;

    auto state = std::make_shared<actor_gateway_state_t> ();
    actor_gateway_runtime_t gateway (state);
    const actor_ref_t actor (
      node_rid_t::from_string ("actor-node"), "player", "actor-1", 7);

    gateway.bind_session_sink (
      actor,
      [] (std::string, const zlink::message_t &) {
          return task_t<void> (result_t<void>::success ());
      });
    gateway.bind_session_stream (
      "actor-1", stream_t{}, stream_codec_t::message_pack, "session-old", 11);
    gateway.bind_session_stream (
      "actor-1", stream_t{}, stream_codec_t::message_pack, "session-new", 12);

    gateway.unbind_session_stream ("actor-1", "session-old", 11);
    {
        const std::lock_guard lock (state->mutex);
        const auto actor_record = state->actors_by_id.find ("actor-1");
        if (actor_record == state->actors_by_id.end ()
            || actor_record->second.binding_session_id != "session-new"
            || actor_record->second.binding_token != 12
            || !actor_record->second.bound_session_stream_sink
            || state->bound_session_sinks.count ("actor-1") != 1) {
            return 1;
        }
    }

    gateway.unbind_session_stream ("actor-1", "session-new", 12);
    {
        const std::lock_guard lock (state->mutex);
        const auto actor_record = state->actors_by_id.find ("actor-1");
        if (actor_record == state->actors_by_id.end ()
            || !actor_record->second.binding_session_id.empty ()
            || actor_record->second.binding_token != 0
            || actor_record->second.bound_session_stream_sink
            || state->bound_session_sinks.count ("actor-1") != 0) {
            return 2;
        }
    }
    return 0;
}

int actor_send_is_one_shot ()
{
    using namespace zlink::framework;
    recording_actor_client_t client;
    actor_send_call_t call (
      client,
      actor_ref_t (node_rid_t::from_string ("actor-node"), "player", "actor-2", 1),
      "message", message_t{});
    auto copied = call;
    call.submit ().result ().value ();
    bool rejected = false;
    try {
        (void) copied.submit ().result ().value ();
    }
    catch (const framework_exception_t &error) {
        rejected = error.kind () == framework_error_kind_t::protocol_error;
    }
    return rejected && client.attempts.load () == 1 ? 0 : 2;
}

int session_disconnect_is_all_settled_and_token_fenced ()
{
    using namespace zlink::framework;
    using namespace zlink::framework::detail;

    auto state = std::make_shared<actor_gateway_state_t> ();
    actor_gateway_runtime_t gateway (state);
    auto manager = gateway.manager ();
    session_actor_manager_access_t::attach (manager, stream_t{});
    const actor_ref_t first (
      node_rid_t::from_string ("actor-node"), "player", "actor-a", 1);
    const actor_ref_t second (
      node_rid_t::from_string ("actor-node"), "player", "actor-b", 1);

    auto stale = manager.bind (first).submit ().result ().value ();
    auto current = manager.bind (first).submit ().result ().value ();
    (void) manager.bind (second).submit ().result ().value ();
    if (stale.notify_disconnected ().result ().error_kind ()
        != framework_error_kind_t::not_configured) {
        return 1;
    }

    std::vector<std::string> disconnected;
    gateway.on_disconnect (
      [&] (const actor_ref_t &actor) {
          disconnected.emplace_back (actor.actor_id ());
          return actor.actor_id () == "actor-a"
                   ? result_t<void>::failure (
                       framework_error_kind_t::not_found,
                       "actor-a callback failed")
                   : result_t<void>::success ();
      });
    session_actor_manager_access_t::disconnect (manager);
    std::sort (disconnected.begin (), disconnected.end ());
    if (disconnected != std::vector<std::string>{"actor-a", "actor-b"}
        || !gateway.actor_disconnected ("actor-a")
        || !gateway.actor_disconnected ("actor-b")) {
        return 2;
    }
    {
        const std::lock_guard lock (state->mutex);
        if (!state->actors_by_id.contains ("actor-a")
            || !state->actors_by_id.contains ("actor-b")) {
            return 3;
        }
    }
    if (current.notify_disconnected ().result ())
        return 4;
    return 0;
}

int logical_disconnect_is_selected_and_keeps_session_live ()
{
    using namespace zlink::framework;
    using namespace zlink::framework::detail;

    auto state = std::make_shared<actor_gateway_state_t> ();
    actor_gateway_runtime_t gateway (state);
    auto manager = gateway.manager ();
    session_actor_manager_access_t::attach (manager, stream_t{});
    const actor_ref_t first (
      node_rid_t::from_string ("actor-node"), "player", "actor-a", 1);
    const actor_ref_t second (
      node_rid_t::from_string ("actor-node"), "player", "actor-b", 1);
    auto first_binding = manager.bind (first).submit ().result ().value ();
    auto second_binding = manager.bind (second).submit ().result ().value ();
    std::vector<std::string> disconnected;
    gateway.on_disconnect (
      [&] (const actor_ref_t &actor) {
          disconnected.emplace_back (actor.actor_id ());
          return result_t<void>::success ();
      });

    if (!first_binding.notify_disconnected ().result ()) {
        return 1;
    }
    if (disconnected != std::vector<std::string>{"actor-a"})
        return 4;
    {
        const std::lock_guard lock (state->mutex);
        const auto second_record = state->actors_by_id.find ("actor-b");
        if (second_record == state->actors_by_id.end ()
            || second_record->second.binding_token == 0) {
            return 2;
        }
    }
    if (!second_binding.notify_disconnected ().result ()
        || disconnected
             != std::vector<std::string>{"actor-a", "actor-b"}) {
        return 3;
    }
    return 0;
}

int route_update_preserves_object_generation ()
{
    using namespace zlink::framework;
    using namespace zlink::framework::detail;

    actor_gateway_runtime_t gateway;
    auto manager = gateway.manager ();
    session_actor_manager_access_t::attach (manager, stream_t{});
    const actor_ref_t original (
      node_rid_t::from_string ("actor-node-a"), "player", "actor-route", 7);
    auto original_binding =
      manager.bind (original).submit ().result ().value ();
    const actor_ref_t unaffected (
      node_rid_t::from_string ("actor-node-a"), "player", "actor-other", 3);
    auto unaffected_binding =
      manager.bind (unaffected).submit ().result ().value ();

    std::vector<actor_ref_t> relay_routes;
    gateway.on_relay (
      [&] (const actor_ref_t &actor,
           const actor_context_t &,
           const stream_header_t &,
           const zlink::message_t &) {
          relay_routes.push_back (actor);
          return result_t<std::optional<zlink::message_t>>::success (
            std::nullopt);
      });

    const actor_ref_t relocated (
      node_rid_t::from_string ("actor-node-b"), "player", "actor-route", 7);
    if (!gateway.update_actor_ref (relocated))
        return 1;
    if (!original_binding.relay ("packet", zlink::message_t{}).result ()
        || relay_routes.size () != 1
        || relay_routes.front ().node_rid ().value ()
             != relocated.node_rid ().value ()) {
        return 5;
    }
    if (!unaffected_binding.relay ("packet", zlink::message_t{}).result ()
        || relay_routes.size () != 2
        || relay_routes.back ().node_rid ().value ()
             != unaffected.node_rid ().value ()) {
        return 6;
    }
    const actor_ref_t new_incarnation (
      node_rid_t::from_string ("actor-node-c"), "player", "actor-route", 8);
    const auto rejected = gateway.update_actor_ref (new_incarnation);
    if (rejected)
        return 2;
    auto explicit_binding =
      manager.bind (new_incarnation).submit ().result ().value ();
    if (explicit_binding.ref ().generation () != 8)
        return 3;
    const auto stale = original_binding.notify_disconnected ().result ();
    if (stale
        || stale.error_kind ()
             != framework_error_kind_t::not_configured
        || !gateway.actor_bound ("actor-route")) {
        return 4;
    }
    return 0;
}

int actor_context_identity_and_source_fence_are_exact ()
{
    using namespace zlink::framework;
    using namespace zlink::framework::detail;

    actor_gateway_runtime_t gateway;
    const actor_ref_t source (
      node_rid_t::from_string ("actor-node-a"), "player", "actor-context", 7);
    const actor_ref_t same (
      node_rid_t::from_string ("actor-node-a"), "player", "actor-context", 7);
    const actor_ref_t successor (
      node_rid_t::from_string ("actor-node-b"), "player", "actor-context", 7);
    const actor_ref_t new_incarnation (
      node_rid_t::from_string ("actor-node-a"), "player", "actor-context", 8);

    const auto source_context = gateway.actor_context (source);
    const auto same_context = gateway.actor_context (same);
    const auto successor_context = gateway.actor_context (successor);
    const auto new_incarnation_context = gateway.actor_context (new_incarnation);

    if (source_context.actor_id () != "actor-context"
        || source_context.object_generation () != 7
        || source_context.actor_ref ().node_rid ().value () != "actor-node-a") {
        return 1;
    }
    if (!gateway.same_context_source_fence (source_context, same_context)) {
        return 2;
    }
    if (gateway.same_context_source_fence (source_context, successor_context)
        || gateway.same_context_source_fence (
          source_context, new_incarnation_context)) {
        return 3;
    }
    return 0;
}

int bound_session_route_preserves_private_fences ()
{
    using namespace zlink::framework;
    using namespace zlink::framework::detail;

    actor_gateway_runtime_t gateway;
    const actor_ref_t actor (
      node_rid_t::from_string ("actor-node"), "player", "actor-fenced", 7);
    gateway.bind_session_sink (
      actor,
      [] (std::string, const zlink::message_t &) {
          return task_t<void> (result_t<void>::success ());
      });
    gateway.record_bound_session_route (
      actor, zlink::routing_id_t::from (std::string ("session-node")), std::nullopt,
      11, 13, 17, 19, 23, 29);

    const auto route = gateway.bound_session_route (actor);
    if (!route || route->object_generation != 7
        || route->node_generation != 11
        || route->authority_owner_generation != 13
        || route->owner_lease_generation != 17
        || route->binding_generation != 19
        || route->binding_token != 23
        || route->session_sequence != 29) {
        return 1;
    }
    if (!gateway.dispatch_bound_session_send (
          actor, "push", zlink::message_t{})) {
        return 2;
    }
    const auto advanced = gateway.bound_session_route (actor);
    return advanced && advanced->session_sequence == 30 ? 0 : 3;
}

} // namespace

int main ()
{
    if (const auto relay_scope =
          relay_dispatch_scope_restores_nested_and_exception_state ();
        relay_scope != 0) {
        return 110 + relay_scope;
    }
    if (const auto route_fence =
          bound_session_route_preserves_private_fences ();
        route_fence != 0) {
        return 100 + route_fence;
    }
    if (const auto context_fence = actor_context_identity_and_source_fence_are_exact ();
        context_fence != 0) {
        return 90 + context_fence;
    }
    if (const auto stale = stale_session_unbind_preserves_rebind (); stale != 0) {
        return stale;
    }
    const auto one_shot = actor_send_is_one_shot ();
    if (one_shot != 0)
        return 10 + one_shot;
    const auto disconnected =
      session_disconnect_is_all_settled_and_token_fenced ();
    if (disconnected != 0)
        return 20 + disconnected;
    const auto logical =
      logical_disconnect_is_selected_and_keeps_session_live ();
    if (logical != 0)
        return 30 + logical;
    const auto route = route_update_preserves_object_generation ();
    return route == 0 ? 0 : 40 + route;
}
