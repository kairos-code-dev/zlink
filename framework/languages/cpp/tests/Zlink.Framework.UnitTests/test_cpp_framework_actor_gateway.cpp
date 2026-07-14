/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/framework.hpp>
#include <zlink/Contracts/Core/context.hpp>
#include <zlink/Contracts/Service/actor.hpp>
#include <zlink/Contracts/Service/spot_node.hpp>

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/actors/actor_client.hpp"
#include "runtime/actors/actor_route_internal_dispatcher.hpp"
#include "runtime/channels/channel_runtime.hpp"
#include "runtime/channels/route_packet_dispatcher.hpp"
#include "runtime/host/actor_gateway_spot_bridge.hpp"
#include "runtime/locations/in_memory_location_store.hpp"
#include "runtime/locations/store_location_resolvers.hpp"
#include "runtime/messaging/envelope_codec.hpp"
#include "runtime/spots/spot_route_packets.hpp"
#include "runtime/streams/stream_runtime.hpp"

#include <iostream>

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeindex>
#include <vector>

namespace
{

struct typed_session_push_t
{
    std::string body;
};

struct json_session_push_t
{
    std::string body;
};

std::string to_stream_payload (const typed_session_push_t &message)
{
    return message.body;
}

struct join_request_t
{
    std::string room_id;
};

struct join_reply_t
{
    std::string room_id;
    std::string mark;
};

struct bridge_request_t
{
    int value{};
};

struct bridge_reply_t
{
    std::string value;
};

struct bridge_actor_t
{
    std::string actor_id;
    zlink::framework::actor_context_t context;
    int joined_value{};

    void set_actor_context (const zlink::framework::actor_context_t &actor_context)
    {
        context = actor_context;
    }
};

struct bridge_actor_factory_t
{
    bridge_actor_t create (std::string actor_id) const { return {std::move (actor_id)}; }
};

struct bridge_spot_t : public zlink::framework::spot_t
{
    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ()
          .add_actor_request<&bridge_spot_t::on_relay> ("bridge.relay")
          .add_actor_send<&bridge_spot_t::on_signal> ("bridge.signal")
          .add_actor_request<&bridge_spot_t::on_throw> ("bridge.throw");
    }

    zlink::framework::spot_actor_join_response_t
    on_actor_join (std::string_view actor_id,
                   const zlink::framework::message_t &request)
    {
        const auto decoded = request.decode<bridge_request_t> ();
        join_value = decoded.value;
        return zlink::framework::spot_actor_join_response_t::accept (
          zlink::framework::message_t::from (bridge_reply_t{"joined:" + std::string (actor_id)}));
    }

    void on_actor_joined (bridge_actor_t &) { ++joined_count; }

    bridge_reply_t on_relay (bridge_actor_t &actor,
                             zlink::framework::spot_actor_request_context_t &,
                             const bridge_request_t &request)
    {
        relay_value = request.value;
        return {actor.actor_id + ":" + std::to_string (request.value)};
    }

    bridge_reply_t on_throw (bridge_actor_t &,
                             zlink::framework::spot_actor_request_context_t &,
                             const bridge_request_t &request)
    {
        throw std::runtime_error ("bridge boom " + std::to_string (request.value));
    }

    void on_signal (bridge_actor_t &,
                    const zlink::framework::spot_actor_send_context_t &,
                    const bridge_request_t &request)
    {
        signal_value = request.value;
    }

    void on_disconnect_actor (bridge_actor_t &) { ++disconnect_count; }

    int join_value{};
    int joined_count{};
    int relay_value{};
    int signal_value{};
    int disconnect_count{};
};

struct no_bind_entry_spot_t : public zlink::framework::entry_spot_t
{
    void configure (zlink::framework::entry_spot_context_t &context)
    {
        context.handlers ()
          .add_actor_request<&no_bind_entry_spot_t::on_relay> ("bridge.relay")
          .add_actor_request<&no_bind_entry_spot_t::on_throw> ("bridge.throw");
    }

    bridge_reply_t on_relay (bridge_actor_t &actor,
                             zlink::framework::spot_actor_request_context_t &,
                             const bridge_request_t &request)
    {
        return {actor.actor_id + ":" + std::to_string (request.value)};
    }

    bridge_reply_t on_throw (bridge_actor_t &,
                             zlink::framework::spot_actor_request_context_t &,
                             const bridge_request_t &request)
    {
        throw std::runtime_error ("bridge boom " + std::to_string (request.value));
    }
};

void to_json (nlohmann::json &json, const join_request_t &value)
{
    json = {{"roomId", value.room_id}};
}

void from_json (const nlohmann::json &json, join_request_t &value)
{
    value.room_id = json.value ("roomId", "");
}

void to_json (nlohmann::json &json, const join_reply_t &value)
{
    json = {{"roomId", value.room_id}, {"mark", value.mark}};
}

void from_json (const nlohmann::json &json, join_reply_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.mark = json.value ("mark", "");
}

void to_json (nlohmann::json &json, const bridge_request_t &value)
{
    json = {{"value", value.value}};
}

void from_json (const nlohmann::json &json, bridge_request_t &value)
{
    value.value = json.value ("value", 0);
}

void to_json (nlohmann::json &json, const bridge_reply_t &value)
{
    json = {{"value", value.value}};
}

void from_json (const nlohmann::json &json, bridge_reply_t &value)
{
    value.value = json.value ("value", "");
}

void to_json (nlohmann::json &json, const json_session_push_t &value)
{
    json = {{"body", value.body}};
}

void from_json (const nlohmann::json &json, json_session_push_t &value)
{
    value.body = json.value ("body", "");
}

std::vector<zlink::message_t>
request_native_actor (zlink::service::spot_node_t &native,
                      const zlink::actor_ref_t &actor,
                      zlink::framework::detail::spot_node_runtime_t &runtime,
                      zlink::framework::service_provider_t &provider,
                      zlink::framework::serializer_registry_t &serializers,
                      std::string packet_name,
                      const bridge_request_t &request)
{
    zlink::framework::runtime::messaging::envelope_codec_t codec;
    zlink::framework::runtime::messaging::envelope_header_t header;
    header.kind = zlink::framework::runtime::messaging::message_kind_t::request;
    header.channel_name = "actor";
    header.message_name = std::move (packet_name);
    auto parts = codec.encode_parts (header, std::type_index (typeid (bridge_request_t)), &request,
                                     serializers);
    auto copied = parts.items ();
    auto submit = std::move (native.request_to_actor (actor)).message (copied[0]);
    for (std::size_t index = 1; index < copied.size (); ++index) {
        submit = std::move (submit).message (copied[index]);
    }
    std::mutex mutex;
    std::condition_variable changed;
    bool completed = false;
    zlink::request_result_t result = zlink::request_result_t::ok;
    std::vector<zlink::message_t> reply;
    if (!std::move (submit)
           .timeout (std::chrono::milliseconds (1000))
           .flags (static_cast<int> (zlink::send_flags_t::dontwait))
           .submit ([&] (zlink::request_result_t callback_result,
                         std::vector<zlink::message_t> callback_reply) {
               {
                   std::lock_guard lock (mutex);
                   result = callback_result;
                   reply = std::move (callback_reply);
                   completed = true;
               }
               changed.notify_all ();
           })) {
        throw std::runtime_error ("native actor request submit failed");
    }
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (2);
    while (true) {
        {
            std::lock_guard lock (mutex);
            if (completed) {
                break;
            }
        }
        (void) runtime.drain_actor_packets (provider, serializers);
        std::unique_lock lock (mutex);
        if (changed.wait_for (lock, std::chrono::milliseconds (1), [&] { return completed; })) {
            break;
        }
        if (std::chrono::steady_clock::now () >= deadline) {
            throw std::runtime_error ("native actor request timed out");
        }
    }
    if (result != zlink::request_result_t::ok) {
        throw std::runtime_error ("native actor request failed: "
                                  + std::to_string (static_cast<int> (result)));
    }
    return reply;
}

zlink::framework::runtime::messaging::envelope_header_t
decode_reply_header (const std::vector<zlink::message_t> &parts)
{
    zlink::framework::runtime::messaging::envelope_codec_t codec;
    auto decoded =
      codec.decode_header (zlink::framework::runtime::messaging::message_parts_t (parts));
    if (!decoded) {
        throw std::runtime_error ("reply header decode failed");
    }
    return decoded.value ();
}

bridge_reply_t decode_bridge_reply (const std::vector<zlink::message_t> &parts)
{
    zlink::framework::runtime::messaging::envelope_codec_t codec;
    auto body = codec.decode_body (zlink::framework::runtime::messaging::message_parts_t (parts));
    if (!body) {
        throw std::runtime_error ("reply body decode failed");
    }
    return body.value ().parse_json<bridge_reply_t> ();
}

} // namespace

int main ()
{
    using zlink::framework::framework_error_kind_t;

    zlink::framework::zlink_builder_t zlink;
    zlink.add_spot_node ("session-actors").bind ("tcp://0.0.0.0:7101");
    zlink.stream ("client-stream").bind ("tcp://0.0.0.0:9200").register_session ("client");

    zlink::framework::detail::actor_gateway_runtime_t gateway;
    zlink::framework::serializer_registry_t serializers;
    serializers.add<std::string> (
      [] (const std::string &value) {
          return zlink::framework::encoded_payload_t::from_string (value);
      },
      [] (const zlink::framework::encoded_payload_t &payload) { return payload.to_string (); });
    serializers.add<typed_session_push_t> (
      [] (const typed_session_push_t &value) {
          return zlink::framework::encoded_payload_t::from_string (value.body);
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return typed_session_push_t{payload.to_string ()};
      });
    serializers.add<bridge_request_t> (
      [] (const bridge_request_t &value) {
          return zlink::framework::encoded_payload_t::from_string (nlohmann::json (value).dump ());
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return nlohmann::json::parse (payload.to_string ()).get<bridge_request_t> ();
      });
    serializers.add<bridge_reply_t> (
      [] (const bridge_reply_t &value) {
          return zlink::framework::encoded_payload_t::from_string (nlohmann::json (value).dump ());
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return nlohmann::json::parse (payload.to_string ()).get<bridge_reply_t> ();
      });
    std::mutex no_bind_events_mutex;
    std::condition_variable no_bind_events_changed;
    std::vector<zlink::framework::message_flow_event_t> no_bind_events;
    zlink::framework::dispatch_options_t no_bind_dispatch;
    no_bind_dispatch.message_flow (zlink::framework::message_flow_log_mode_t::errors_only);
    no_bind_dispatch.set_message_flow_observer (
      [&] (const zlink::framework::message_flow_event_t &event) {
          {
              std::lock_guard lock (no_bind_events_mutex);
              no_bind_events.push_back (event);
          }
          no_bind_events_changed.notify_all ();
      });
    zlink::framework::zlink_builder_t no_bind_host;
    zlink::framework::detail::channel_runtime_t::from (no_bind_host.message_bus ())
      .bind_serializers (serializers);
    auto no_bind_spot = std::make_shared<no_bind_entry_spot_t> ();
    auto no_bind_node_builder = no_bind_host.add_spot_node ("no-bind-node");
    zlink::framework::detail::apply_dispatch_options (no_bind_host, no_bind_dispatch);
    no_bind_node_builder.set_routing_id (zlink::routing_id_t::from ("no-bind-node"));
    no_bind_node_builder
      .add_entry_spot<no_bind_entry_spot_t> ([no_bind_spot] { return no_bind_spot; })
      .add_actor_factory<bridge_actor_factory_t> ("bridge-player");
    auto no_bind_entry = no_bind_node_builder.create_spot ("entry");
    auto no_bind_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (no_bind_node_builder);
    zlink::context_t no_bind_context;
    auto no_bind_native = std::make_shared<zlink::service::spot_node_t> (no_bind_context);
    no_bind_native->set_routing_id (zlink::routing_id_t::from ("no-bind-node"));
    auto no_bind_native_entry = no_bind_native->entry_spot ();
    no_bind_runtime.attach_native_node (no_bind_native);
    zlink::framework::detail::actor_gateway_runtime_t no_bind_gateway;
    no_bind_gateway.bind_serializers (serializers);
    zlink::framework::service_collection_t no_bind_services;
    auto no_bind_provider = no_bind_services.build_provider ();
    auto no_bind_native_actor = no_bind_native->create_actor ("actor-a");
    auto cleanup_no_bind = [&] {
        no_bind_native_actor.close (std::chrono::milliseconds (100));
        no_bind_runtime.detach_native_node ();
        no_bind_native.reset ();
    };
    const auto no_bind_actor_ref = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("no-bind-node"), "bridge-player", "actor-a",
      no_bind_native_actor.ref ().generation ());
    auto no_bind_join = no_bind_runtime.join_actor_to_entry_spot_erased (
      no_bind_actor_ref, zlink::framework::node_rid_t::from_string ("no-bind-node"),
      zlink::message_t::from_json (bridge_request_t{0}));
    if (!no_bind_join || no_bind_join.value ().result_code != 0) {
        cleanup_no_bind ();
        return 75;
    }
    auto no_bind_reply =
      request_native_actor (*no_bind_native, no_bind_native_actor.ref (), no_bind_runtime,
                            no_bind_provider, serializers, "bridge.relay", bridge_request_t{7});
    auto no_bind_reply_header = decode_reply_header (no_bind_reply);
    if (no_bind_reply_header.kind != zlink::framework::runtime::messaging::message_kind_t::response
        || decode_bridge_reply (no_bind_reply).value != "actor-a:7"
        || no_bind_gateway.actor_bound ("actor-a")) {
        cleanup_no_bind ();
        return 76;
    }
    zlink::framework::runtime::in_memory_location_store_t no_bind_location_store;
    auto no_bind_owner =
      no_bind_location_store
        .renew_owner_lease ("no-bind-owner", zlink::routing_id_t::from ("no-bind-node"),
                            std::chrono::seconds (30))
        .result ();
    if (!no_bind_owner) {
        cleanup_no_bind ();
        return 79;
    }
    auto no_bind_location =
      no_bind_location_store
        .update_actor (
          zlink::framework::actor_location_t{
            .actor_id = "actor-a",
            .actor_type = "bridge-player",
            .actor_ref = no_bind_join.value ().actor,
            .node_rid = zlink::routing_id_t::from ("no-bind-node"),
            .location_kind = zlink::spot_kind::entry,
            .spot_mesh_name = "bridge",
            .spot_rid = std::nullopt,
            .owner_id = "no-bind-owner"},
          zlink::framework::location_write_intent_t::new_claim)
        .result ();
    if (!no_bind_location
        || no_bind_location.value ().status
             != zlink::framework::location_write_status_t::stored) {
        cleanup_no_bind ();
        return 80;
    }
    auto no_bind_actor_client = zlink::framework::runtime::make_actor_client (
      no_bind_location_store, serializers, {no_bind_runtime},
      std::make_shared<zlink::framework::runtime::actor_location_observer_t> ());
    auto no_bind_client_reply =
      no_bind_actor_client->request_to_actor (no_bind_join.value ().actor, bridge_request_t{8})
        .timeout (std::chrono::milliseconds (1000))
        .async<bridge_reply_t> ()
        .result ();
    if (!no_bind_client_reply
        && (no_bind_client_reply.error_kind () == framework_error_kind_t::payload_decode_failed
            || (no_bind_client_reply.error ()
                && std::string (no_bind_client_reply.error ()->what ())
                     .find ("erased serializer is not registered")
                     != std::string::npos))) {
        cleanup_no_bind ();
        return 81;
    }
    if (no_bind_client_reply && no_bind_client_reply.value ().value != "actor-a:8") {
        cleanup_no_bind ();
        return 82;
    }
    if (no_bind_gateway.actor_bound ("actor-a")) {
        cleanup_no_bind ();
        return 83;
    }
    auto no_bind_throw_reply =
      request_native_actor (*no_bind_native, no_bind_native_actor.ref (), no_bind_runtime,
                            no_bind_provider, serializers, "bridge.throw", bridge_request_t{9});
    auto no_bind_throw_header = decode_reply_header (no_bind_throw_reply);
    if (no_bind_throw_header.kind != zlink::framework::runtime::messaging::message_kind_t::error
        || no_bind_throw_header.error_code.value_or ("") != "request_failed"
        || no_bind_throw_header.error_message.value_or ("").find ("bridge boom 9")
             == std::string::npos
        || no_bind_gateway.actor_bound ("actor-a")) {
        cleanup_no_bind ();
        return 78;
    }
    auto has_throw_event = [&] {
        return std::find_if (
                 no_bind_events.begin (), no_bind_events.end (),
                 [] (const zlink::framework::message_flow_event_t &event) {
                     return event.outcome == zlink::framework::message_flow_outcome_t::error
                            && event.surface
                                 == zlink::framework::dispatch_error_surface_t::spot_actor
                            && event.message_kind
                                 == zlink::framework::dispatch_message_kind_t::actor_request
                            && event.error_reason
                                 == zlink::framework::dispatch_error_reason_t::handler_exception
                            && event.error_action
                                 == zlink::framework::dispatch_error_action_t::reply_error
                            && event.packet_name && *event.packet_name == "bridge.throw"
                            && event.actor_id && *event.actor_id == "actor-a";
                 })
               != no_bind_events.end ();
    };
    {
        std::unique_lock lock (no_bind_events_mutex);
        if (!no_bind_events_changed.wait_for (lock, std::chrono::milliseconds (500),
                                              has_throw_event)) {
            cleanup_no_bind ();
            return 80;
        }
    }
    zlink::framework::detail::actor_gateway_runtime_t bound_mirror_gateway;
    bound_mirror_gateway.bind_serializers (serializers);
    auto bound_mirror_actor = bound_mirror_gateway.manager ()
                                .bind (zlink::framework::actor_ref_t (
                                  zlink::framework::node_rid_t::from_string ("bound-node"),
                                  "bridge-player", "bound-actor", 1))
                                .async ()
                                .result ();
    bool bound_mirror_relay_seen = false;
    bound_mirror_gateway.on_relay ([&] (const zlink::framework::actor_ref_t &actor,
                                        zlink::framework::actor_context_t,
                                        const zlink::framework::detail::stream_header_t &header,
                                        const zlink::message_t &payload) {
        const auto request = payload.parse_json<bridge_request_t> ();
        bound_mirror_relay_seen = actor.actor_id () == "bound-actor"
                                  && header.packet_name () == "bridge.relay" && request.value == 8;
        return zlink::framework::result_t<std::optional<zlink::message_t>>::success (
          zlink::message_t::from_json (bridge_reply_t{"bound-ok"}));
    });
    auto bound_mirror_reply =
      bound_mirror_actor.value ()
        .relay_request ("bridge.relay", zlink::message_t::from_json (bridge_request_t{8}))
        .async ()
        .result ();
    if (!bound_mirror_reply || !bound_mirror_relay_seen
        || bound_mirror_reply.value ().parse_json<bridge_reply_t> ().value != "bound-ok"
        || !bound_mirror_gateway.actor_bound ("bound-actor")) {
        return 77;
    }
    auto missing_reply = no_bind_runtime.relay_actor_packet (
      zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("no-bind-node"),
                                     "missing-player", "missing-actor", 1),
      zlink::framework::actor_context_t{}, zlink::framework::detail::stream_message_kind_t::request,
      "bridge.relay", zlink::message_t::from_json (bridge_request_t{10}), no_bind_provider,
      serializers);
    if (missing_reply
        || missing_reply.error_kind ()
             != zlink::framework::framework_error_kind_t::actor_route_not_found
        || no_bind_gateway.actor_bound ("missing-actor")) {
        cleanup_no_bind ();
        return 79;
    }
    cleanup_no_bind ();
    zlink::framework::detail::actor_gateway_runtime_t no_serializer_gateway;
    const auto no_serializer_create = no_serializer_gateway.manager ().create (
      "player", "raw", zlink::framework::message_t::from (join_request_t{"raw"}));
    if (no_serializer_create
        || no_serializer_create.error_kind () != framework_error_kind_t::request_protocol_error) {
        return 54;
    }
    gateway.bind_serializers (serializers);
    auto manager = gateway.manager ();
    const auto empty_actor_create = manager.create ("", "nobody");
    if (empty_actor_create
        || empty_actor_create.error_kind () != framework_error_kind_t::request_protocol_error) {
        return 55;
    }
    auto created = manager.create ("player", "alice");
    if (!created || created.value ().actor_id () != "alice") {
        return 2;
    }

    auto duplicate = manager.create ("player", "alice");
    if (duplicate || duplicate.error_kind () != framework_error_kind_t::actor_already_exists) {
        return 3;
    }

    auto found = manager.find ("alice");
    if (!found || found->ref ().actor_type () != "player") {
        return 4;
    }

    auto type_mismatch = manager.get_or_create ("enemy", "alice");
    if (type_mismatch
        || type_mismatch.error_kind () != framework_error_kind_t::actor_type_mismatch) {
        return 5;
    }
    auto empty_bind =
      manager.bind (zlink::framework::actor_ref_t (zlink::framework::node_rid_t{}, "", "", 0))
        .async ()
        .result ();
    if (empty_bind || empty_bind.error_kind () != framework_error_kind_t::actor_route_not_found) {
        return 56;
    }
    auto mismatched_bind =
      manager
        .bind (zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string ("remote-node"), "enemy", "alice", 1))
        .async ()
        .result ();
    if (mismatched_bind
        || mismatched_bind.error_kind () != framework_error_kind_t::actor_type_mismatch) {
        return 57;
    }
    if (gateway
          .update_actor_ref (
            zlink::framework::actor_ref_t (zlink::framework::node_rid_t{}, "", "", 0))
          .error_kind ()
        != framework_error_kind_t::actor_route_not_found) {
        return 58;
    }
    const auto update_missing = gateway.update_actor_ref (zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("remote-node"), "player", "missing", 1));
    if (!update_missing) {
        return 59;
    }
    const auto update_type_mismatch = gateway.update_actor_ref (zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("remote-node"), "enemy", "alice", 1));
    if (update_type_mismatch
        || update_type_mismatch.error_kind () != framework_error_kind_t::actor_type_mismatch) {
        return 60;
    }
    const auto update_stale = gateway.update_actor_ref (zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("remote-node"), "player", "alice", 0));
    if (update_stale
        || (update_stale.error () != nullptr
         && zlink::framework::detail::boundary_state (*update_stale.error ()) != zlink::framework::detail::boundary_error_t::stale_generation)) {
        return 61;
    }
    const auto destroy_empty = gateway.destroy_actor (
      zlink::framework::actor_ref_t (zlink::framework::node_rid_t{}, "", "", 0));
    if (destroy_empty
        || destroy_empty.error_kind () != framework_error_kind_t::actor_route_not_found) {
        return 62;
    }
    const auto destroy_missing = gateway.destroy_actor (zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("remote-node"), "player", "missing", 1));
    if (!destroy_missing) {
        return 63;
    }
    const auto destroy_type_mismatch = gateway.destroy_actor (zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("remote-node"), "enemy", "alice", 1));
    if (destroy_type_mismatch
        || destroy_type_mismatch.error_kind () != framework_error_kind_t::actor_type_mismatch) {
        return 64;
    }
    const auto temp_actor = manager.create ("player", "temp");
    if (!temp_actor) {
        return 65;
    }
    const auto destroy_temp = gateway.destroy_actor (temp_actor.value ().ref ());
    if (!destroy_temp || manager.find ("temp")) {
        return 66;
    }
    zlink::framework::actor_ref_t replay_first_ref (
      zlink::framework::node_rid_t::from_string ("replay-node-a"), "player", "replayer", 3);
    zlink::framework::actor_ref_t replay_second_ref (
      zlink::framework::node_rid_t::from_string ("replay-node-b"), "player", "replayer", 4);
    auto replay_first = manager.bind_or_get (replay_first_ref).async ().result ();
    if (!replay_first || !gateway.actor_bound ("replayer")
        || replay_first.value ().ref ().generation () != 3) {
        return 81;
    }
    auto replay_second = manager.bind_or_get (replay_second_ref).async ().result ();
    auto replay_found = manager.find ("replayer");
    if (!replay_second || !replay_found || !gateway.actor_bound ("replayer")
        || gateway.actor_disconnected ("replayer")
        || replay_second.value ().ref ().node_rid ().value () != "replay-node-b"
        || replay_second.value ().ref ().generation () != 4
        || replay_found->ref ().node_rid ().value () != "replay-node-b"
        || replay_found->ref ().generation () != 4) {
        return 82;
    }

    zlink::framework::actor_ref_t remote_ref (
      zlink::framework::node_rid_t::from_string ("remote-node"), "player", "bob", 7);
    auto bound = manager.bind (remote_ref).async ().result ();
    if (!bound || !gateway.actor_bound ("bob") || bound.value ().ref ().generation () != 7) {
        return 6;
    }

    zlink::framework::stream_metadata_t metadata;
    metadata.with ("trace", "t1");
    zlink::framework::detail::stream_header_t header (
      zlink::framework::detail::stream_message_kind_t::send, zlink::framework::stream_codec_t::json,
      zlink::framework::detail::stream_header_flags_t::has_metadata, std::nullopt, "move",
      metadata);
    const auto payload = zlink::message_t::from (std::string ("payload"));
    const auto framework_payload = zlink::framework::message_t::from (std::string ("payload"));
    auto relay_with_header = [&] (zlink::framework::session_actor_t &actor,
                                  const zlink::message_t &message) {
        zlink::framework::detail::enter_stream_relay_dispatch (header);
        (void) actor.relay (message).result ();
        zlink::framework::detail::exit_stream_relay_dispatch ();
    };
    auto relay_request_with_header = [&] (zlink::framework::session_actor_t &actor,
                                          const zlink::message_t &message) {
        zlink::framework::detail::enter_stream_relay_dispatch (header);
        auto result = actor.relay_request (message).async ().result ();
        zlink::framework::detail::exit_stream_relay_dispatch ();
        return result;
    };
    relay_with_header (bound.value (), payload);
    if (gateway.relayed_frames ().size () != 1
        || gateway.relayed_frames ()[0].payload.to_string () != "payload"
        || payload.to_string () != "payload") {
        return 7;
    }
    auto relay_request_without_handler = relay_request_with_header (bound.value (), payload);
    if (relay_request_without_handler
        || relay_request_without_handler.error_kind ()
             != framework_error_kind_t::actor_dispatch_handler_not_found) {
        return 68;
    }
    const auto relayed_before_dispatch = gateway.relayed_frames ().size ();

    bool relay_dispatch_seen = false;
    std::uint64_t relay_dispatch_generation = 0;
    gateway.on_relay ([&] (const zlink::framework::actor_ref_t &actor,
                           zlink::framework::actor_context_t,
                           const zlink::framework::detail::stream_header_t &received_header,
                           const zlink::message_t &received_payload) {
        relay_dispatch_seen = actor.actor_id () == "bob" && received_header.packet_name () == "move"
                              && received_payload.to_string () == "payload";
        relay_dispatch_generation = actor.generation ();
        return zlink::framework::result_t<std::optional<zlink::message_t>>::success (
          zlink::message_t::from (std::string ("relay-reply")));
    });
    relay_with_header (bound.value (), payload);
    if (!relay_dispatch_seen || gateway.relayed_frames ().size () != relayed_before_dispatch) {
        return 22;
    }
    auto relay_request = relay_request_with_header (bound.value (), payload);
    if (!relay_request || relay_request.value ().to_string () != "relay-reply") {
        return 23;
    }
    zlink::framework::actor_ref_t moved_ref (
      zlink::framework::node_rid_t::from_string ("remote-node"), "player", "bob", 8);
    if (!gateway.update_actor_ref (moved_ref)) {
        return 83;
    }
    relay_dispatch_seen = false;
    auto relay_request_after_move = relay_request_with_header (bound.value (), payload);
    if (!relay_request_after_move || relay_request_after_move.value ().to_string () != "relay-reply"
        || !relay_dispatch_seen || relay_dispatch_generation != 8
        || bound.value ().ref ().generation () != 8) {
        return 84;
    }
    auto rebound_from_stale_ref = manager.bind_or_get (remote_ref).async ().result ();
    if (!rebound_from_stale_ref || rebound_from_stale_ref.value ().ref ().generation () != 8) {
        return 87;
    }
    relay_dispatch_seen = false;
    auto relay_after_stale_rebind =
      relay_request_with_header (rebound_from_stale_ref.value (), payload);
    if (!relay_after_stale_rebind || !relay_dispatch_seen || relay_dispatch_generation != 8) {
        return 88;
    }

    /* framework API §2.4.3: local relay request가 reply 없이 끝나면 caller의 task를 framework
     * 오류로 완료하고, 그 실패를 action=fail_caller / reason=reply_path_missing으로 관측한다. */
    std::mutex fail_caller_mutex;
    std::vector<zlink::framework::message_flow_event_t> fail_caller_events;
    zlink::framework::dispatch_options_t fail_caller_dispatch;
    fail_caller_dispatch.message_flow (zlink::framework::message_flow_log_mode_t::errors_only);
    fail_caller_dispatch.set_message_flow_observer (
      [&] (const zlink::framework::message_flow_event_t &event) {
          const std::lock_guard lock (fail_caller_mutex);
          fail_caller_events.push_back (event);
      });
    gateway.set_dispatch (fail_caller_dispatch);
    gateway.on_relay ([&] (const zlink::framework::actor_ref_t &,
                           zlink::framework::actor_context_t,
                           const zlink::framework::detail::stream_header_t &,
                           const zlink::message_t &) {
        return zlink::framework::result_t<std::optional<zlink::message_t>>::success (std::nullopt);
    });
    auto relay_request_without_reply = relay_request_with_header (bound.value (), payload);
    if (relay_request_without_reply
        || relay_request_without_reply.error_kind ()
             != framework_error_kind_t::request_protocol_error) {
        return 89;
    }
    {
        /* observer 전달은 비동기다: 이벤트가 도착할 때까지 짧게 기다린다. */
        const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (2);
        for (;;) {
            {
                const std::lock_guard lock (fail_caller_mutex);
                if (!fail_caller_events.empty ()) {
                    break;
                }
            }
            if (std::chrono::steady_clock::now () >= deadline) {
                return 91;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
        const std::lock_guard lock (fail_caller_mutex);
        const auto found = std::find_if (
          fail_caller_events.begin (), fail_caller_events.end (),
          [] (const zlink::framework::message_flow_event_t &event) {
              return event.outcome == zlink::framework::message_flow_outcome_t::error
                     && event.error_action
                          == std::optional<zlink::framework::dispatch_error_action_t> (
                            zlink::framework::dispatch_error_action_t::fail_caller)
                     && event.error_reason
                          == std::optional<zlink::framework::dispatch_error_reason_t> (
                            zlink::framework::dispatch_error_reason_t::reply_path_missing)
                     && event.surface == zlink::framework::dispatch_error_surface_t::spot_actor
                     && event.actor_id == std::optional<std::string> ("bob");
          });
        if (found == fail_caller_events.end ()) {
            return 90;
        }
    }

    zlink::framework::session_actor_t unbound;
    auto unbound_relay_request = unbound.relay_request ("move", payload).async ().result ();
    if (unbound_relay_request
        || unbound_relay_request.error_kind () != framework_error_kind_t::actor_route_not_found) {
        return 70;
    }
    relay_with_header (unbound, payload);

    const auto sink_less_push = [] (auto &&push_fn) {
        try {
            push_fn ();
            return false;
        }
        catch (const zlink::framework::framework_exception_t &error) {
            return error.kind () == framework_error_kind_t::actor_session_not_bound;
        }
    };
    if (!sink_less_push (
          [&] { bound.value ().context ().bound_session ().send (framework_payload).submit (); })) {
        return 75;
    }
    if (gateway.bound_session_pushes ().size () != 1
        || gateway.bound_session_pushes ()[0].payload.to_string () != "payload") {
        return 9;
    }

    if (!sink_less_push ([&] {
            bound.value ()
              .context ()
              .bound_session ()
              .send (typed_session_push_t{"typed-payload"})
              .submit ();
        })) {
        return 76;
    }
    if (gateway.bound_session_pushes ().size () != 2
        || gateway.bound_session_pushes ()[1].payload.to_string () != "typed-payload") {
        return 13;
    }
    if (!sink_less_push ([&] {
            bound.value ()
              .context ()
              .bound_session ()
              .send (json_session_push_t{"json-payload"})
              .submit ();
        })) {
        return 77;
    }
    if (gateway.bound_session_pushes ().size () != 3
        || gateway.bound_session_pushes ()[2].payload.to_string ()
             != R"({"body":"json-payload"})") {
        return 24;
    }

    (void) bound.value ().bound_session ().disconnect ().result ();
    if (gateway.actor_bound ("bob") || !gateway.actor_disconnected ("bob")) {
        return 10;
    }
    auto disconnected_relay_request =
      bound.value ().relay_request ("move", payload).async ().result ();
    if (disconnected_relay_request
        || (disconnected_relay_request.error () != nullptr
         && zlink::framework::detail::boundary_state (*disconnected_relay_request.error ()) != zlink::framework::detail::boundary_error_t::disconnected)) {
        return 72;
    }
    bool disconnected_send_rejected = false;
    try {
        bound.value ().bound_session ().send (framework_payload).submit ();
    }
    catch (const zlink::framework::framework_exception_t &error) {
        disconnected_send_rejected = zlink::framework::detail::boundary_state (error) == zlink::framework::detail::boundary_error_t::disconnected;
    }
    if (!disconnected_send_rejected) {
        return 73;
    }
    relay_with_header (bound.value (), payload);
    if (payload.to_string () != "payload") {
        return 17;
    }

    auto rebound = manager.bind (remote_ref).async ().result ();
    if (!rebound || !gateway.actor_bound ("bob")) {
        return 11;
    }

    (void) rebound.value ().notify_disconnected ().result ();
    if (gateway.actor_bound ("bob") || !gateway.actor_disconnected ("bob")) {
        return 18;
    }
    rebound = manager.bind (remote_ref).async ().result ();
    if (!rebound || !gateway.actor_bound ("bob")) {
        return 19;
    }

    bool disconnect_dispatch_seen = false;
    gateway.on_disconnect ([&] (const zlink::framework::actor_ref_t &actor) {
        disconnect_dispatch_seen = actor.actor_id () == "bob" && actor.generation () == 7;
        return zlink::framework::result_t<void>::success ();
    });
    (void) rebound.value ().notify_disconnected ().result ();
    if (!disconnect_dispatch_seen || gateway.actor_bound ("bob")
        || !gateway.actor_disconnected ("bob")) {
        return 33;
    }
    rebound = manager.bind (remote_ref).async ().result ();
    if (!rebound || !gateway.actor_bound ("bob")) {
        return 34;
    }

    auto actor_context = rebound.value ().context ();
    auto empty_spot_join =
      actor_context.join_spot (zlink::framework::spot_rid_t{}, join_request_t{"empty"})
        .async ()
        .result ();
    if (empty_spot_join
        || empty_spot_join.error_kind () != framework_error_kind_t::spot_route_not_found) {
        return 73;
    }
    auto no_dispatcher_join =
      actor_context
        .join_spot (zlink::framework::spot_rid_t::from_string ("no-dispatcher"),
                    join_request_t{"no-dispatcher"})
        .async ()
        .result ();
    if (no_dispatcher_join
        || no_dispatcher_join.error_kind ()
             != framework_error_kind_t::actor_dispatch_handler_not_found) {
        return 74;
    }

    bool typed_join_seen = false;
    gateway.on_join_spot ([&] (const zlink::framework::actor_ref_t &actor,
                               zlink::framework::spot_rid_t spot_rid,
                               const zlink::message_t &payload) {
        const auto request = payload.parse_json<join_request_t> ();
        typed_join_seen = actor.actor_id () == "bob" && spot_rid.value () == "typed-match"
                          && request.room_id == "typed-match";
        return zlink::framework::result_t<zlink::framework::detail::actor_join_reply_t>::success (
          zlink::framework::detail::actor_join_reply_t{
            0,
            zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("spot-node"),
                                           "player", "bob", 8),
            zlink::message_t::from_json (join_reply_t{"typed-match", "X"})});
    });
    const auto typed_join = actor_context
                              .join_spot (zlink::framework::spot_rid_t::from_string ("typed-match"),
                                          join_request_t{"typed-match"})
                              .async<join_reply_t> ()
                              .result ();
    const auto *typed_join_accepted =
      typed_join
        ? std::get_if<zlink::framework::actor_join_accepted_t<join_reply_t>> (&typed_join.value ())
        : nullptr;
    if (typed_join_accepted == nullptr || !typed_join_seen
        || typed_join_accepted->reply.mark != "X") {
        return 28;
    }

    bool join_spot_seen = false;
    gateway.on_join_spot ([&] (const zlink::framework::actor_ref_t &actor,
                               zlink::framework::spot_rid_t spot_rid,
                               const zlink::message_t &payload) {
        const auto request = payload.parse_json<join_request_t> ();
        join_spot_seen = actor.actor_id () == "bob" && spot_rid.value () == "match-1"
                         && request.room_id == "match-1";
        return zlink::framework::result_t<zlink::framework::detail::actor_join_reply_t>::success (
          zlink::framework::detail::actor_join_reply_t{
            0,
            zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("spot-node"),
                                           "player", "bob", 8),
            zlink::message_t::from_json (join_reply_t{"match-1", "O"})});
    });
    const auto join_spot =
      actor_context
        .join_spot (zlink::framework::spot_rid_t::from_string ("match-1"),
                    zlink::framework::message_t::from (join_request_t{"match-1"}))
        .async ()
        .result ();
    const auto *join_spot_accepted =
      join_spot ? std::get_if<zlink::framework::actor_join_accepted_t<zlink::framework::message_t>> (&join_spot.value ()) : nullptr;
    if (join_spot_accepted == nullptr || !join_spot_seen
        || join_spot_accepted->actor.generation () != 8
        || join_spot_accepted->reply.decode<join_reply_t> (serializers).mark != "O") {
        return 14;
    }
    relay_with_header (rebound.value (), payload);
    if (payload.to_string () != "payload") {
        return 20;
    }
    try {
        rebound.value ().bound_session ().send (framework_payload).submit ();
    }
    catch (const zlink::framework::framework_exception_t &) {
        // The rebound session may not have a stream sink in this fixture; the
        // one-way contract reports that synchronously and the test moves on.
    }

    bool entry_join_seen = false;
    gateway.on_join_entry_spot ([&] (const zlink::framework::actor_ref_t &actor,
                                     zlink::framework::node_rid_t node_rid,
                                     const zlink::message_t &request) {
        entry_join_seen = actor.actor_id () == "bob" && node_rid.value () == "entry-node"
                          && request.to_string () == "entry";
        return zlink::framework::result_t<zlink::framework::detail::actor_join_reply_t>::success (
          zlink::framework::detail::actor_join_reply_t{
            0,
            zlink::framework::actor_ref_t (zlink::framework::node_rid_t::from_string ("entry-node"),
                                           "player", "bob", 9),
            zlink::message_t::from (std::string ("joined"))});
    });
    const auto entry_join =
      actor_context
        .join_entry_spot (zlink::framework::node_rid_t::from_string ("entry-node"),
                          zlink::framework::message_t::from (std::string ("entry")))
        .async ()
        .result ();
    const auto *entry_join_accepted =
      entry_join ? std::get_if<zlink::framework::actor_join_accepted_t<zlink::framework::message_t>> (&entry_join.value ()) : nullptr;
    if (entry_join_accepted == nullptr || !entry_join_seen
        || entry_join_accepted->actor.generation () != 9
        || entry_join_accepted->reply.decode<std::string> (serializers) != "joined") {
        return 15;
    }
    disconnect_dispatch_seen = false;
    gateway.on_disconnect ([&] (const zlink::framework::actor_ref_t &actor) {
        disconnect_dispatch_seen = actor.actor_id () == "bob" && actor.generation () == 9;
        return zlink::framework::result_t<void>::success ();
    });
    (void) rebound.value ().notify_disconnected ().result ();
    if (!disconnect_dispatch_seen || gateway.actor_bound ("bob")
        || !gateway.actor_disconnected ("bob") || rebound.value ().ref ().generation () != 9) {
        return 85;
    }
    auto rebound_after_disconnect =
      manager.bind (entry_join_accepted->actor).async ().result ();
    if (!rebound_after_disconnect || !gateway.actor_bound ("bob")
        || gateway.actor_disconnected ("bob")) {
        return 86;
    }
    manager.unbind_session ("bob");
    if (gateway.actor_bound ("bob") || !gateway.actor_disconnected ("bob")) {
        return 12;
    }
    auto rebound_after_entry = manager.bind (entry_join_accepted->actor).async ().result ();
    if (!rebound_after_entry || !gateway.actor_bound ("bob")
        || gateway.actor_disconnected ("bob")) {
        return 24;
    }
    gateway.bind_session_route (entry_join_accepted->actor,
                                zlink.route_client (serializers), "actor.session.route",
                                zlink::routing_id_t::from (std::string ("session-node")));
    bool actor_route_sink_used = false;
    gateway.bind_session_sink (
      entry_join_accepted->actor,
      [&actor_route_sink_used] (std::string packet_name, const zlink::message_t &payload) {
          actor_route_sink_used = packet_name == "session.route"
                                  && payload.to_string () == "routed";
          return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
      },
      zlink::framework::stream_codec_t::message_pack);
    zlink::framework::detail::register_spot_route_packet_serializers (serializers);
    zlink::framework::service_collection_t actor_route_services;
    auto actor_route_provider = actor_route_services.build_provider ();
    zlink::framework::detail::route_handler_registry_t actor_route_handlers;
    zlink::framework::detail::actor_route_internal_dispatcher_t actor_route_internal (gateway,
                                                                                      serializers);
    zlink::framework::detail::route_packet_dispatcher_t actor_route_dispatcher (
      "actor.route", actor_route_provider, serializers, actor_route_handlers, actor_route_internal);
    zlink::framework::runtime::messaging::envelope_codec_t actor_route_envelope;
    zlink::framework::runtime::messaging::envelope_header_t actor_route_header;
    actor_route_header.kind = zlink::framework::runtime::messaging::message_kind_t::request;
    actor_route_header.channel_name = "actor.route";
    actor_route_header.message_name =
      zlink::framework::detail::actor_bound_session_route_request_t::packet_name;
    actor_route_header.correlation_id = "actor-route-1";
    const auto actor_route_request =
      zlink::framework::detail::make_actor_bound_session_route_request (
        entry_join_accepted->actor, "session.route",
        zlink::message_t::from (std::string ("routed")));
    auto actor_route_parts = actor_route_envelope.encode_parts (
      actor_route_header,
      std::type_index (typeid (zlink::framework::detail::actor_bound_session_route_request_t)),
      &actor_route_request, serializers);
    auto actor_route_dispatch =
      actor_route_dispatcher.dispatch (zlink::framework::detail::route_received_packet_t{
        zlink::routing_id_t::from (std::string ("entry-node")), 93, actor_route_parts});
    if (!actor_route_dispatch || !actor_route_dispatch.value ()
        || !actor_route_dispatch.value ()->request_seq
        || actor_route_dispatch.value ()->request_seq.value () != 93) {
        return 50;
    }
    auto actor_route_reply_header =
      actor_route_envelope.decode_header (actor_route_dispatch.value ()->parts);
    if (!actor_route_reply_header
        || actor_route_reply_header.value ().kind
             != zlink::framework::runtime::messaging::message_kind_t::response) {
        return 51;
    }
    if (!actor_route_sink_used) {
        return 54;
    }
    auto actor_route_header_only = actor_route_envelope.encode_header (actor_route_header);
    const auto actor_route_missing_body_send = actor_route_internal.dispatch_send (
      zlink::framework::detail::route_received_packet_t{
        zlink::routing_id_t::from (std::string ("entry-node")), std::nullopt,
        zlink::framework::runtime::messaging::message_parts_t (
          std::vector<zlink::message_t>{actor_route_header_only})},
      actor_route_provider);
    if (actor_route_missing_body_send
        || actor_route_missing_body_send.error_kind ()
             != zlink::framework::framework_error_kind_t::request_protocol_error) {
        return 52;
    }
    const auto actor_route_missing_body_request = actor_route_internal.dispatch_request (
      zlink::framework::detail::route_received_packet_t{
        zlink::routing_id_t::from (std::string ("entry-node")), 94,
        zlink::framework::runtime::messaging::message_parts_t (
          std::vector<zlink::message_t>{actor_route_header_only})},
      actor_route_header, actor_route_provider);
    if (actor_route_missing_body_request
        || actor_route_missing_body_request.error_kind ()
             != zlink::framework::framework_error_kind_t::request_protocol_error) {
        return 53;
    }
    const auto destroy_bound = gateway.destroy_actor (entry_join_accepted->actor);
    if (!destroy_bound || gateway.actor_bound ("bob") || gateway.actor_disconnected ("bob")) {
        return 25;
    }
    bool destroyed_send_rejected = false;
    try {
        rebound_after_entry.value ().bound_session ().send (framework_payload).submit ();
    }
    catch (const zlink::framework::framework_exception_t &) {
        destroyed_send_rejected = true;
    }
    if (!destroyed_send_rejected) {
        return 74;
    }
    relay_with_header (rebound_after_entry.value (), payload);

    zlink::framework::zlink_builder_t stream_sink_builder;
    stream_sink_builder.stream ("preserved-bound-session").bind ("inproc://preserved-bound-session");
    auto stream_sink_runtime =
      zlink::framework::detail::stream_runtime_t::from (stream_sink_builder);
    auto stream_sink = stream_sink_runtime.open_session ("preserved-bound-session");
    zlink::framework::detail::actor_gateway_runtime_t stream_sink_gateway;
    auto stream_sink_ref = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("entry-node"), "player", "stream-user", 3);
    auto stream_sink_actor =
      stream_sink_gateway.manager ().bind (stream_sink_ref).async ().result ();
    if (!stream_sink_actor) {
        return 76;
    }
    stream_sink_gateway.bind_session_stream ("stream-user", stream_sink,
                                             zlink::framework::stream_codec_t::json);
    stream_sink_gateway.bind_session_sink (
      stream_sink_ref,
      [] (std::string, const zlink::message_t &) {
          return zlink::framework::task_t<void> (
            zlink::framework::result_t<void>::failure (
              zlink::framework::framework_error_kind_t::request_failed,
              "route sink should not replace stream sink"));
      },
      zlink::framework::stream_codec_t::message_pack);
    auto preserved_stream_push = stream_sink_gateway.dispatch_bound_session_send (
      stream_sink_ref, "PreservedStreamSinkNotify",
      zlink::message_t::from (std::string ("preserved")));
    const auto preserved_stream_headers = stream_sink_runtime.written_headers (stream_sink);
    if (!preserved_stream_push || preserved_stream_headers.size () != 1
        || preserved_stream_headers[0].packet_name () != "PreservedStreamSinkNotify"
        || preserved_stream_headers[0].codec () != zlink::framework::stream_codec_t::json) {
        return 77;
    }
    zlink::framework::detail::actor_gateway_runtime_t route_sink_gateway;
    auto route_sink_ref = zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string ("entry-node"), "player", "route-user", 4);
    auto route_sink_actor = route_sink_gateway.manager ().bind (route_sink_ref).async ().result ();
    if (!route_sink_actor) {
        return 78;
    }
    bool first_route_sink_used = false;
    bool replacement_route_sink_used = false;
    route_sink_gateway.bind_session_sink (
      route_sink_ref,
      [&first_route_sink_used] (std::string packet_name, const zlink::message_t &payload) {
          first_route_sink_used =
            packet_name == "ReplacedRouteSinkNotify"
            && payload.to_string () == "route-replaced";
          return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
      },
      zlink::framework::stream_codec_t::message_pack);
    route_sink_gateway.bind_session_sink (
      route_sink_ref,
      [&replacement_route_sink_used] (std::string packet_name, const zlink::message_t &payload) {
          replacement_route_sink_used =
            packet_name == "ReplacedRouteSinkNotify"
            && payload.to_string () == "route-replaced";
          return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
      },
      zlink::framework::stream_codec_t::message_pack);
    auto replaced_route_push = route_sink_gateway.dispatch_bound_session_send (
      route_sink_ref, "ReplacedRouteSinkNotify",
      zlink::message_t::from (std::string ("route-replaced")));
    if (!replaced_route_push || first_route_sink_used || !replacement_route_sink_used) {
        return 79;
    }

    zlink::framework::zlink_builder_t dispatcher_builder;
    dispatcher_builder.route_channel ("dispatcher.route");
    dispatcher_builder.add_spot_node ("dispatcher-node")
      .set_routing_id (zlink::routing_id_t::from ("dispatcher-node"));
    zlink::framework::spot_node_snapshot_t dispatcher_spot_node;
    dispatcher_spot_node.name = "dispatcher-node";
    dispatcher_spot_node.accepted_route_channels.push_back (
      zlink::framework::accepted_spot_route_channel_t{"dispatcher.route", {}});
    dispatcher_spot_node.accepted_route_channels.push_back (
      zlink::framework::accepted_spot_route_channel_t{"implicit.dispatcher.route", {}});
    auto dispatchers = zlink::framework::detail::build_route_internal_dispatchers (
      dispatcher_builder, {dispatcher_spot_node}, dispatcher_builder.route_channels (), gateway,
      serializers);
    const auto dispatcher_found = dispatchers.find ("dispatcher.route");
    const auto implicit_dispatcher_found = dispatchers.find ("implicit.dispatcher.route");
    if (dispatcher_found == dispatchers.end () || implicit_dispatcher_found == dispatchers.end ()
        || !dispatcher_found->second->can_handle_request (
          zlink::framework::detail::spot_actor_join_route_request_t::packet_name)
        || !implicit_dispatcher_found->second->can_handle_request (
          zlink::framework::detail::spot_actor_packet_route_request_t::packet_name)) {
        return 45;
    }

    auto bridge_spot = std::make_shared<bridge_spot_t> ();
    auto bridge_location_store =
      std::make_shared<zlink::framework::runtime::in_memory_location_store_t> ();
    auto bridge_app = zlink::framework::app_t::create ();
    auto bridge_node = bridge_app.advanced ().zlink ().add_spot_node ("bridge-node");
    bridge_node.set_routing_id (zlink::routing_id_t::from ("bridge-node"));
    bridge_node.add_spot_resolver (
      "remote-bridge",
      [] (zlink::framework::spot_rid_t spot_rid) -> std::optional<zlink::framework::spot_route_t> {
          if (spot_rid.value () != "remote-node:bridge-room") {
              return std::nullopt;
          }
          return zlink::framework::spot_route_t{
            zlink::framework::node_rid_t::from_string ("remote-node"), std::move (spot_rid),
            "bridge-room"};
      });
    bridge_node.add_actor_factory<bridge_actor_factory_t> ("bridge-player")
      .add_spot<bridge_spot_t> ("bridge-room", [bridge_spot] { return bridge_spot; });
    const auto bridge_room = bridge_node.create_spot ("bridge-room");
    if (bridge_room.state == zlink::framework::spot_create_state_t::rejected
        || bridge_room.spot_rid.empty ()) {
        return 35;
    }
    bridge_app.add_zlink_framework (
      [bridge_location_store] (zlink::framework::zlink_framework_options_t &options) {
          options.add_location_store (bridge_location_store);
      });
    auto remote_actor_owner =
      bridge_location_store
        ->renew_owner_lease ("remote-actor-owner", zlink::routing_id_t::from ("remote-node"),
                             std::chrono::seconds (30))
        .result ();
    if (!remote_actor_owner) {
        return 46;
    }
    auto remote_actor_location =
      bridge_location_store
        ->update_actor (
          zlink::framework::actor_location_t{.actor_id = "dora",
                                             .actor_type = "bridge-player",
                                             .actor_ref = std::nullopt,
                                             .node_rid = zlink::routing_id_t::from ("remote-node"),
                                             .location_kind = zlink::spot_kind::entry,
                                             .spot_mesh_name = "bridge",
                                             .spot_rid = std::nullopt,
                                             .owner_id = "remote-actor-owner"},
          zlink::framework::location_write_intent_t::new_claim)
        .result ();
    if (!remote_actor_location
        || remote_actor_location.value ().status
             != zlink::framework::location_write_status_t::stored) {
        return 47;
    }
    auto bridge_provider = bridge_app.advanced ().services ().build_provider ();
    auto bridge_scope = bridge_provider.create_scope ();
    auto bridge_gateway = bridge_scope.get_required<zlink::framework::actor_gateway_t> ();
    auto bridge_actor = bridge_gateway.manager ().create ("bridge-player", "carol");
    if (!bridge_actor) {
        return 36;
    }
    auto missing_route_join =
      bridge_actor.value ()
        .context ()
        .join_spot (zlink::framework::spot_rid_t::from_string ("remote-node:bridge-room"),
                    bridge_request_t{19})
        .async<bridge_reply_t> ()
        .result ();
    if (missing_route_join
        || missing_route_join.error_kind ()
             != zlink::framework::framework_error_kind_t::spot_route_not_found) {
        return 43;
    }
    auto missing_route_entry_join =
      bridge_actor.value ()
        .context ()
        .join_entry_spot (zlink::framework::node_rid_t::from_string ("remote-node"),
                          zlink::framework::message_t::from (bridge_request_t{29}))
        .async ()
        .result ();
    if (missing_route_entry_join
        || missing_route_entry_join.error_kind ()
             != zlink::framework::framework_error_kind_t::spot_route_not_found) {
        return 44;
    }
    auto resolved_remote_actor =
      bridge_gateway.manager ()
        .bind (zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string ("bridge-node"), "bridge-player", "dora", 1))
        .async ()
        .result ();
    if (!resolved_remote_actor) {
        return 48;
    }
    auto missing_route_relay =
      resolved_remote_actor.value ()
        .relay_request ("bridge.relay", zlink::message_t::from_json (bridge_request_t{31}))
        .async ()
        .result ();
    if (missing_route_relay
        || missing_route_relay.error_kind ()
             != zlink::framework::framework_error_kind_t::spot_route_not_found) {
        return 49;
    }
    auto bridge_join = bridge_actor.value ()
                         .context ()
                         .join_spot (bridge_room.spot_rid, bridge_request_t{17})
                         .async<bridge_reply_t> ()
                         .result ();
    if (!bridge_join) {
        std::cerr << "bridge join failed: "
                  << (bridge_join.error () ? bridge_join.error ()->what () : "unknown") << '\n';
        return 37;
    }
    const auto *bridge_join_accepted =
      std::get_if<zlink::framework::actor_join_accepted_t<bridge_reply_t>> (
        &bridge_join.value ());
    if (bridge_join_accepted == nullptr || bridge_join_accepted->reply.value != "joined:carol"
        || bridge_spot->join_value != 17 || bridge_spot->joined_count != 1
        || bridge_join_accepted->actor.node_rid ().empty ()) {
        std::cerr << "bridge join mismatch: accepted="
                  << (bridge_join_accepted != nullptr ? "true" : "false")
                  << " join_value=" << bridge_spot->join_value
                  << " joined_count=" << bridge_spot->joined_count << '\n';
        return 37;
    }
    auto joined_bridge_actor =
      bridge_gateway.manager ().bind (bridge_join_accepted->actor).async ().result ();
    if (!joined_bridge_actor) {
        return 38;
    }
    auto bridge_relay =
      joined_bridge_actor.value ()
        .relay_request ("bridge.relay", zlink::message_t::from_json (bridge_request_t{23}))
        .async ()
        .result ();
    if (!bridge_relay) {
        return 39;
    }
    if (bridge_relay.value ().parse_json<bridge_reply_t> ().value != "carol:23") {
        return 41;
    }
    if (bridge_spot->relay_value != 23) {
        return 42;
    }
    (void) joined_bridge_actor.value ()
      .relay ("bridge.signal", zlink::message_t::from_json (bridge_request_t{24}))
      .result ();
    if (bridge_spot->signal_value != 24) {
        return 45;
    }
    (void) joined_bridge_actor.value ().notify_disconnected ().result ();
    bridge_scope.close ();
    bridge_provider.close ();

    return 0;
}
