/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/host/actor_gateway_spot_bridge.hpp"

#include "runtime/actors/actor_route_internal_dispatcher.hpp"
#include "runtime/messaging/envelope_codec.hpp"
#include "runtime/messaging/client_call_codec.hpp"
#include "runtime/spots/spot_route_internal_dispatcher.hpp"
#include "runtime/spots/spot_route_packets.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <zlink.hpp>
#include <zlink/framework/contracts/locations/resolvers.hpp>

#include <condition_variable>
#include <chrono>
#include <mutex>

namespace zlink::framework::detail
{

namespace
{

constexpr std::string_view actor_relay_kind_metadata_key = "__zlink.actorRelayKind";
constexpr std::string_view actor_relay_kind_send = "send";
constexpr std::string_view actor_relay_kind_request = "request";

void remember_actor_relay_kind (spot_actor_message_metadata_t &metadata, stream_message_kind_t kind)
{
    metadata.values[std::string (actor_relay_kind_metadata_key)] =
      kind == stream_message_kind_t::send ? std::string (actor_relay_kind_send)
                                          : std::string (actor_relay_kind_request);
}

spot_actor_message_metadata_t project_stream_metadata (const stream_header_t &header,
                                                       const message_metadata_policy_t &policy)
{
    auto metadata = policy.project (header.metadata ().values ());
    remember_actor_relay_kind (metadata, header.kind ());
    return metadata;
}

bool rid_targets_node (std::string_view rid, std::string_view node_rid)
{
    return rid.size () > node_rid.size () && rid.substr (0, node_rid.size ()) == node_rid
           && rid[node_rid.size ()] == ':';
}

framework_error_kind_t request_result_error_kind (zlink::request_result_t result)
{
    switch (result) {
        case zlink::request_result_t::timed_out:
            return framework_error_kind_t::timeout;
        case zlink::request_result_t::not_connected:
            return framework_error_kind_t::route_not_connected;
        case zlink::request_result_t::ok:
            return framework_error_kind_t::request_failed;
        default:
            return framework_error_kind_t::request_failed;
    }
}

result_t<actor_join_reply_t> actor_join_reply_from_native (
  zlink::request_result_t result,
  int join_result_code,
  const zlink::actor_ref_t &native_actor_ref,
  const actor_ref_t &fallback_actor_ref,
  const std::vector<zlink::message_t> &reply_parts,
  std::string_view operation)
{
    if (result != zlink::request_result_t::ok) {
        return result_t<actor_join_reply_t>::failure (request_result_error_kind (result),
                                                      std::string (operation) + " failed");
    }
    const auto node_rid = native_actor_ref.node_rid ().size () == 0
                            ? fallback_actor_ref.node_rid ()
                            : node_rid_t::from_string (native_actor_ref.node_rid ().to_string ());
    const auto actor_id = native_actor_ref.actor_id ().empty ()
                            ? std::string (fallback_actor_ref.actor_id ())
                            : native_actor_ref.actor_id ();
    auto joined_actor = actor_ref_t (node_rid, std::string (fallback_actor_ref.actor_type ()),
                                     actor_id, native_actor_ref.generation ());
    const auto payload = reply_parts.empty () ? zlink::message_t{} : reply_parts.front ();
    return result_t<actor_join_reply_t>::success (
      actor_join_reply_t{join_result_code, std::move (joined_actor), payload});
}

result_t<actor_join_reply_t> wait_native_actor_join (
  zlink::service::actor_join_callback_submit_operation_t submit,
  const actor_ref_t &fallback_actor_ref,
  std::string_view operation)
{
    struct join_state_t
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool completed = false;
        zlink::actor_join_result_t result;
        std::vector<zlink::message_t> parts;
    };
    auto state = std::make_shared<join_state_t> ();
    try {
        const bool submitted = std::move (submit).submit (
          [state] (const zlink::actor_join_result_t &result,
                   std::vector<zlink::message_t> parts) {
              {
                  std::lock_guard lock (state->mutex);
                  state->result = result;
                  state->parts = std::move (parts);
                  state->completed = true;
              }
              state->changed.notify_all ();
          });
        if (!submitted) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::request_failed, std::string (operation) + " was not submitted");
        }
        std::unique_lock lock (state->mutex);
        state->changed.wait (lock, [&] { return state->completed; });
        return actor_join_reply_from_native (state->result.result, state->result.join_result_code,
                                             state->result.actor, fallback_actor_ref, state->parts,
                                             operation);
    }
    catch (const framework_exception_t &error) {
        return result_t<actor_join_reply_t>::failure (error.kind (), error.what (),
                                                      error.is_retriable ());
    }
    catch (const zlink::request_error_t &error) {
        return result_t<actor_join_reply_t>::failure (request_result_error_kind (error.result ()),
                                                      error.what ());
    }
    catch (const std::exception &error) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::request_failed,
                                                      error.what ());
    }
}

result_t<runtime::messaging::message_parts_t>
request_spot_mesh_parts (spot_node_runtime_t runtime,
                         const node_rid_t &target_node_rid,
                         const spot_rid_t &target_spot_rid,
                         runtime::messaging::message_parts_t parts)
{
    auto native_node = runtime.native_node ();
    if (!native_node) {
        return result_t<runtime::messaging::message_parts_t>::failure (
          framework_error_kind_t::spot_route_not_found, "SPOT node is not running");
    }
    try {
        auto native_parts = parts.items ();
        if (native_parts.empty ()) {
            return result_t<runtime::messaging::message_parts_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "remote SPOT mesh request requires at least one message part");
        }
        auto origin_spot = native_node->entry_spot ();
        auto iterator = native_parts.begin ();
        auto submit =
          origin_spot
            .request_to_spot (
              zlink::routing_id_t::from (std::string (target_node_rid.value ())),
              zlink::routing_id_t::from (std::string (target_spot_rid.value ())))
            .message (*iterator);
        ++iterator;
        for (; iterator != native_parts.end (); ++iterator) {
            submit = std::move (submit).message (*iterator);
        }
        auto reply = std::move (submit).timeout (std::chrono::seconds (30)).async ().get ();
        return result_t<runtime::messaging::message_parts_t>::success (
          runtime::messaging::message_parts_t (std::move (reply)));
    }
    catch (const framework_exception_t &error) {
        return result_t<runtime::messaging::message_parts_t>::failure (
          error.kind (), error.what (), error.is_retriable ());
    }
    catch (const zlink::request_error_t &error) {
        return result_t<runtime::messaging::message_parts_t>::failure (
          request_result_error_kind (error.result ()), error.what ());
    }
    catch (const std::exception &error) {
        return result_t<runtime::messaging::message_parts_t>::failure (
          framework_error_kind_t::request_failed, error.what ());
    }
}

result_t<actor_join_reply_t> join_actor_to_remote_spot_route_mesh (
  spot_node_runtime_t runtime,
  const actor_ref_t &actor_ref,
  const node_rid_t &target_node_rid,
  const spot_rid_t &target_delivery_spot_rid,
  const spot_rid_t &target_join_spot_rid,
  const zlink::message_t &payload,
  const std::optional<zlink::message_t> &actor_snapshot,
  serializer_registry_t &serializers)
{
    runtime::messaging::client_call_codec_t codec;
    auto header = codec.create_envelope (
      runtime::messaging::message_kind_t::request, "spot",
      std::string (spot_actor_join_route_request_t::packet_name), std::chrono::seconds (30));
    auto request =
      make_spot_actor_join_route_request (actor_ref, target_join_spot_rid, payload, actor_snapshot);
    auto parts = codec.encode_envelope_parts (header, request, serializers);
    auto reply_parts =
      request_spot_mesh_parts (runtime, target_node_rid, target_delivery_spot_rid, std::move (parts));
    if (!reply_parts) {
        const auto *error = reply_parts.error ();
        return result_t<actor_join_reply_t>::failure (
          reply_parts.error_kind (),
          error != nullptr ? error->what () : "remote SPOT mesh join failed",
          error != nullptr && error->is_retriable ());
    }
    auto decoded = codec.decode_envelope_reply<spot_actor_join_route_reply_t> (
      reply_parts.value (), serializers, "remote SPOT mesh join reply is empty",
      "remote SPOT mesh join reply decode failed", "JoinEntrySpot");
    if (!decoded) {
        const auto *error = decoded.error ();
        return result_t<actor_join_reply_t>::failure (
          decoded.error_kind (), error != nullptr ? error->what () : "remote SPOT mesh join failed",
          error != nullptr && error->is_retriable ());
    }
    return result_t<actor_join_reply_t>::success (
      actor_join_reply_from_spot_route (decoded.value ()));
}

result_t<actor_join_reply_t> join_actor_to_remote_spot_mesh (spot_node_runtime_t runtime,
                                                             const actor_ref_t &actor_ref,
                                                             const node_rid_t &target_node_rid,
                                                             const spot_rid_t &target_spot_rid,
                                                             const zlink::message_t &payload)
{
    auto native_node = runtime.native_node ();
    if (!native_node) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::spot_route_not_found,
                                                      "SPOT node is not running");
    }
    auto request = payload;
    auto submit =
      native_node
        ->join_actor (zlink::service::spot_node_t::remote_actor_ref (
                        zlink::routing_id_t::from (std::string (actor_ref.node_rid ().value ())),
                        std::string (actor_ref.actor_id ())),
                      zlink::routing_id_t::from (std::string (target_node_rid.value ())),
                      zlink::routing_id_t::from (std::string (target_spot_rid.value ())))
        .message (request)
        .flags (static_cast<int> (zlink::send_flags_t::none));
    return wait_native_actor_join (std::move (submit), actor_ref, "remote SPOT actor join");
}

result_t<actor_join_reply_t> join_actor_to_remote_entry_spot_mesh (
  spot_node_runtime_t runtime,
  const actor_ref_t &actor_ref,
  const node_rid_t &target_node_rid,
  const zlink::message_t &payload)
{
    auto native_node = runtime.native_node ();
    if (!native_node) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::spot_route_not_found,
                                                      "SPOT node is not running");
    }
    try {
        auto request = payload;
        auto joined =
          std::move (native_node
                       ->join_actor_entry_spot (
                         zlink::service::spot_node_t::remote_actor_ref (
                           zlink::routing_id_t::from (std::string (actor_ref.node_rid ().value ())),
                           std::string (actor_ref.actor_id ())),
                         zlink::routing_id_t::from (std::string (target_node_rid.value ())), request)
                       .flags (static_cast<int> (zlink::send_flags_t::none)))
            .async ()
            .get ();
        return actor_join_reply_from_native (joined.result, joined.join_result_code, joined.actor,
                                             actor_ref, joined.reply_parts,
                                             "remote entry SPOT actor join");
    }
    catch (const framework_exception_t &error) {
        return result_t<actor_join_reply_t>::failure (error.kind (), error.what (),
                                                      error.is_retriable ());
    }
    catch (const zlink::request_error_t &error) {
        return result_t<actor_join_reply_t>::failure (request_result_error_kind (error.result ()),
                                                      error.what ());
    }
    catch (const std::exception &error) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::request_failed,
                                                      error.what ());
    }
}

runtime::messaging::message_parts_t make_actor_mesh_parts (
  const stream_header_t &header,
  const zlink::message_t &payload,
  const spot_actor_message_metadata_t &metadata)
{
    runtime::messaging::client_call_codec_t codec;
    auto envelope =
      codec.create_envelope (header.kind () == stream_message_kind_t::send
                                ? runtime::messaging::message_kind_t::command
                                : runtime::messaging::message_kind_t::request,
                              "actor", std::string (header.packet_name ()));
    envelope.content_type = metadata.content_type;
    envelope.metadata = metadata.values;
    runtime::messaging::envelope_codec_t envelope_codec;
    return envelope_codec.encode_raw_body_parts (envelope, payload);
}

result_t<std::optional<zlink::message_t>> relay_actor_packet_to_remote_actor_mesh (
  spot_node_runtime_t runtime,
  const actor_ref_t &actor_ref,
  const stream_header_t &header,
  const zlink::message_t &payload,
  const spot_actor_message_metadata_t &metadata)
{
    auto native_node = runtime.native_node ();
    if (!native_node) {
        return result_t<std::optional<zlink::message_t>>::failure (
          framework_error_kind_t::spot_route_not_found, "SPOT node is not running");
    }
    try {
        auto parts = make_actor_mesh_parts (header, payload, metadata).items ();
        if (parts.empty ()) {
            return result_t<std::optional<zlink::message_t>>::failure (
              framework_error_kind_t::request_protocol_error,
              "remote actor mesh relay requires at least one message part");
        }
        auto native_actor = zlink::service::spot_node_t::remote_actor_ref (
          zlink::routing_id_t::from (std::string (actor_ref.node_rid ().value ())),
          std::string (actor_ref.actor_id ()));
        auto iterator = parts.begin ();
        if (header.kind () == stream_message_kind_t::send) {
            auto submit = native_node->send_to_actor (native_actor).message (*iterator);
            ++iterator;
            for (; iterator != parts.end (); ++iterator) {
                submit = std::move (submit).message (*iterator);
            }
            if (!std::move (submit).submit ()) {
                return result_t<std::optional<zlink::message_t>>::failure (
                  framework_error_kind_t::request_failed, "remote actor mesh send was not submitted");
            }
            return result_t<std::optional<zlink::message_t>>::success (std::nullopt);
        }

        auto submit = native_node->request_to_actor (native_actor).message (*iterator);
        ++iterator;
        for (; iterator != parts.end (); ++iterator) {
            submit = std::move (submit).message (*iterator);
        }
        auto reply =
          runtime::messaging::message_parts_t (std::move (std::move (submit).async ().get ()));
        runtime::messaging::envelope_codec_t envelope;
        auto reply_header = envelope.decode_header (reply);
        if (!reply_header) {
            return result_t<std::optional<zlink::message_t>>::failure (
              reply_header.error_kind (),
              reply_header.error () ? reply_header.error ()->what ()
                                    : "remote actor mesh reply header decode failed");
        }
        if (reply_header.value ().kind == runtime::messaging::message_kind_t::error) {
            return result_t<std::optional<zlink::message_t>>::failure (
              framework_error_kind_t::request_failed,
              reply_header.value ().error_message.value_or ("remote actor mesh request failed"));
        }
        auto body = envelope.decode_body (reply);
        if (!body) {
            return result_t<std::optional<zlink::message_t>>::failure (
              body.error_kind (),
              body.error () ? body.error ()->what () : "remote actor mesh reply body missing");
        }
        return result_t<std::optional<zlink::message_t>>::success (body.value ());
    }
    catch (const framework_exception_t &error) {
        return result_t<std::optional<zlink::message_t>>::failure (
          error.kind (), error.what (), error.is_retriable ());
    }
    catch (const zlink::request_error_t &error) {
        return result_t<std::optional<zlink::message_t>>::failure (
          request_result_error_kind (error.result ()), error.what ());
    }
    catch (const std::exception &error) {
        return result_t<std::optional<zlink::message_t>>::failure (
          framework_error_kind_t::request_failed, error.what ());
    }
}

result_t<actor_join_reply_t>
join_actor_to_spot_through_route (spot_node_runtime_t runtime,
                                  actor_gateway_runtime_t actor_gateway,
                                  route_client_t route_client,
                                  std::string local_spot_node_rid,
                                  std::optional<std::string> route_channel_name,
                                  bool accepts_route_channels,
                                  const actor_ref_t &actor_ref,
                                  spot_rid_t spot_rid,
                                  const zlink::message_t &payload,
                                  serializer_registry_t &serializers)
{
    (void) actor_gateway;
    (void) route_client;
    (void) route_channel_name;
    auto route = runtime.resolve_spot (spot_rid);
    if (!route) {
        if (rid_targets_node (spot_rid.value (), local_spot_node_rid)) {
            return runtime.join_actor_to_spot_erased (actor_ref, std::move (spot_rid), payload);
        }
        if (accepts_route_channels || (route_channel_name && !route_channel_name->empty ())) {
            return result_t<actor_join_reply_t>::failure (
              framework_error_kind_t::spot_route_not_found, "remote SPOT route was not resolved");
        }
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::spot_route_not_found,
                                                      "SPOT route was not resolved");
    }
    if (route->node_rid.empty () || route->node_rid.value () == local_spot_node_rid) {
        return runtime.join_actor_to_spot_erased (actor_ref, std::move (spot_rid), payload);
    }
    auto joined = join_actor_to_remote_spot_route_mesh (
      runtime, actor_ref, route->node_rid, route->spot_rid, route->spot_rid, payload, std::nullopt,
      serializers);
    if (!joined) {
        return joined;
    }
    runtime.record_actor_route (joined.value ().actor,
                                spot_route_t{route->node_rid, spot_rid, route->spot_name});
    return joined;
}

result_t<actor_join_reply_t> join_actor_to_entry_spot_through_route (
  spot_node_runtime_t runtime,
  route_client_t route_client,
  std::string local_spot_node_rid,
  std::optional<std::string> route_channel_name,
  const actor_ref_t &actor_ref,
  node_rid_t target_node_rid,
  const zlink::message_t &payload,
  const std::optional<zlink::message_t> &actor_snapshot,
  serializer_registry_t &serializers)
{
    if (target_node_rid.empty () || target_node_rid.value () == local_spot_node_rid) {
        return runtime.join_actor_to_entry_spot_erased (actor_ref, std::move (target_node_rid),
                                                        payload, actor_snapshot);
    }
    (void) route_client;
    (void) route_channel_name;
    return join_actor_to_remote_spot_route_mesh (
      runtime, actor_ref, target_node_rid,
      spot_rid_t::from_string (std::string (target_node_rid.value ())),
      spot_rid_t{}, payload, actor_snapshot, serializers);
}

result_t<std::optional<zlink::message_t>>
relay_actor_packet_through_route (spot_node_runtime_t runtime,
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
    (void) actor_gateway;
    (void) route_client;
    (void) route_channel_name;
    const auto send_remote =
      [&] (const spot_route_t &route,
           const spot_rid_t &spot_rid) -> result_t<std::optional<zlink::message_t>> {
        auto relayed =
          relay_actor_packet_to_remote_actor_mesh (runtime, actor_ref, header, payload, metadata);
        if (relayed) {
            runtime.record_actor_route (actor_ref,
                                        spot_route_t{route.node_rid, spot_rid, route.spot_name});
        }
        return relayed;
    };

    auto route = runtime.actor_route (actor_ref);
    if (route && !route->node_rid.empty ()
        && route->node_rid.value () != runtime.node_rid ().value ()) {
        return send_remote (*route, route->spot_rid);
    }

    auto local =
      runtime.relay_actor_packet (actor_ref, actor_context, header.kind (), header.packet_name (),
                                  payload, provider, serializers, metadata);
    if (local
        || (local.error_kind () != framework_error_kind_t::spot_route_not_found
            && local.error_kind () != framework_error_kind_t::actor_route_not_found)) {
        return local;
    }
    const auto spot_rid = runtime.actor_spot (actor_ref);
    if (!spot_rid) {
        try {
            auto &resolver = provider.get_required<actor_location_resolver_t> ();
            auto resolved =
              resolver.resolve_actor_spot_address (std::string (actor_ref.actor_id ())).result ();
            if (!resolved) {
                return result_t<std::optional<zlink::message_t>>::failure (
                  resolved.error_kind (),
                  resolved.error () ? resolved.error ()->what () : "actor location resolve failed");
            }
            if (resolved.value ()) {
                auto route =
                  spot_route_t{node_rid_t::from_string (resolved.value ()->node_rid.to_string ()),
                               spot_rid_t::from_string (resolved.value ()->spot_rid.to_string ()),
                               resolved.value ()->mesh_name};
                return send_remote (route, route.spot_rid);
            }
        }
        catch (const framework_exception_t &) {
        }
        if (!actor_ref.node_rid ().empty ()
            && actor_ref.node_rid ().value () != runtime.node_rid ().value ()) {
            return send_remote (spot_route_t{actor_ref.node_rid (), spot_rid_t{}, std::string{}},
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
            return send_remote (spot_route_t{actor_ref.node_rid (), spot_rid_t{}, std::string{}},
                                spot_rid_t{});
        }
        return local;
    }
    return send_remote (*route, *spot_rid);
}

result_t<void>
notify_actor_disconnected_through_route (spot_node_runtime_t runtime,
                                         route_client_t route_client,
                                         std::string local_spot_node_rid,
                                         std::optional<std::string> route_channel_name,
                                         const actor_ref_t &actor_ref)
{
    const auto send_remote = [&] (std::string target_node_rid) -> result_t<void> {
        if (!route_channel_name || route_channel_name->empty ()) {
            return result_t<void>::failure (framework_error_kind_t::spot_route_not_found,
                                            "remote SPOT route channel is not configured");
        }
        auto reply = route_client
                       .request_to_node (*route_channel_name,
                                         zlink::routing_id_t::from (std::move (target_node_rid)),
                                         make_spot_actor_disconnect_route_request (actor_ref))
                       .packet_name (spot_actor_disconnect_route_request_t::packet_name)
                       .template async<spot_actor_disconnect_route_reply_t> ()
                       .result ();
        if (!reply) {
            return result_t<void>::failure (
              reply.error_kind (),
              reply.error () ? reply.error ()->what () : "remote actor disconnect notify failed");
        }
        return result_t<void>::success ();
    };

    if (!actor_ref.node_rid ().empty () && actor_ref.node_rid ().value () != local_spot_node_rid) {
        return send_remote (std::string (actor_ref.node_rid ().value ()));
    }
    if (auto route = runtime.actor_route (actor_ref);
        route && !route->node_rid.empty () && route->node_rid.value () != local_spot_node_rid) {
        return send_remote (std::string (route->node_rid.value ()));
    }
    return runtime.notify_actor_disconnected_erased (actor_ref);
}

struct actor_gateway_spot_node_binding_t
{
    spot_node_runtime_t runtime;
    route_client_t route_client;
    std::string local_spot_node_rid;
    std::optional<std::string> route_channel_name;
    bool accepts_route_channels = false;
};

bool is_spot_route_miss (framework_error_kind_t kind)
{
    return kind == framework_error_kind_t::spot_route_not_found
           || kind == framework_error_kind_t::actor_route_not_found;
}

std::optional<std::string> default_spot_route_channel (const spot_node_snapshot_t &spot_node)
{
    if (spot_node.spot_route_channel_name && !spot_node.spot_route_channel_name->empty ()) {
        return spot_node.spot_route_channel_name;
    }
    if (spot_node.accepted_route_channels.size () != 1) {
        return std::nullopt;
    }
    return spot_node.accepted_route_channels.front ().channel_name;
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
result_t<actor_join_reply_t>
join_spot_with_target_binding_first (std::vector<actor_gateway_spot_node_binding_t> &bindings,
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

    auto last = result_t<actor_join_reply_t>::failure (framework_error_kind_t::spot_route_not_found,
                                                       "SPOT node route not found");
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
                composite->add (std::make_shared<spot_route_internal_dispatcher_t> (
                  *runtime, actor_gateway, builder.route_client (serializers), serializers));
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
    register_spot_route_packet_serializers (serializers);
    std::vector<actor_gateway_spot_node_binding_t> actor_gateway_spot_nodes;
    actor_gateway_spot_nodes.reserve (spot_node_snapshot.size ());
    for (const auto &spot_node : spot_node_snapshot) {
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
        actor_gateway_spot_nodes.push_back (actor_gateway_spot_node_binding_t{
          *runtime, zlink.route_client (serializers), std::string (runtime->node_rid ().value ()),
          default_spot_route_channel (spot_node), !spot_node.accepted_route_channels.empty ()});
        auto binding = actor_gateway_spot_nodes.back ();
        runtime->on_actor_entry_spot_join (
          [binding, &serializers] (const actor_ref_t &actor_ref, node_rid_t node_rid,
                                   const zlink::message_t &payload,
                                   const std::optional<zlink::message_t> &actor_snapshot) mutable {
              if (!node_rid.empty () && node_rid.value () != binding.local_spot_node_rid) {
                  return join_actor_to_entry_spot_through_route (
                    binding.runtime, binding.route_client, binding.local_spot_node_rid,
                    binding.route_channel_name, actor_ref, node_rid, payload, actor_snapshot,
                    serializers);
              }
              return binding.runtime.join_actor_to_entry_spot_erased (
                actor_ref, std::move (node_rid), payload, actor_snapshot);
          });
    }
    if (actor_gateway_spot_nodes.empty ()) {
        return;
    }

    auto framework_provider = services.build_provider ();
    auto &actor_gateway = framework_provider.get_required<actor_gateway_runtime_t> ();
    actor_gateway.on_join_spot ([bindings = actor_gateway_spot_nodes, actor_gateway,
                                 &serializers] (const actor_ref_t &actor_ref, spot_rid_t spot_rid,
                                                 const zlink::message_t &payload) mutable {
        auto join_local = [&] (actor_gateway_spot_node_binding_t &binding,
                               const spot_rid_t &target_spot_rid) {
            if (!actor_ref.node_rid ().empty ()
                && actor_ref.node_rid ().value () != binding.local_spot_node_rid) {
                return binding.runtime.join_remote_actor_to_spot_erased (
                  actor_ref, target_spot_rid, payload, actor_gateway.actor_context (actor_ref));
            }
            return join_actor_to_spot_through_route (
              binding.runtime, actor_gateway, binding.route_client, binding.local_spot_node_rid,
              binding.route_channel_name, binding.accepts_route_channels, actor_ref,
              target_spot_rid, payload, serializers);
        };
        auto join_fallback = [&] (actor_gateway_spot_node_binding_t &binding,
                                  const spot_rid_t &target_spot_rid) {
            return join_actor_to_spot_through_route (
              binding.runtime, actor_gateway, binding.route_client, binding.local_spot_node_rid,
              binding.route_channel_name, binding.accepts_route_channels, actor_ref,
              target_spot_rid, payload, serializers);
        };
        return join_spot_with_target_binding_first (bindings, spot_rid, join_local, join_fallback);
    });
    actor_gateway.on_join_entry_spot (
      [bindings = actor_gateway_spot_nodes, &serializers] (
        const actor_ref_t &actor_ref, node_rid_t node_rid,
        const zlink::message_t &payload) mutable {
          auto last = result_t<actor_join_reply_t>::failure (
            framework_error_kind_t::spot_route_not_found, "SPOT node route not found");
          for (auto &binding : bindings) {
              if (!node_rid.empty () && node_rid.value () != binding.local_spot_node_rid) {
                  auto joined = join_actor_to_entry_spot_through_route (
                    binding.runtime, binding.route_client, binding.local_spot_node_rid,
                    binding.route_channel_name, actor_ref, node_rid, payload, std::nullopt,
                    serializers);
                  if (joined || !is_spot_route_miss (joined.error_kind ())) {
                      return joined;
                  }
                  last = std::move (joined);
                  continue;
              }
              return binding.runtime.join_actor_to_entry_spot_erased (
                actor_ref, std::move (node_rid), payload);
          }
          return last;
      });
    actor_gateway.on_relay (
      [bindings = actor_gateway_spot_nodes, actor_gateway, services = &services,
       serializers = &serializers] (const actor_ref_t &actor_ref, actor_context_t actor_context,
                                    const stream_header_t &header,
                                    const zlink::message_t &payload) mutable {
          auto relay_with = [&] (actor_gateway_spot_node_binding_t &binding,
                                 actor_context_t context) {
              auto provider = services->build_provider ();
              auto &metadata_policy = provider.get_required<message_metadata_policy_t> ();
              return relay_actor_packet_through_route (
                binding.runtime, actor_gateway, binding.route_client, binding.route_channel_name,
                actor_ref, std::move (context), header, payload, provider, *serializers,
                project_stream_metadata (header, metadata_policy));
          };
          return relay_actor_with_local_binding_first (bindings, actor_ref,
                                                       std::move (actor_context), relay_with);
      });
    actor_gateway.on_disconnect ([bindings = actor_gateway_spot_nodes] (
                                   const actor_ref_t &actor_ref) mutable {
        for (auto &binding : bindings) {
            if (!actor_ref.node_rid ().empty ()
                && actor_ref.node_rid ().value () != binding.local_spot_node_rid) {
                continue;
            }
            return notify_actor_disconnected_through_route (binding.runtime, binding.route_client,
                                                            binding.local_spot_node_rid,
                                                            binding.route_channel_name, actor_ref);
        }
        for (auto &binding : bindings) {
            auto notified = notify_actor_disconnected_through_route (
              binding.runtime, binding.route_client, binding.local_spot_node_rid,
              binding.route_channel_name, actor_ref);
            if (notified
                || (notified.error_kind () != framework_error_kind_t::spot_route_not_found
                    && notified.error_kind () != framework_error_kind_t::actor_route_not_found)) {
                return notified;
            }
        }
        return result_t<void>::failure (framework_error_kind_t::actor_route_not_found,
                                        "actor route not found");
    });
    for (auto &node_binding : actor_gateway_spot_nodes) {
        node_binding.runtime.on_actor_packet_relay (
          [bindings = actor_gateway_spot_nodes, actor_gateway] (
            const actor_ref_t &actor_ref, actor_context_t actor_context,
            stream_message_kind_t message_kind, std::string_view packet_name,
            const zlink::message_t &payload, service_provider_t &services,
            serializer_registry_t &serializers, spot_actor_message_metadata_t metadata) mutable {
              stream_header_t header (message_kind, stream_codec_t::message_pack,
                                      stream_header_flags_t::none, std::nullopt,
                                      std::string (packet_name));
              auto relay_with = [&] (actor_gateway_spot_node_binding_t &binding,
                                     actor_context_t context,
                                     spot_actor_message_metadata_t relay_metadata) {
                  return relay_actor_packet_through_route (
                    binding.runtime, actor_gateway, binding.route_client,
                    binding.route_channel_name, actor_ref, std::move (context), header, payload,
                    services, serializers, std::move (relay_metadata));
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
