/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/framework/contracts/actors/actor.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace zlink::framework::detail
{

struct spot_actor_join_route_request_t
{
    static constexpr const char *packet_name = "__zlink.spot.actor.join";

    std::string actor_node_rid;
    std::string actor_type;
    std::string actor_id;
    std::uint64_t actor_generation = 0;
    std::string spot_rid;
    std::vector<std::uint8_t> payload;
};

struct spot_actor_join_route_reply_t
{
    int result_code = 0;
    std::string actor_node_rid;
    std::string actor_type;
    std::string actor_id;
    std::uint64_t actor_generation = 0;
    std::vector<std::uint8_t> payload;
};

struct spot_actor_packet_route_request_t
{
    static constexpr const char *packet_name = "__zlink.spot.actor.packet";

    std::string actor_node_rid;
    std::string actor_type;
    std::string actor_id;
    std::uint64_t actor_generation = 0;
    std::string spot_rid;
    std::string packet_name_value;
    std::string content_type = "application/json";
    std::map<std::string, std::string> metadata;
    std::vector<std::uint8_t> payload;
};

struct spot_actor_packet_route_reply_t
{
    bool actor_ref_present = false;
    std::string actor_node_rid;
    std::string actor_type;
    std::string actor_id;
    std::uint64_t actor_generation = 0;
    bool has_reply = false;
    std::vector<std::uint8_t> payload;
};

struct spot_actor_disconnect_route_request_t
{
    static constexpr const char *packet_name = "__zlink.spot.actor.disconnect";

    std::string actor_node_rid;
    std::string actor_type;
    std::string actor_id;
    std::uint64_t actor_generation = 0;
};

struct spot_actor_disconnect_route_reply_t
{
    bool accepted = true;
};

struct actor_bound_session_route_request_t
{
    static constexpr const char *packet_name = "__zlink.actor.boundSession.send";

    std::string actor_node_rid;
    std::string actor_type;
    std::string actor_id;
    std::uint64_t actor_generation = 0;
    std::string packet_name_value;
    std::vector<std::uint8_t> payload;
};

struct actor_bound_session_route_reply_t
{
    bool accepted = true;
};

spot_actor_join_route_request_t make_spot_actor_join_route_request (
  const actor_ref_t &actor_ref, spot_rid_t spot_rid, const zlink::message_t &payload);

actor_ref_t actor_ref_from_spot_route (const spot_actor_join_route_request_t &request);

spot_actor_join_route_reply_t make_spot_actor_join_route_reply (const actor_join_reply_t &reply);

actor_join_reply_t actor_join_reply_from_spot_route (const spot_actor_join_route_reply_t &reply);

spot_actor_packet_route_request_t
make_spot_actor_packet_route_request (const actor_ref_t &actor_ref,
                                      spot_rid_t spot_rid,
                                      std::string_view packet_name,
                                      const zlink::message_t &payload,
                                      const spot_actor_message_metadata_t &metadata);

actor_ref_t actor_ref_from_spot_route (const spot_actor_packet_route_request_t &request);

spot_actor_disconnect_route_request_t
make_spot_actor_disconnect_route_request (const actor_ref_t &actor_ref);

actor_ref_t actor_ref_from_spot_route (const spot_actor_disconnect_route_request_t &request);

actor_bound_session_route_request_t make_actor_bound_session_route_request (
  const actor_ref_t &actor_ref, std::string_view packet_name, const zlink::message_t &payload);

actor_ref_t actor_ref_from_bound_session_route (const actor_bound_session_route_request_t &request);

void register_spot_route_packet_serializers (serializer_registry_t &serializers);

} // namespace zlink::framework::detail
