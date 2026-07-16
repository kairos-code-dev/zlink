/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/host/actor_gateway_spot_bridge.hpp"

#include "runtime/actors/actor_route_internal_dispatcher.hpp"
#include "runtime/messaging/envelope_codec.hpp"
#include "runtime/messaging/client_call_codec.hpp"
#include "runtime/spots/spot_route_internal_dispatcher.hpp"
#include "runtime/spots/spot_route_packets.hpp"
#include "runtime/locations/spot_handle_state.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <zlink.hpp>
#include <zlink/framework/contracts/locations/resolvers.hpp>

#include <condition_variable>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

namespace zlink::framework::detail
{

namespace
{

constexpr std::string_view actor_relay_kind_metadata_key = "__zlink.actorRelayKind";
constexpr std::string_view actor_relay_kind_send = "send";
constexpr std::string_view actor_relay_kind_request = "request";

void trace_actor_transfer (std::string_view stage,
                           const actor_ref_t &actor_ref,
                           const node_rid_t &target_node_rid = {},
                           const spot_rid_t &target_spot_rid = {})
{
    const auto *enabled = std::getenv ("ZLINK_CPP_AUTO_CONNECT_TRACE");
    if (enabled == nullptr || *enabled == '\0') {
        return;
    }
    std::cerr << "zlink actor-transfer stage=" << stage
              << " actor=" << actor_ref.actor_id ()
              << " generation=" << actor_ref.generation ()
              << " sourceNode=" << actor_ref.node_rid ().value ()
              << " targetNode=" << target_node_rid.value ()
              << " targetSpot=" << target_spot_rid.value () << '\n';
}

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

framework_exception_t request_result_error (zlink::request_result_t result, std::string message)
{
    switch (result) {
        case zlink::request_result_t::timed_out:
            return detail::make_boundary_exception (detail::boundary_error_t::timed_out,
                                                    std::move (message));
        case zlink::request_result_t::not_connected:
            return framework_exception_t (framework_error_kind_t::route_not_connected,
                                          std::move (message));
        default:
            return framework_exception_t (framework_error_kind_t::request_failed,
                                          std::move (message));
    }
}

result_t<actor_join_reply_t>
actor_join_reply_from_native (zlink::request_result_t result,
                              int join_result_code,
                              const zlink::actor_ref_t &native_actor_ref,
                              const actor_ref_t &fallback_actor_ref,
                              const std::vector<zlink::message_t> &reply_parts,
                              std::string_view operation)
{
    if (result != zlink::request_result_t::ok) {
        return detail::result_access_t::failure<actor_join_reply_t> (
          request_result_error (result, std::string (operation) + " failed"));
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

result_t<actor_join_reply_t>
wait_native_actor_join (zlink::service::actor_join_callback_submit_operation_t submit,
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
          [state] (const zlink::actor_join_result_t &result, std::vector<zlink::message_t> parts) {
              {
                  std::lock_guard lock (state->mutex);
                  state->result = result;
                  state->parts = std::move (parts);
                  state->completed = true;
              }
              state->changed.notify_all ();
          });
        if (!submitted) {
            return result_t<actor_join_reply_t>::failure (framework_error_kind_t::request_failed,
                                                          std::string (operation)
                                                            + " was not submitted");
        }
        std::unique_lock lock (state->mutex);
        state->changed.wait (lock, [&] { return state->completed; });
        return actor_join_reply_from_native (state->result.result, state->result.join_result_code,
                                             state->result.actor, fallback_actor_ref, state->parts,
                                             operation);
    }
    catch (const framework_exception_t &error) {
        return detail::result_access_t::failure<actor_join_reply_t> (error);
    }
    catch (const zlink::request_error_t &error) {
        return detail::result_access_t::failure<actor_join_reply_t> (
          request_result_error (error.result (), error.what ()));
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
    auto stopping_failure = [] {
        return detail::boundary_failure<runtime::messaging::message_parts_t> (detail::boundary_error_t::shutdown, "SPOT node is stopping");
    };
    if (runtime.stopping ()) {
        return stopping_failure ();
    }
    try {
        const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (30);
        bool routed_peer_ready = false;
        while (std::chrono::steady_clock::now () < deadline) {
            if (runtime.stopping ()) {
                return stopping_failure ();
            }
            const auto status = native_node->status ();
            if (status.connected_peer_count () > 0
                && status.disconnected_routed_target_count () == 0) {
                routed_peer_ready = true;
                break;
            }
            if (status.disconnected_routed_target_count () > 0) {
                return result_t<runtime::messaging::message_parts_t>::failure (
                  framework_error_kind_t::route_not_connected,
                  "SPOT route peer has a disconnected routed target");
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
        if (!routed_peer_ready) {
            return result_t<runtime::messaging::message_parts_t>::failure (
              framework_error_kind_t::route_not_connected, "SPOT route peer is not ready");
        }
        trace_actor_transfer ("mesh-ready", actor_ref_t{}, target_node_rid, target_spot_rid);
        auto native_parts = parts.items ();
        if (native_parts.empty ()) {
            return result_t<runtime::messaging::message_parts_t>::failure (
              framework_error_kind_t::request_protocol_error,
              "remote SPOT mesh request requires at least one message part");
        }
        if (runtime.stopping ()) {
            return stopping_failure ();
        }
        auto origin_spot =
          native_node
            ->get_or_create_spot (zlink::routing_id_t::from (
              std::string (runtime.node_rid ().value ()) + ":__zlink-route-origin"))
            .first;
        auto iterator = native_parts.begin ();
        auto submit =
          origin_spot
            .request_to_spot (zlink::routing_id_t::from (std::string (target_node_rid.value ())),
                              zlink::routing_id_t::from (std::string (target_spot_rid.value ())))
            .message (*iterator);
        ++iterator;
        for (; iterator != native_parts.end (); ++iterator) {
            submit = std::move (submit).message (*iterator);
        }
        auto pending = std::move (submit).timeout (std::chrono::seconds (30)).async ();
        trace_actor_transfer ("mesh-submitted", actor_ref_t{}, target_node_rid, target_spot_rid);
        while (pending.wait_for (std::chrono::milliseconds (10)) != std::future_status::ready) {
            if (runtime.stopping ()) {
                return stopping_failure ();
            }
        }
        trace_actor_transfer ("mesh-completed", actor_ref_t{}, target_node_rid, target_spot_rid);
        auto reply = pending.get ();
        return result_t<runtime::messaging::message_parts_t>::success (
          runtime::messaging::message_parts_t (std::move (reply)));
    }
    catch (const framework_exception_t &error) {
        return detail::result_access_t::failure<runtime::messaging::message_parts_t> (error);
    }
    catch (const zlink::request_error_t &error) {
        return detail::result_access_t::failure<runtime::messaging::message_parts_t> (
          request_result_error (error.result (), error.what ()));
    }
    catch (const std::exception &error) {
        return result_t<runtime::messaging::message_parts_t>::failure (
          framework_error_kind_t::request_failed, error.what ());
    }
}

result_t<actor_join_reply_t>
join_actor_to_remote_spot_route_mesh (spot_node_runtime_t runtime,
                                      const actor_ref_t &actor_ref,
                                      const node_rid_t &target_node_rid,
                                      const spot_rid_t &target_delivery_spot_rid,
                                      const spot_rid_t &target_join_spot_rid,
                                      const zlink::message_t &payload,
                                      const std::optional<zlink::message_t> &actor_snapshot,
                                      serializer_registry_t &serializers)
{
    runtime::messaging::client_call_codec_t codec;
    auto header = codec.create_envelope (runtime::messaging::message_kind_t::request, "spot",
                                         std::string (spot_actor_join_route_request_t::packet_name),
                                         std::chrono::seconds (30));
    auto request =
      make_spot_actor_join_route_request (actor_ref, target_join_spot_rid, payload, actor_snapshot);
    auto parts = codec.encode_envelope_parts (header, request, serializers);
    auto reply_parts = request_spot_mesh_parts (runtime, target_node_rid, target_delivery_spot_rid,
                                                std::move (parts));
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

result_t<void>
register_bound_session_route_through_mesh (spot_node_runtime_t runtime,
                                           const actor_ref_t &actor_ref,
                                           std::string session_node_rid,
                                           serializer_registry_t &serializers)
{
    runtime::messaging::client_call_codec_t codec;
    auto header = codec.create_envelope (
      runtime::messaging::message_kind_t::request, "spot",
      std::string (actor_bound_session_bind_route_request_t::packet_name),
      std::chrono::seconds (30));
    auto request = actor_bound_session_bind_route_request_t{
      .actor_node_rid = std::string (actor_ref.node_rid ().value ()),
      .actor_type = std::string (actor_ref.actor_type ()),
      .actor_id = std::string (actor_ref.actor_id ()),
      .actor_generation = actor_ref.generation (),
      .session_node_rid = std::move (session_node_rid)};
    auto parts = codec.encode_envelope_parts (header, request, serializers);
    const auto target_node_rid = actor_ref.node_rid ();
    auto reply_parts = request_spot_mesh_parts (
      runtime, target_node_rid,
      spot_rid_t::from_string (std::string (target_node_rid.value ())), std::move (parts));
    if (!reply_parts) {
        return detail::propagate_failure<void> (reply_parts,
                                                "bound session route registration failed");
    }
    auto decoded = codec.decode_envelope_reply<actor_bound_session_route_reply_t> (
      reply_parts.value (), serializers, "bound session route reply is empty",
      "bound session route reply decode failed", "BindActorSession");
    if (!decoded) {
        return detail::propagate_failure<void> (decoded,
                                                "bound session route registration failed");
    }
    return decoded.value ().accepted
             ? result_t<void>::success ()
             : result_t<void>::failure (framework_error_kind_t::actor_session_not_bound,
                                        "bound session route registration was rejected");
}

result_t<actor_join_reply_t>
join_actor_to_remote_spot_route_channel (route_client_t route_client,
                                         const std::optional<std::string> &route_channel_name,
                                         const actor_ref_t &actor_ref,
                                         const node_rid_t &target_node_rid,
                                         const spot_rid_t &target_delivery_spot_rid,
                                         const spot_rid_t &target_join_spot_rid,
                                         const zlink::message_t &payload,
                                         const std::optional<zlink::message_t> &actor_snapshot)
{
    if (!route_channel_name || route_channel_name->empty ()) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::spot_route_not_found,
                                                      "SPOT route channel is not configured");
    }
    auto request =
      make_spot_actor_join_route_request (actor_ref, target_join_spot_rid, payload, actor_snapshot);
    auto target = runtime::make_fixed_spot_handle (runtime::spot_address_t{
      *route_channel_name, zlink::routing_id_t::from (std::string (target_node_rid.value ())),
      zlink::routing_id_t::from (std::string (target_delivery_spot_rid.value ()))});
    auto reply = route_client.request_to_spot (std::move (target), std::move (request))
                   .timeout (std::chrono::seconds (30))
                   .async<spot_actor_join_route_reply_t> ()
                   .result ();
    if (!reply) {
        const auto *error = reply.error ();
        return result_t<actor_join_reply_t>::failure (
          reply.error_kind (), error != nullptr ? error->what () : "remote SPOT route join failed",
          error != nullptr && error->is_retriable ());
    }
    return result_t<actor_join_reply_t>::success (
      actor_join_reply_from_spot_route (reply.value ()));
}

result_t<spot_actor_admission_route_reply_t>
request_remote_actor_admission (spot_node_runtime_t runtime,
                                route_client_t route_client,
                                const std::optional<std::string> &route_channel_name,
                                const node_rid_t &target_node_rid,
                                const spot_rid_t &target_spot_rid,
                                spot_actor_admission_route_request_t request,
                                serializer_registry_t &serializers)
{
    if (route_channel_name && !route_channel_name->empty ()) {
        auto target = runtime::make_fixed_spot_handle (runtime::spot_address_t{
          *route_channel_name, zlink::routing_id_t::from (std::string (target_node_rid.value ())),
          zlink::routing_id_t::from (std::string (target_spot_rid.value ()))});
        return route_client.request_to_spot (std::move (target), std::move (request))
          .timeout (std::chrono::seconds (30))
          .async<spot_actor_admission_route_reply_t> ()
          .result ();
    }

    runtime::messaging::client_call_codec_t codec;
    auto header = codec.create_envelope (runtime::messaging::message_kind_t::request, "spot",
                                         spot_actor_admission_route_request_t::packet_name,
                                         std::chrono::seconds (30));
    auto parts = codec.encode_envelope_parts (header, request, serializers);
    auto reply_parts = request_spot_mesh_parts (
      runtime, target_node_rid,
      spot_rid_t::from_string (std::string (target_node_rid.value ())), std::move (parts));
    if (!reply_parts) {
        return detail::propagate_failure<spot_actor_admission_route_reply_t> (reply_parts, "remote actor admission failed");
    }
    return codec.decode_envelope_reply<spot_actor_admission_route_reply_t> (
      reply_parts.value (), serializers, "remote actor admission reply is empty",
      "remote actor admission reply decode failed", "ActorTransferAdmission");
}

result_t<actor_join_reply_t>
request_remote_actor_commit (spot_node_runtime_t runtime,
                             route_client_t route_client,
                             const std::optional<std::string> &route_channel_name,
                             const node_rid_t &target_node_rid,
                             const spot_rid_t &target_spot_rid,
                             spot_actor_commit_route_request_t request,
                             serializer_registry_t &serializers)
{
    if (route_channel_name && !route_channel_name->empty ()) {
        auto target = runtime::make_fixed_spot_handle (runtime::spot_address_t{
          *route_channel_name, zlink::routing_id_t::from (std::string (target_node_rid.value ())),
          zlink::routing_id_t::from (std::string (target_spot_rid.value ()))});
        auto reply = route_client.request_to_spot (std::move (target), std::move (request))
                       .timeout (std::chrono::seconds (30))
                       .async<spot_actor_join_route_reply_t> ()
                       .result ();
        if (!reply) {
            return detail::propagate_failure<actor_join_reply_t> (reply, "remote actor commit failed");
        }
        return result_t<actor_join_reply_t>::success (
          actor_join_reply_from_spot_route (reply.value ()));
    }

    runtime::messaging::client_call_codec_t codec;
    auto header = codec.create_envelope (runtime::messaging::message_kind_t::request, "spot",
                                         spot_actor_commit_route_request_t::packet_name,
                                         std::chrono::seconds (30));
    auto parts = codec.encode_envelope_parts (header, request, serializers);
    auto reply_parts = request_spot_mesh_parts (
      runtime, target_node_rid,
      spot_rid_t::from_string (std::string (target_node_rid.value ())), std::move (parts));
    if (!reply_parts) {
        return detail::propagate_failure<actor_join_reply_t> (reply_parts, "remote actor commit failed");
    }
    auto decoded = codec.decode_envelope_reply<spot_actor_join_route_reply_t> (
      reply_parts.value (), serializers, "remote actor commit reply is empty",
      "remote actor commit reply decode failed", "ActorTransferCommit");
    if (!decoded) {
        return detail::propagate_failure<actor_join_reply_t> (decoded, "remote actor commit failed");
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

result_t<actor_join_reply_t>
join_actor_to_remote_entry_spot_mesh (spot_node_runtime_t runtime,
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
                         zlink::routing_id_t::from (std::string (target_node_rid.value ())),
                         request)
                       .flags (static_cast<int> (zlink::send_flags_t::none)))
            .async ()
            .get ();
        return actor_join_reply_from_native (joined.result, joined.join_result_code, joined.actor,
                                             actor_ref, joined.reply_parts,
                                             "remote entry SPOT actor join");
    }
    catch (const framework_exception_t &error) {
        return detail::result_access_t::failure<actor_join_reply_t> (error);
    }
    catch (const zlink::request_error_t &error) {
        return detail::result_access_t::failure<actor_join_reply_t> (
          request_result_error (error.result (), error.what ()));
    }
    catch (const std::exception &error) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::request_failed,
                                                      error.what ());
    }
}

runtime::messaging::message_parts_t
make_actor_mesh_parts (const stream_header_t &header,
                       const zlink::message_t &payload,
                       const spot_actor_message_metadata_t &metadata)
{
    runtime::messaging::client_call_codec_t codec;
    auto envelope = codec.create_envelope (header.kind () == stream_message_kind_t::send
                                             ? runtime::messaging::message_kind_t::command
                                             : runtime::messaging::message_kind_t::request,
                                           "actor", std::string (header.packet_name ()));
    envelope.content_type = metadata.content_type;
    envelope.metadata = metadata.values;
    runtime::messaging::envelope_codec_t envelope_codec;
    return envelope_codec.encode_raw_body_parts (envelope, payload);
}

result_t<std::optional<zlink::message_t>>
relay_actor_packet_to_remote_actor_mesh (spot_node_runtime_t runtime,
                                         actor_gateway_runtime_t actor_gateway,
                                         const actor_ref_t &actor_ref,
                                         const node_rid_t &target_node_rid,
                                         const spot_rid_t &target_spot_rid,
                                         const stream_header_t &header,
                                         const zlink::message_t &payload,
                                         const spot_actor_message_metadata_t &metadata,
                                         serializer_registry_t &serializers)
{
    try {
        auto route_metadata = metadata;
        if (header.kind () == stream_message_kind_t::send) {
            route_metadata.values["__zlink.actorRelayKind"] = "send";
        }
        runtime::messaging::client_call_codec_t codec;
        auto request_header = codec.create_envelope (
          runtime::messaging::message_kind_t::request, "spot",
          std::string (spot_actor_packet_route_request_t::packet_name), std::chrono::seconds (30));
        auto request = make_spot_actor_packet_route_request (
          actor_ref, target_spot_rid, header.packet_name (), payload, route_metadata);
        auto request_parts = codec.encode_envelope_parts (request_header, request, serializers);
        auto reply_parts = request_spot_mesh_parts (runtime, target_node_rid, target_spot_rid,
                                                    std::move (request_parts));
        if (!reply_parts) {
            const auto *error = reply_parts.error ();
            return result_t<std::optional<zlink::message_t>>::failure (
              reply_parts.error_kind (),
              error != nullptr ? error->what () : "remote actor mesh request failed",
              error != nullptr && error->is_retriable ());
        }
        auto decoded = codec.decode_envelope_reply<spot_actor_packet_route_reply_t> (
          reply_parts.value (), serializers, "remote actor mesh reply is empty",
          "remote actor mesh reply decode failed", std::string (header.packet_name ()));
        if (!decoded) {
            const auto *error = decoded.error ();
            return result_t<std::optional<zlink::message_t>>::failure (
              decoded.error_kind (),
              error != nullptr ? error->what () : "remote actor mesh request failed",
              error != nullptr && error->is_retriable ());
        }
        if (decoded.value ().actor_ref_present) {
            auto updated = actor_gateway.update_actor_ref (actor_ref_t (
              node_rid_t::from_string (decoded.value ().actor_node_rid),
              decoded.value ().actor_type, decoded.value ().actor_id,
              decoded.value ().actor_generation));
            if (!updated) {
                return detail::propagate_failure<std::optional<zlink::message_t>> (updated, "actor ref update failed");
            }
        }
        return result_t<std::optional<zlink::message_t>>::success (
          decoded.value ().has_reply
            ? std::make_optional (zlink::message_t::from (decoded.value ().payload))
            : std::nullopt);
    }
    catch (const framework_exception_t &error) {
        return detail::result_access_t::failure<std::optional<zlink::message_t>> (error);
    }
    catch (const zlink::request_error_t &error) {
        return detail::result_access_t::failure<std::optional<zlink::message_t>> (
          request_result_error (error.result (), error.what ()));
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
    trace_actor_transfer ("resolve-start", actor_ref, {}, spot_rid);
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
    trace_actor_transfer ("resolved", actor_ref, route->node_rid, route->spot_rid);
    if ((!route_channel_name || route_channel_name->empty ()) && !runtime.native_node ()) {
        return result_t<actor_join_reply_t>::failure (
          framework_error_kind_t::spot_route_not_found,
          "remote SPOT route transport is not configured");
    }
    const auto source_spot = runtime.actor_spot (actor_ref);
    if (!source_spot) {
        return result_t<actor_join_reply_t>::failure (framework_error_kind_t::actor_route_not_found,
                                                      "source actor is not joined to a local spot");
    }
    const auto transfer_id = runtime.next_actor_transfer_id ();
    trace_actor_transfer ("admission-start", actor_ref, route->node_rid, route->spot_rid);
    auto admitted = request_remote_actor_admission (
      runtime, route_client, route_channel_name, route->node_rid, route->spot_rid,
      spot_actor_admission_route_request_t{
        .transfer_id = transfer_id,
        .actor_node_rid = std::string (actor_ref.node_rid ().value ()),
        .actor_type = std::string (actor_ref.actor_type ()),
        .actor_id = std::string (actor_ref.actor_id ()),
        .actor_generation = actor_ref.generation (),
        .source_spot_rid = std::string (source_spot->value ()),
        .target_spot_rid = std::string (route->spot_rid.value ()),
        .payload = payload.to_bytes ()},
      serializers);
    trace_actor_transfer ("admission-completed", actor_ref, route->node_rid, route->spot_rid);
    if (!admitted) {
        return detail::propagate_failure<actor_join_reply_t> (admitted, "remote actor admission failed");
    }
    if (!admitted.value ().accepted) {
        return result_t<actor_join_reply_t>::success (
          actor_join_reply_t{1, actor_ref, zlink::message_t::from (admitted.value ().payload)});
    }

    trace_actor_transfer ("transfer-out-start", actor_ref, route->node_rid, route->spot_rid);
    auto transfer = runtime.transfer_actor_out (actor_ref, transfer_id);
    if (!transfer) {
        return detail::propagate_failure<actor_join_reply_t> (transfer, "actor transfer-out failed");
    }
    auto left = runtime.leave_actor_for_remote_transfer (actor_ref);
    if (!left) {
        return detail::propagate_failure<actor_join_reply_t> (left, "source actor leave failed");
    }
    const auto bound_session = actor_gateway.bound_session_route (actor_ref);
    // In-flight handoff (§10.2-2): the packets preserved while the actor was
    // moving travel with the commit so the target can replay them before it
    // publishes the committed location.
    auto handoff_backlog = runtime.take_actor_handoff_backlog (actor_ref);
    std::vector<spot_actor_handoff_packet_t> wire_backlog;
    wire_backlog.reserve (handoff_backlog.size ());
    for (auto &packet : handoff_backlog) {
        wire_backlog.push_back (spot_actor_handoff_packet_t{
          std::move (packet.packet_name), std::move (packet.payload),
          std::move (packet.content_type), std::move (packet.metadata), packet.is_request});
    }
    trace_actor_transfer ("commit-start", actor_ref, route->node_rid, route->spot_rid);
    runtime.emit_actor_transfer_marker ("commit_request", actor_ref, transfer_id,
                                        route->spot_rid);
    auto joined = request_remote_actor_commit (
      runtime, route_client, route_channel_name, route->node_rid, route->spot_rid,
      spot_actor_commit_route_request_t{
        .transfer_id = transfer_id,
        .actor_node_rid = std::string (actor_ref.node_rid ().value ()),
        .actor_type = std::string (actor_ref.actor_type ()),
        .actor_id = std::string (actor_ref.actor_id ()),
        .actor_generation = actor_ref.generation (),
        .target_spot_rid = std::string (route->spot_rid.value ()),
        .bound_session_node_rid =
          bound_session ? bound_session->node_rid.to_string () : std::string{},
        .bound_session_rid =
          bound_session && bound_session->session_rid
            ? bound_session->session_rid->to_string ()
            : std::string{},
        .transfer_state = transfer.value ().state.to_bytes (),
        .handoff_backlog = {},
        .prepare = true,
        .finalize = false},
      serializers);
    trace_actor_transfer ("commit-completed", actor_ref, route->node_rid, route->spot_rid);
    if (!joined) {
        runtime.fail_remote_actor_transfer (actor_ref, true);
        return joined;
    }
    if (joined.value ().result_code != 0) {
        runtime.fail_remote_actor_transfer (actor_ref, true);
        return joined;
    }
    // The prepare RPC completes target materialization and OnJoinedActor but
    // deliberately keeps the new location private. Packets that raced that RPC
    // are appended after the earlier snapshot, then the finalize RPC enqueues
    // the complete ordered backlog before it publishes the target location.
    auto late_backlog = runtime.take_actor_handoff_backlog (actor_ref);
    wire_backlog.reserve (wire_backlog.size () + late_backlog.size ());
    for (auto &packet : late_backlog) {
        wire_backlog.push_back (spot_actor_handoff_packet_t{
          std::move (packet.packet_name), std::move (packet.payload),
          std::move (packet.content_type), std::move (packet.metadata), packet.is_request});
    }
    auto finalized = request_remote_actor_commit (
      runtime, route_client, route_channel_name, route->node_rid, route->spot_rid,
      spot_actor_commit_route_request_t{
        .transfer_id = transfer_id,
        .actor_node_rid = std::string (actor_ref.node_rid ().value ()),
        .actor_type = std::string (actor_ref.actor_type ()),
        .actor_id = std::string (actor_ref.actor_id ()),
        .actor_generation = actor_ref.generation (),
        .target_spot_rid = std::string (route->spot_rid.value ()),
        .bound_session_node_rid =
          bound_session ? bound_session->node_rid.to_string () : std::string{},
        .bound_session_rid =
          bound_session && bound_session->session_rid
            ? bound_session->session_rid->to_string ()
            : std::string{},
        .transfer_state = {},
        .handoff_backlog = std::move (wire_backlog),
        .prepare = false,
        .finalize = true},
      serializers);
    if (!finalized || finalized.value ().result_code != 0) {
        runtime.fail_remote_actor_transfer (actor_ref, true);
        return finalized;
    }
    runtime.emit_actor_transfer_marker ("commit_ack", actor_ref, transfer_id,
                                        route->spot_rid);
    runtime.complete_remote_actor_transfer (
      actor_ref, finalized.value ().actor,
      spot_route_t{route->node_rid, spot_rid, route->spot_name},
      transfer_id);
    runtime.emit_actor_transfer_marker ("forwarding_entry", actor_ref, transfer_id,
                                        route->spot_rid, route->node_rid);
    return result_t<actor_join_reply_t>::success (
      actor_join_reply_t{finalized.value ().result_code, finalized.value ().actor,
                         zlink::message_t::from (admitted.value ().payload)});
}

result_t<actor_join_reply_t>
join_actor_to_entry_spot_through_route (spot_node_runtime_t runtime,
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
    if (route_channel_name) {
        return join_actor_to_remote_spot_route_channel (
          std::move (route_client), route_channel_name, actor_ref, target_node_rid,
          spot_rid_t::from_string (std::string (target_node_rid.value ())), spot_rid_t{}, payload,
          actor_snapshot);
    }
    return join_actor_to_remote_spot_route_mesh (
      runtime, actor_ref, target_node_rid,
      spot_rid_t::from_string (std::string (target_node_rid.value ())), spot_rid_t{}, payload,
      actor_snapshot, serializers);
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
    (void) route_client;
    (void) route_channel_name;
    const auto send_remote =
      [&] (const spot_route_t &route,
           const spot_rid_t &spot_rid,
           const actor_ref_t &routed_actor) -> result_t<std::optional<zlink::message_t>> {
        auto relayed = relay_actor_packet_to_remote_actor_mesh (
          runtime, actor_gateway, routed_actor, route.node_rid, spot_rid, header, payload, metadata,
          serializers);
        if (relayed) {
            runtime.record_actor_route (routed_actor,
                                        spot_route_t{route.node_rid, spot_rid, route.spot_name});
        }
        return relayed;
    };

    if (const auto forwarding = runtime.actor_forwarding_target (actor_ref)) {
        runtime.emit_actor_transfer_marker (
          "straggler_forward", actor_ref,
          std::string (actor_ref.actor_type ()) + ":" + std::string (actor_ref.actor_id ()),
          forwarding->route.spot_rid, forwarding->route.node_rid);
        return send_remote (forwarding->route, forwarding->route.spot_rid,
                            forwarding->actor);
    }

    auto route = runtime.actor_route (actor_ref);
    if (route && !route->node_rid.empty ()
        && route->node_rid.value () != runtime.node_rid ().value ()) {
        if (const auto current = runtime.current_actor_ref (actor_ref);
            current && current->generation () > actor_ref.generation ()) {
            runtime.emit_actor_transfer_marker (
              "straggler_forward", actor_ref,
              std::string (actor_ref.actor_type ()) + ":" + std::string (actor_ref.actor_id ()),
              route->spot_rid, route->node_rid);
            const auto forwarded = actor_ref_t (
              route->node_rid, std::string (actor_ref.actor_type ()),
              std::string (actor_ref.actor_id ()), current->generation ());
            return send_remote (*route, route->spot_rid, forwarded);
        }
        return send_remote (*route, route->spot_rid, actor_ref);
    }

    // A relay for an actor whose ref is homed on another node and that has no
    // local placement here must forward to the home node, not materialize a
    // local incarnation in this node's entry spot. Skipping the local relay
    // falls through to the resolver / send_remote path below. This keeps a
    // bound-session relay to a remote actor (spot-actor §9) running on the
    // actor's node rather than the session's node.
    const bool homed_elsewhere =
      !actor_ref.node_rid ().empty ()
      && actor_ref.node_rid ().value () != runtime.node_rid ().value ()
      && !runtime.actor_spot (actor_ref);
    auto local = homed_elsewhere
                   ? result_t<std::optional<zlink::message_t>>::failure (
                       framework_error_kind_t::spot_route_not_found, "actor is homed on another node")
                   : runtime.relay_actor_packet (actor_ref, actor_context, header.kind (),
                                                 header.packet_name (), payload, provider,
                                                 serializers, metadata);
    if (local
        || (local.error_kind () != framework_error_kind_t::spot_route_not_found
            && local.error_kind () != framework_error_kind_t::actor_route_not_found)) {
        return local;
    }
    const auto spot_rid = runtime.actor_spot (actor_ref);
    if (!spot_rid) {
        try {
            auto &resolver = provider.get_required<runtime::actor_address_resolver_t> ();
            auto resolved =
              resolver.resolve_actor_address (std::string (actor_ref.actor_id ())).result ();
            if (!resolved) {
                return detail::propagate_failure<std::optional<zlink::message_t>> (resolved, "actor location resolve failed");
            }
            if (resolved.value ()) {
                auto route =
                  spot_route_t{node_rid_t::from_string (resolved.value ()->node_rid.to_string ()),
                               spot_rid_t::from_string (resolved.value ()->spot_rid.to_string ()),
                               resolved.value ()->mesh_name};
                return send_remote (route, route.spot_rid, actor_ref);
            }
        }
        catch (const framework_exception_t &) {
        }
        if (!actor_ref.node_rid ().empty ()
            && actor_ref.node_rid ().value () != runtime.node_rid ().value ()) {
            return send_remote (spot_route_t{actor_ref.node_rid (), spot_rid_t{}, std::string{}},
                                spot_rid_t{}, actor_ref);
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
                                spot_rid_t{}, actor_ref);
        }
        return local;
    }
    return send_remote (*route, *spot_rid, actor_ref);
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
                       .template async<spot_actor_disconnect_route_reply_t> ()
                       .result ();
        if (!reply) {
            return detail::propagate_failure<void> (reply, "remote actor disconnect notify failed");
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

/* Single owner of the spot-node routing decision (route client, local node
 * identity, default route channel, accepted-channel interpretation): the
 * configure-time wiring and the drain handoff both consume this binding so
 * normal joins and drain moves cannot diverge. */
actor_gateway_spot_node_binding_t
make_actor_gateway_spot_node_binding (zlink_builder_t &zlink,
                                      const spot_node_snapshot_t &spot_node,
                                      spot_node_runtime_t runtime,
                                      serializer_registry_t &serializers)
{
    auto route_client = zlink.route_client (serializers);
    runtime.set_route_client (route_client);
    auto local_rid = std::string (runtime.node_rid ().value ());
    return actor_gateway_spot_node_binding_t{std::move (runtime), std::move (route_client),
                                             std::move (local_rid),
                                             default_spot_route_channel (spot_node),
                                             !spot_node.accepted_route_channels.empty ()};
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

drain_actor_handoff_result_t drain_actors_through_route (zlink_builder_t &zlink,
                                                         service_provider_t &provider,
                                                         serializer_registry_t &serializers)
{
    drain_actor_handoff_result_t outcome;
    auto peers = provider.get<peer_location_resolver_t> ();
    auto actor_gateway = provider.get<actor_gateway_runtime_t> ();
    for (const auto &spot_node : spot_node_runtime_t::snapshots (zlink)) {
        auto runtime = spot_node_runtime_t::from (zlink, spot_node.name);
        if (!runtime) {
            continue;
        }
        const auto actors = runtime->local_actor_refs ();
        if (actors.empty ()) {
            continue;
        }
        if (!peers || !actor_gateway) {
            // No peer resolver or gateway: nothing to select targets from.
            // The actors stay on the source and the shared deadline owns the
            // outcome (§5.3 "no eligible target").
            outcome.completed = false;
            outcome.remaining += actors.size ();
            continue;
        }
        const auto mesh_name = spot_node.discovery_channel_name ? *spot_node.discovery_channel_name
                                                                : spot_node.name;
        std::vector<peer_location_t> mesh_peers;
        try {
            mesh_peers =
              peers->get ()
                .list_live_peers (peer_location_filter_t{
                  .auto_connect_type = location_auto_connect_type_t::spot_mesh,
                  .mesh_name = mesh_name,
                  .role = location_role_t::spot})
                .result ()
                .value ();
        }
        catch (...) {
            outcome.completed = false;
            outcome.remaining += actors.size ();
            continue;
        }
        auto binding =
          make_actor_gateway_spot_node_binding (zlink, spot_node, *runtime, serializers);
        std::size_t next_target = 0;
        for (const auto &actor_ref : actors) {
            const auto capability = "actor:" + std::string (actor_ref.actor_type ());
            std::vector<node_rid_t> eligible;
            for (const auto &peer : mesh_peers) {
                if (peer.draining || !peer.node_rid) {
                    continue;
                }
                const auto peer_rid = peer.node_rid->to_string ();
                if (peer_rid.empty () || peer_rid == binding.local_spot_node_rid) {
                    continue;
                }
                if (std::find (peer.capabilities.begin (), peer.capabilities.end (), capability)
                    == peer.capabilities.end ()) {
                    continue;
                }
                eligible.push_back (node_rid_t::from_string (peer_rid));
            }
            if (eligible.empty ()) {
                outcome.completed = false;
                outcome.remaining++;
                continue;
            }
            // One in-flight handoff at a time is the v1 concurrency bound; a
            // rejected/left peer just advances to the next round-robin target
            // and the next pass retries with a refreshed store view. The
            // general join runs the full admission/transfer/commit transaction
            // toward the target's entry spot (rid == node rid), mirroring the
            // .NET drain handoff.
            bool moved = false;
            for (std::size_t attempt = 0; attempt < eligible.size () && !moved; attempt++) {
                const auto &target = eligible[(next_target + attempt) % eligible.size ()];
                try {
                    auto joined = join_actor_to_spot_through_route (
                      binding.runtime, actor_gateway->get (), binding.route_client,
                      binding.local_spot_node_rid, binding.route_channel_name,
                      binding.accepts_route_channels, actor_ref,
                      spot_rid_t::from_string (std::string (target.value ())),
                      zlink::message_t{}, serializers);
                    moved = joined && joined.value ().result_code == 0;
                }
                catch (...) {
                }
            }
            next_target++;
            if (moved) {
                outcome.moved++;
            } else {
                outcome.completed = false;
                outcome.remaining++;
            }
        }
    }
    return outcome;
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
        actor_gateway_spot_nodes.push_back (
          make_actor_gateway_spot_node_binding (zlink, spot_node, *runtime, serializers));
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
    actor_gateway.on_join_entry_spot ([bindings = actor_gateway_spot_nodes, &serializers] (
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
            return binding.runtime.join_actor_to_entry_spot_erased (actor_ref, std::move (node_rid),
                                                                    payload);
        }
        return last;
    });
    actor_gateway.on_bound_session (
      [bindings = actor_gateway_spot_nodes,
       &serializers] (const actor_ref_t &actor_ref) mutable {
          if (actor_ref.node_rid ().empty ()) {
              return result_t<void>::failure (framework_error_kind_t::actor_route_not_found,
                                              "bound actor node route is empty");
          }
          if (detail::is_local_actor_ref (actor_ref)) {
              return result_t<void>::success ();
          }
          for (auto &binding : bindings) {
              if (actor_ref.node_rid ().value () == binding.local_spot_node_rid) {
                  return result_t<void>::success ();
              }
              if (!binding.accepts_route_channels) {
                  return register_bound_session_route_through_mesh (
                    binding.runtime, actor_ref, binding.local_spot_node_rid, serializers);
              }
              auto reply =
                binding.route_client
                  .request_to_node (
                    *binding.route_channel_name,
                    zlink::routing_id_t::from (std::string (actor_ref.node_rid ().value ())),
                    actor_bound_session_bind_route_request_t{
                      .actor_node_rid = std::string (actor_ref.node_rid ().value ()),
                      .actor_type = std::string (actor_ref.actor_type ()),
                      .actor_id = std::string (actor_ref.actor_id ()),
                      .actor_generation = actor_ref.generation (),
                      .session_node_rid = binding.local_spot_node_rid})
                  .template async<actor_bound_session_route_reply_t> ()
                  .result ();
              if (!reply) {
                  return detail::propagate_failure<void> (
                    reply, "bound session route registration failed");
              }
              return result_t<void>::success ();
          }
          return result_t<void>::failure (framework_error_kind_t::spot_route_not_found,
                                          "bound session route channel is not configured");
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
    actor_gateway.on_membership (
      [bindings = actor_gateway_spot_nodes] (
        const actor_ref_t &actor_ref) mutable -> std::optional<spot_rid_t> {
          for (auto &binding : bindings) {
              if (auto spot = binding.runtime.actor_spot (actor_ref)) {
                  return spot;
              }
          }
          return std::nullopt;
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
