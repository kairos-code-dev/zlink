/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include "runtime/actors/actor_gateway_runtime.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace
{

struct typed_session_push_t
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

} // namespace

int main ()
{
    using zlink::framework::framework_error_kind_t;

    zlink::framework::zlink_builder_t zlink;
    zlink.add_spot_node ("session-actors").bind ("tcp://0.0.0.0:7101").enable_actor_gateway ();
    zlink.stream ("client-stream")
      .bind ("tcp://0.0.0.0:9200")
      .register_session ("client")
      .attach_actor_gateway ("session-actors");

    if (zlink.streams ().size () != 1 || !zlink.streams ()[0].actor_gateway_spot_node_name
        || *zlink.streams ()[0].actor_gateway_spot_node_name != "session-actors") {
        return 1;
    }

    zlink::framework::detail::actor_gateway_runtime_t gateway;
    zlink::framework::serializer_registry_t serializers;
    serializers.add_json<join_request_t> ().add_json<join_reply_t> ();
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
    gateway.bind_serializers (serializers);
    auto manager = gateway.manager ();
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

    zlink::framework::actor_ref_t remote_ref (
      zlink::framework::node_rid_t::from_string ("remote-node"), "player", "bob", 7);
    auto bound = manager.bind (remote_ref).async ().result ();
    if (!bound || !gateway.actor_bound ("bob") || bound.value ().ref ().generation () != 7) {
        return 6;
    }

    zlink::framework::stream_metadata_t metadata;
    metadata.with ("trace", "t1");
    zlink::framework::stream_header_t header (
      zlink::framework::stream_message_kind_t::send, zlink::framework::stream_codec_t::json,
      zlink::framework::stream_header_flags_t::has_metadata, std::nullopt, "move", metadata);
    const auto payload = zlink::framework::message_t::from (std::string ("payload"));
    auto relay_with_header = [&] (zlink::framework::session_actor_t &actor,
                                  const zlink::framework::message_t &message) {
        zlink::framework::detail::enter_stream_relay_dispatch (header);
        auto result = actor.relay (message).async ().result ();
        zlink::framework::detail::exit_stream_relay_dispatch ();
        return result;
    };
    auto relay_request_with_header = [&] (zlink::framework::session_actor_t &actor,
                                          const zlink::framework::message_t &message) {
        zlink::framework::detail::enter_stream_relay_dispatch (header);
        auto result = actor.relay_request (message).async ().result ();
        zlink::framework::detail::exit_stream_relay_dispatch ();
        return result;
    };
    auto relay = relay_with_header (bound.value (), payload);
    if (!relay || gateway.relayed_frames ().size () != 1
        || gateway.relayed_frames ()[0].payload.to_string () != "payload"
        || payload.decode<std::string> (serializers) != "payload") {
        return 7;
    }

    bool relay_dispatch_seen = false;
    gateway.on_relay ([&] (const zlink::framework::actor_ref_t &actor,
                           zlink::framework::actor_context_t,
                           const zlink::framework::stream_header_t &received_header,
                           const zlink::message_t &received_payload) {
        relay_dispatch_seen = actor.actor_id () == "bob" && received_header.packet_name () == "move"
                              && received_payload.to_string () == "payload";
        return zlink::framework::result_t<std::optional<zlink::message_t>>::success (
          zlink::message_t::from (std::string ("relay-reply")));
    });
    auto dispatched_relay = relay_with_header (bound.value (), payload);
    if (!dispatched_relay || !relay_dispatch_seen || gateway.relayed_frames ().size () != 1) {
        return 22;
    }
    auto relay_request = relay_request_with_header (bound.value (), payload);
    if (!relay_request || relay_request.value ().decode<std::string> (serializers) != "relay-reply") {
        return 23;
    }

    zlink::framework::session_actor_t unbound;
    auto missing_relay = relay_with_header (unbound, payload);
    if (missing_relay
        || missing_relay.error_kind () != framework_error_kind_t::actor_route_not_found) {
        return 8;
    }

    auto push_result =
      bound.value ().context ().bound_session ().send (payload).async ().result ();
    if (!push_result || gateway.bound_session_pushes ().size () != 1
        || gateway.bound_session_pushes ()[0].payload.to_string () != "payload") {
        return 9;
    }

    auto typed_push = bound.value ()
                        .context ()
                        .bound_session ()
                        .send (typed_session_push_t{"typed-payload"})
                        .async ()
                        .result ();
    if (!typed_push || gateway.bound_session_pushes ().size () != 2
        || gateway.bound_session_pushes ()[1].payload.to_string () != "typed-payload") {
        return 13;
    }

    auto disconnect = bound.value ().bound_session ().disconnect ().async ().result ();
    if (!disconnect || gateway.actor_bound ("bob") || !gateway.actor_disconnected ("bob")) {
        return 10;
    }
    auto disconnected_push = bound.value ().bound_session ().send (payload).async ().result ();
    if (disconnected_push
        || disconnected_push.error_kind () != framework_error_kind_t::disconnected) {
        return 16;
    }
    auto disconnected_relay = relay_with_header (bound.value (), payload);
    if (disconnected_relay
        || disconnected_relay.error_kind () != framework_error_kind_t::disconnected
        || payload.decode<std::string> (serializers) != "payload") {
        return 17;
    }

    auto rebound = manager.bind (remote_ref).async ().result ();
    if (!rebound || !gateway.actor_bound ("bob")) {
        return 11;
    }

    auto actor_disconnect = rebound.value ().notify_disconnected ().async ().result ();
    if (!actor_disconnect || gateway.actor_bound ("bob") || !gateway.actor_disconnected ("bob")) {
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
    auto dispatched_disconnect = rebound.value ().notify_disconnected ().async ().result ();
    if (!dispatched_disconnect || !disconnect_dispatch_seen || gateway.actor_bound ("bob")
        || !gateway.actor_disconnected ("bob")) {
        return 33;
    }
    rebound = manager.bind (remote_ref).async ().result ();
    if (!rebound || !gateway.actor_bound ("bob")) {
        return 34;
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
    auto actor_context = rebound.value ().context ();
    const auto typed_join = actor_context
                              .join_spot (zlink::framework::spot_rid_t::from_string ("typed-match"),
                                          join_request_t{"typed-match"})
                              .async<join_reply_t> ()
                              .result ();
    if (!typed_join || !typed_join_seen || typed_join.value ().reply.mark != "X") {
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
    const auto join_spot = actor_context
                             .join_spot (zlink::framework::spot_rid_t::from_string ("match-1"),
                                         zlink::framework::message_t::from (
                                           join_request_t{"match-1"}))
                             .async ()
                             .result ();
    if (!join_spot || !join_spot_seen || join_spot.value ().result_code != 0
        || join_spot.value ().actor.generation () != 8
        || join_spot.value ().reply.decode<join_reply_t> (serializers).mark != "O") {
        return 14;
    }
    const auto stale_relay = relay_with_header (rebound.value (), payload);
    if (stale_relay || stale_relay.error_kind () != framework_error_kind_t::actor_stale_generation
        || payload.decode<std::string> (serializers) != "payload") {
        return 20;
    }
    const auto stale_push = rebound.value ().bound_session ().send (payload).async ().result ();
    if (stale_push || stale_push.error_kind () != framework_error_kind_t::actor_stale_generation) {
        return 21;
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
    if (!entry_join || !entry_join_seen || entry_join.value ().actor.generation () != 9
        || entry_join.value ().reply.decode<std::string> (serializers) != "joined") {
        return 15;
    }
    manager.unbind_session ("bob");
    if (gateway.actor_bound ("bob") || !gateway.actor_disconnected ("bob")) {
        return 12;
    }
    auto rebound_after_entry = manager.bind (entry_join.value ().actor).async ().result ();
    if (!rebound_after_entry || !gateway.actor_bound ("bob")
        || gateway.actor_disconnected ("bob")) {
        return 24;
    }
    const auto destroy_bound = gateway.destroy_actor (entry_join.value ().actor);
    if (!destroy_bound || gateway.actor_bound ("bob") || gateway.actor_disconnected ("bob")) {
        return 25;
    }
    const auto post_destroy_push =
      rebound_after_entry.value ().bound_session ().send (payload).async ().result ();
    if (post_destroy_push
        || post_destroy_push.error_kind () != framework_error_kind_t::actor_session_not_bound) {
        return 26;
    }
    const auto post_destroy_relay =
      relay_with_header (rebound_after_entry.value (), payload);
    if (post_destroy_relay
        || post_destroy_relay.error_kind () != framework_error_kind_t::actor_route_not_found) {
        return 27;
    }

    return 0;
}
