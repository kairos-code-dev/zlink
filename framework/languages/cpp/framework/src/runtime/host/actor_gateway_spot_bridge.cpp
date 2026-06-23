/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/host/actor_gateway_spot_bridge.hpp"

#include "runtime/actors/actor_route_internal_dispatcher.hpp"
#include "runtime/spots/spot_route_internal_dispatcher.hpp"
#include "runtime/spots/spot_route_packets.hpp"
#include "runtime/spots/spot_runtime.hpp"

namespace zlink::framework::detail
{

namespace
{

spot_actor_message_metadata_t project_stream_metadata (const stream_header_t &header,
                                                       const message_metadata_policy_t &policy)
{
    return policy.project (header.metadata ().values ());
}

result_t<actor_join_reply_t> join_actor_to_spot_through_route (
  spot_node_runtime_t runtime,
  route_client_t route_client,
  std::string local_spot_node_rid,
  std::optional<std::string> route_channel_name,
  const actor_ref_t &actor_ref,
  spot_rid_t spot_rid,
  const zlink::message_t &payload)
{
    auto route = runtime.resolve_spot (spot_rid);
    if (!route || route->node_rid.empty ()
        || route->node_rid.value () == local_spot_node_rid) {
        return runtime.join_actor_to_spot_erased (actor_ref, std::move (spot_rid), payload);
    }
    if (!route_channel_name || route_channel_name->empty ()) {
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::spot_route_not_found,
          "remote SPOT route channel is not configured");
    }

    auto request = make_spot_actor_join_route_request (actor_ref, spot_rid, payload);
    auto reply = route_client
                   .request (*route_channel_name,
                             zlink::routing_id_t::from (
                               std::string (route->node_rid.value ())),
                             std::move (request))
                   .packet_name (spot_actor_join_route_request_t::packet_name)
                   .template async<spot_actor_join_route_reply_t> ()
                   .result ();
    if (!reply) {
        return result_t<actor_join_reply_t>::failure (
          reply.error_kind (),
          reply.error () ? reply.error ()->what () : "remote SPOT route join failed");
    }
    auto joined = actor_join_reply_from_spot_route (reply.value ());
    runtime.record_actor_route (joined.actor, spot_route_t{route->node_rid, spot_rid,
                                                           route->spot_name});
    return result_t<actor_join_reply_t>::success (std::move (joined));
}

result_t<std::optional<zlink::message_t>> relay_actor_packet_through_route (
  spot_node_runtime_t runtime,
  actor_gateway_runtime_t actor_gateway,
  route_client_t route_client,
  std::optional<std::string> route_channel_name,
  const actor_ref_t &actor_ref,
  actor_context_t actor_context,
  const stream_header_t &header,
  const zlink::message_t &payload,
  service_provider_t &provider,
  serializer_registry_t &serializers,
  spot_actor_message_metadata_t metadata)
{
    const auto send_remote =
      [&] (const spot_route_t &route,
           const spot_rid_t &spot_rid) -> result_t<std::optional<zlink::message_t>> {
        if (!route_channel_name || route_channel_name->empty ()) {
            return result_t<std::optional<zlink::message_t>>::failure (
              framework_error_kind_t::spot_route_not_found,
              "remote SPOT route channel is not configured");
        }
        auto reply = route_client
                       .request (*route_channel_name,
                                 zlink::routing_id_t::from (
                                   std::string (route.node_rid.value ())),
                                 make_spot_actor_packet_route_request (
                                   actor_ref, spot_rid, header.packet_name (), payload, metadata))
                       .packet_name (spot_actor_packet_route_request_t::packet_name)
                       .template async<spot_actor_packet_route_reply_t> ()
                       .result ();
        if (!reply) {
            return result_t<std::optional<zlink::message_t>>::failure (
              reply.error_kind (),
              reply.error () ? reply.error ()->what () : "remote actor packet relay failed");
        }
        if (reply.value ().actor_ref_present) {
            auto updated_actor_ref = actor_ref_t (
              node_rid_t::from_string (reply.value ().actor_node_rid),
              reply.value ().actor_type,
              reply.value ().actor_id,
              reply.value ().actor_generation);
            (void) actor_gateway.update_actor_ref (updated_actor_ref);
            runtime.record_actor_route (
              updated_actor_ref,
              spot_route_t{route.node_rid, spot_rid, route.spot_name});
        }
        if (!reply.value ().has_reply) {
            return result_t<std::optional<zlink::message_t>>::success (std::nullopt);
        }
        return result_t<std::optional<zlink::message_t>>::success (
          zlink::message_t::from (reply.value ().payload));
      };

    auto route = runtime.actor_route (actor_ref);
    if (route && !route->node_rid.empty ()
        && route->node_rid.value () != runtime.node_rid ().value ()) {
        return send_remote (*route, route->spot_rid);
    }

    auto local = runtime.relay_actor_packet (actor_ref, actor_context, header.packet_name (),
                                             payload, provider, serializers, metadata);
    if (local
        || (local.error_kind () != framework_error_kind_t::spot_route_not_found
            && local.error_kind () != framework_error_kind_t::actor_route_not_found)) {
        return local;
    }
    const auto spot_rid = runtime.actor_spot (actor_ref);
    if (!spot_rid) {
        if (!actor_ref.node_rid ().empty ()
            && actor_ref.node_rid ().value () != runtime.node_rid ().value ()) {
            return send_remote (
              spot_route_t{actor_ref.node_rid (), spot_rid_t{}, std::string {}},
              spot_rid_t{});
        }
        return local;
    }
    if (!route) {
        route = runtime.resolve_spot (*spot_rid);
    }
    if (!route || route->node_rid.empty ()) {
        if (!actor_ref.node_rid ().empty ()
            && actor_ref.node_rid ().value () != runtime.node_rid ().value ()) {
            return send_remote (
              spot_route_t{actor_ref.node_rid (), spot_rid_t{}, std::string {}},
              spot_rid_t{});
        }
        return local;
    }
    return send_remote (*route, *spot_rid);
}

struct actor_gateway_spot_node_binding_t
{
    spot_node_runtime_t runtime;
    route_client_t route_client;
    std::string local_spot_node_rid;
    std::optional<std::string> route_channel_name;
};

bool is_spot_route_miss (framework_error_kind_t kind)
{
    return kind == framework_error_kind_t::spot_route_not_found
           || kind == framework_error_kind_t::actor_route_not_found;
}

bool rid_targets_node (std::string_view rid, std::string_view node_rid)
{
    return rid.size () > node_rid.size () && rid.substr (0, node_rid.size ()) == node_rid
           && rid[node_rid.size ()] == ':';
}

template <typename Relay>
result_t<std::optional<zlink::message_t>>
relay_actor_with_local_binding_first (std::vector<actor_gateway_spot_node_binding_t> &bindings,
                                      const actor_ref_t &actor_ref,
                                      actor_context_t actor_context,
                                      Relay relay)
{
    for (auto &binding : bindings) {
        if (actor_ref.node_rid ().value () != binding.local_spot_node_rid) {
            continue;
        }
        return relay (binding, std::move (actor_context));
    }

    auto last = result_t<std::optional<zlink::message_t>>::failure (
      framework_error_kind_t::actor_route_not_found, "actor route not found");
    for (auto &binding : bindings) {
        auto candidate = relay (binding, actor_context);
        if (candidate || !is_spot_route_miss (candidate.error_kind ())) {
            return candidate;
        }
        last = std::move (candidate);
    }
    return last;
}

template <typename JoinLocal, typename JoinFallback>
result_t<actor_join_reply_t> join_spot_with_target_binding_first (
  std::vector<actor_gateway_spot_node_binding_t> &bindings,
  const spot_rid_t &spot_rid,
  JoinLocal join_local,
  JoinFallback join_fallback)
{
    for (auto &binding : bindings) {
        if (!rid_targets_node (spot_rid.value (), binding.local_spot_node_rid)) {
            continue;
        }
        return join_local (binding, spot_rid);
    }

    auto last = result_t<actor_join_reply_t>::failure (
      framework_error_kind_t::spot_route_not_found, "SPOT node route not found");
    for (auto &binding : bindings) {
        auto candidate = join_fallback (binding, spot_rid);
        if (candidate || !is_spot_route_miss (candidate.error_kind ())) {
            return candidate;
        }
        last = std::move (candidate);
    }
    return last;
}

} // namespace

std::map<std::string, std::shared_ptr<route_internal_packet_dispatcher_t>>
build_route_internal_dispatchers (const zlink_builder_t &builder,
                                  const std::vector<spot_node_snapshot_t> &spot_nodes,
                                  const std::vector<std::string> &route_channel_ids,
                                  actor_gateway_runtime_t actor_gateway,
                                  serializer_registry_t &serializers)
{
    std::map<std::string, std::shared_ptr<route_internal_packet_dispatcher_t>> dispatchers;
    register_spot_route_packet_serializers (serializers);
    for (const auto &route_channel_id : route_channel_ids) {
        auto composite = std::make_shared<composite_route_internal_packet_dispatcher_t> ();
        composite->add (
          std::make_shared<actor_route_internal_dispatcher_t> (actor_gateway, serializers));
        dispatchers.emplace (route_channel_id, std::move (composite));
    }
    for (const auto &spot_node : spot_nodes) {
        if (spot_node.accepted_route_channels.empty ()) {
            continue;
        }
        auto runtime = spot_node_runtime_t::from (builder, spot_node.name);
        if (!runtime) {
            continue;
        }
        for (const auto &accepted : spot_node.accepted_route_channels) {
            auto found = dispatchers.find (accepted.channel_name);
            if (found == dispatchers.end ()) {
                auto composite = std::make_shared<composite_route_internal_packet_dispatcher_t> ();
                dispatchers.emplace (accepted.channel_name, composite);
                found = dispatchers.find (accepted.channel_name);
            }
            auto *composite =
              dynamic_cast<composite_route_internal_packet_dispatcher_t *> (found->second.get ());
            if (composite != nullptr) {
                composite->add (
                  std::make_shared<spot_route_internal_dispatcher_t> (*runtime, actor_gateway,
                                                                      builder.route_client (serializers),
                                                                      serializers));
            }
        }
    }
    return dispatchers;
}

void configure_actor_gateway_spot_bridge (
  zlink_builder_t &zlink,
  service_collection_t &services,
  serializer_registry_t &serializers,
  const std::vector<spot_node_snapshot_t> &spot_node_snapshot)
{
    std::vector<actor_gateway_spot_node_binding_t> actor_gateway_spot_nodes;
    actor_gateway_spot_nodes.reserve (spot_node_snapshot.size ());
    for (const auto &spot_node : spot_node_snapshot) {
        if (!spot_node.actor_gateway_enabled) {
            continue;
        }
        auto runtime = spot_node_runtime_t::from (zlink, spot_node.name);
        if (!runtime) {
            continue;
        }
        if (spot_node.entry_spot_name) {
            try {
                (void) runtime->create_spot (*spot_node.entry_spot_name);
            }
            catch (const framework_exception_t &) {
            }
        }
        if (!services.contains (std::type_index (typeid (spot_node_runtime_t)))) {
            services.add_singleton<spot_node_runtime_t> (
              std::make_unique<spot_node_runtime_t> (*runtime));
        }
        if (!services.contains (std::type_index (typeid (spot_node_manager_t)))) {
            services.add_singleton<spot_node_manager_t> (
              std::make_unique<spot_node_manager_t> (runtime->manager ()));
        }
        auto framework_provider = services.build_provider ();
        auto &actor_gateway = framework_provider.get_required<actor_gateway_runtime_t> ();
        runtime->on_destroy_actor ([&actor_gateway] (const actor_ref_t &actor_ref) {
            return actor_gateway.destroy_actor (actor_ref);
        });
        runtime->on_actor_ref_updated ([&actor_gateway] (const actor_ref_t &actor_ref) {
            return actor_gateway.update_actor_ref (actor_ref);
        });
        actor_gateway_spot_nodes.push_back (
          actor_gateway_spot_node_binding_t{*runtime,
                                            zlink.route_client (serializers),
                                            spot_node.name,
                                            spot_node.registry_spot_route_channel});
    }
    if (actor_gateway_spot_nodes.empty ()) {
        return;
    }

    auto framework_provider = services.build_provider ();
    auto &actor_gateway = framework_provider.get_required<actor_gateway_runtime_t> ();
    actor_gateway.on_join_spot (
      [bindings = actor_gateway_spot_nodes, actor_gateway] (
        const actor_ref_t &actor_ref, spot_rid_t spot_rid,
        const zlink::message_t &payload) mutable {
          auto join_local = [&] (actor_gateway_spot_node_binding_t &binding,
                                 const spot_rid_t &target_spot_rid) {
              if (!actor_ref.node_rid ().empty ()
                  && actor_ref.node_rid ().value () != binding.local_spot_node_rid) {
                  auto joined = binding.runtime.join_remote_actor_to_spot_erased (
                    actor_ref, target_spot_rid, payload,
                    actor_gateway.actor_context (actor_ref));
                  if (joined && joined.value ().result_code == 0) {
                      joined.value ().actor = actor_ref_t (
                        node_rid_t::from_string (binding.local_spot_node_rid),
                        std::string (actor_ref.actor_type ()),
                        std::string (actor_ref.actor_id ()),
                        actor_ref.generation ());
                  }
                  return joined;
              }
              return join_actor_to_spot_through_route (
                binding.runtime, binding.route_client, binding.local_spot_node_rid,
                binding.route_channel_name, actor_ref, target_spot_rid, payload);
          };
          auto join_fallback = [&] (actor_gateway_spot_node_binding_t &binding,
                                    const spot_rid_t &target_spot_rid) {
              return join_actor_to_spot_through_route (
                binding.runtime, binding.route_client, binding.local_spot_node_rid,
                binding.route_channel_name, actor_ref, target_spot_rid, payload);
          };
          return join_spot_with_target_binding_first (bindings, spot_rid, join_local,
                                                      join_fallback);
      });
    actor_gateway.on_join_entry_spot (
      [bindings = actor_gateway_spot_nodes] (const actor_ref_t &actor_ref,
                                             node_rid_t node_rid,
                                             const zlink::message_t &payload) mutable {
          for (auto &binding : bindings) {
              if (node_rid.value () != binding.local_spot_node_rid) {
                  continue;
              }
              return binding.runtime.join_actor_to_entry_spot_erased (
                actor_ref, std::move (node_rid), payload);
          }
          return result_t<actor_join_reply_t>::failure (
            framework_error_kind_t::spot_route_not_found, "SPOT node route not found");
      });
    actor_gateway.on_relay (
      [bindings = actor_gateway_spot_nodes,
       actor_gateway,
       services = &services,
       serializers = &serializers] (
        const actor_ref_t &actor_ref, actor_context_t actor_context,
        const stream_header_t &header, const zlink::message_t &payload) mutable {
          auto relay_with = [&] (actor_gateway_spot_node_binding_t &binding,
                                 actor_context_t context) {
              auto provider = services->build_provider ();
              auto &metadata_policy = provider.get_required<message_metadata_policy_t> ();
              return relay_actor_packet_through_route (
                binding.runtime, actor_gateway, binding.route_client,
                binding.route_channel_name, actor_ref, std::move (context), header,
                payload, provider, *serializers,
                project_stream_metadata (header, metadata_policy));
          };
          return relay_actor_with_local_binding_first (bindings, actor_ref,
                                                       std::move (actor_context), relay_with);
      });
    for (auto &node_binding : actor_gateway_spot_nodes) {
        node_binding.runtime.on_actor_packet_relay (
          [bindings = actor_gateway_spot_nodes, actor_gateway] (
            const actor_ref_t &actor_ref, actor_context_t actor_context,
            std::string_view packet_name, const zlink::message_t &payload,
            service_provider_t &services, serializer_registry_t &serializers,
            spot_actor_message_metadata_t metadata) mutable {
              stream_header_t header (stream_message_kind_t::request,
                                      stream_codec_t::message_pack,
                                      stream_header_flags_t::none,
                                      std::nullopt,
                                      std::string (packet_name));
              auto relay_with = [&] (actor_gateway_spot_node_binding_t &binding,
                                     actor_context_t context,
                                     spot_actor_message_metadata_t relay_metadata) {
                  return relay_actor_packet_through_route (
                    binding.runtime, actor_gateway, binding.route_client,
                    binding.route_channel_name, actor_ref, std::move (context), header,
                    payload, services, serializers, std::move (relay_metadata));
              };
              auto relay = [&] (actor_gateway_spot_node_binding_t &binding,
                                actor_context_t context) {
                  return relay_with (binding, std::move (context), metadata);
              };
              return relay_actor_with_local_binding_first (bindings, actor_ref,
                                                           std::move (actor_context), relay);
          });
    }
}

} // namespace zlink::framework::detail
