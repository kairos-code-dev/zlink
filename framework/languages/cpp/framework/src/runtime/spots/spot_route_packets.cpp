/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/spots/spot_route_packets.hpp"

#include "runtime/streams/stream_runtime.hpp"

#include <nlohmann/json.hpp>

#include <typeindex>
#include <utility>

namespace zlink::framework::detail
{

result_t<zlink::message_t> encode_actor_bound_session_frame (
  stream_codec_t codec,
  std::string packet_name,
  const zlink::message_t &payload)
{
    stream_runtime_t stream_runtime (std::make_shared<stream_runtime_state_t> ());
    const stream_header_t header (stream_message_kind_t::send, codec,
                                  stream_header_flags_t::none, std::nullopt,
                                  std::move (packet_name));
    auto encoded_header = stream_runtime.encode_header (header);
    if (!encoded_header) {
        return result_t<zlink::message_t>::failure (
          encoded_header.error_kind (), encoded_header.error () ? encoded_header.error ()->what ()
                                                                : "STREAM header encode failed");
    }
    const auto payload_bytes = payload.to_bytes ();
    const auto header_size = encoded_header.value ().size ();
    std::vector<std::uint8_t> frame;
    frame.reserve (6 + header_size + payload_bytes.size ());
    frame.push_back (static_cast<std::uint8_t> ((header_size >> 8) & 0xff));
    frame.push_back (static_cast<std::uint8_t> (header_size & 0xff));
    frame.push_back (static_cast<std::uint8_t> ((payload_bytes.size () >> 24) & 0xff));
    frame.push_back (static_cast<std::uint8_t> ((payload_bytes.size () >> 16) & 0xff));
    frame.push_back (static_cast<std::uint8_t> ((payload_bytes.size () >> 8) & 0xff));
    frame.push_back (static_cast<std::uint8_t> (payload_bytes.size () & 0xff));
    frame.insert (frame.end (), encoded_header.value ().begin (), encoded_header.value ().end ());
    frame.insert (frame.end (), payload_bytes.begin (), payload_bytes.end ());
    return result_t<zlink::message_t>::success (zlink::message_t::from (std::move (frame)));
}

void to_json (nlohmann::json &json, const spot_actor_admission_route_request_t &value)
{
    json = nlohmann::json{{"transferId", value.transfer_id},
                          {"actorNodeRid", value.actor_node_rid},
                          {"actorType", value.actor_type},
                          {"actorId", value.actor_id},
                          {"actorGeneration", value.actor_generation},
                          {"completionOperationIdHigh",
                           value.completion_operation_id_high},
                          {"completionOperationIdLow",
                           value.completion_operation_id_low},
                          {"sourceSpotId", value.source_spot_id},
                          {"targetSpotId", value.target_spot_id},
                          {"payload", value.payload}};
}

void from_json (const nlohmann::json &json, spot_actor_admission_route_request_t &value)
{
    value.transfer_id = json.at ("transferId").get<std::string> ();
    value.actor_node_rid = json.at ("actorNodeRid").get<std::string> ();
    value.actor_type = json.at ("actorType").get<std::string> ();
    value.actor_id = json.at ("actorId").get<std::string> ();
    value.actor_generation = json.at ("actorGeneration").get<std::uint64_t> ();
    value.completion_operation_id_high =
      json.value ("completionOperationIdHigh", std::uint64_t{0});
    value.completion_operation_id_low =
      json.value ("completionOperationIdLow", std::uint64_t{0});
    value.source_spot_id = json.at ("sourceSpotId").get<std::string> ();
    value.target_spot_id = json.at ("targetSpotId").get<std::string> ();
    value.payload = json.at ("payload").get<std::vector<std::uint8_t>> ();
}

void to_json (nlohmann::json &json, const spot_actor_admission_route_reply_t &value)
{
    json = nlohmann::json{
      {"accepted", value.accepted},
      {"payload", value.payload},
      {"completionRootReference", value.completion_root_reference},
      {"completionRootChecksum", value.completion_root_checksum}};
}

void from_json (const nlohmann::json &json, spot_actor_admission_route_reply_t &value)
{
    value.accepted = json.at ("accepted").get<bool> ();
    value.payload = json.at ("payload").get<std::vector<std::uint8_t>> ();
    value.completion_root_reference =
      json.value ("completionRootReference", "");
    value.completion_root_checksum =
      json.value ("completionRootChecksum", std::uint32_t{0});
}

void to_json (nlohmann::json &json, const spot_actor_handoff_packet_t &value)
{
    json = nlohmann::json{{"packetName", value.packet_name_value},
                          {"payload", value.payload},
                          {"contentType", value.content_type},
                          {"metadata", value.metadata},
                          {"isRequest", value.is_request}};
}

void from_json (const nlohmann::json &json, spot_actor_handoff_packet_t &value)
{
    value.packet_name_value = json.at ("packetName").get<std::string> ();
    value.payload = json.at ("payload").get<std::vector<std::uint8_t>> ();
    value.content_type = json.value ("contentType", "");
    value.metadata = json.value ("metadata", std::map<std::string, std::string>{});
    value.is_request = json.value ("isRequest", false);
}

void to_json (nlohmann::json &json, const spot_actor_commit_route_request_t &value)
{
    json = nlohmann::json{{"transferId", value.transfer_id},
                          {"actorNodeRid", value.actor_node_rid},
                          {"actorType", value.actor_type},
                          {"actorId", value.actor_id},
                          {"actorGeneration", value.actor_generation},
                          {"completionRootReference",
                           value.completion_root_reference},
                          {"completionRootChecksum",
                           value.completion_root_checksum},
                          {"targetSpotId", value.target_spot_id},
                          {"boundSessionNodeRid", value.bound_session_node_rid},
                          {"boundSessionRid", value.bound_session_rid},
                          {"transferState", value.transfer_state},
                          {"handoffBacklog", value.handoff_backlog},
                          {"coreTransfer", value.core_transfer},
                          {"coreTransferIdHigh", value.core_transfer_id_high},
                          {"coreTransferIdLow", value.core_transfer_id_low},
                          {"coreMembershipEpoch", value.core_membership_epoch},
                          {"coreFinalSequence", value.core_final_sequence},
                          {"coreReserveMessageCount", value.core_reserve_message_count},
                          {"coreReserveByteCount", value.core_reserve_byte_count},
                          {"prepare", value.prepare},
                          {"finalize", value.finalize}};
}

void from_json (const nlohmann::json &json, spot_actor_commit_route_request_t &value)
{
    value.transfer_id = json.at ("transferId").get<std::string> ();
    value.actor_node_rid = json.at ("actorNodeRid").get<std::string> ();
    value.actor_type = json.at ("actorType").get<std::string> ();
    value.actor_id = json.at ("actorId").get<std::string> ();
    value.actor_generation = json.at ("actorGeneration").get<std::uint64_t> ();
    value.completion_root_reference =
      json.value ("completionRootReference", "");
    value.completion_root_checksum =
      json.value ("completionRootChecksum", std::uint32_t{0});
    value.target_spot_id = json.at ("targetSpotId").get<std::string> ();
    value.bound_session_node_rid = json.value ("boundSessionNodeRid", "");
    value.bound_session_rid = json.value ("boundSessionRid", "");
    value.transfer_state = json.at ("transferState").get<std::vector<std::uint8_t>> ();
    value.handoff_backlog =
      json.value ("handoffBacklog", std::vector<spot_actor_handoff_packet_t>{});
    value.core_transfer = json.value ("coreTransfer", false);
    value.core_transfer_id_high = json.value ("coreTransferIdHigh", std::uint64_t{0});
    value.core_transfer_id_low = json.value ("coreTransferIdLow", std::uint64_t{0});
    value.core_membership_epoch = json.value ("coreMembershipEpoch", std::uint64_t{0});
    value.core_final_sequence = json.value ("coreFinalSequence", std::uint64_t{0});
    value.core_reserve_message_count =
      json.value ("coreReserveMessageCount", std::uint64_t{0});
    value.core_reserve_byte_count =
      json.value ("coreReserveByteCount", std::uint64_t{0});
    value.prepare = json.value ("prepare", false);
    value.finalize = json.value ("finalize", false);
}

void to_json (nlohmann::json &json, const spot_actor_join_route_request_t &value)
{
    json = nlohmann::json{{"actorNodeRid", value.actor_node_rid},
                          {"actorType", value.actor_type},
                          {"actorId", value.actor_id},
                          {"actorGeneration", value.actor_generation},
                          {"spotId", value.spot_id},
                          {"payload", value.payload},
                          {"actorSnapshotPresent", value.actor_snapshot_present},
                          {"actorSnapshot", value.actor_snapshot}};
}

void from_json (const nlohmann::json &json, spot_actor_join_route_request_t &value)
{
    value.actor_node_rid = json.at ("actorNodeRid").get<std::string> ();
    value.actor_type = json.at ("actorType").get<std::string> ();
    value.actor_id = json.at ("actorId").get<std::string> ();
    value.actor_generation = json.at ("actorGeneration").get<std::uint64_t> ();
    value.spot_id = json.at ("spotId").get<std::string> ();
    value.payload = json.at ("payload").get<std::vector<std::uint8_t>> ();
    value.actor_snapshot_present = json.value ("actorSnapshotPresent", false);
    value.actor_snapshot = json.value ("actorSnapshot", std::vector<std::uint8_t>{});
}

void to_json (nlohmann::json &json, const spot_actor_join_route_reply_t &value)
{
    json = nlohmann::json{{"resultCode", value.result_code},
                          {"actorNodeRid", value.actor_node_rid},
                          {"actorType", value.actor_type},
                          {"actorId", value.actor_id},
                          {"actorGeneration", value.actor_generation},
                          {"payload", value.payload}};
}

void from_json (const nlohmann::json &json, spot_actor_join_route_reply_t &value)
{
    value.result_code = json.at ("resultCode").get<int> ();
    value.actor_node_rid = json.at ("actorNodeRid").get<std::string> ();
    value.actor_type = json.at ("actorType").get<std::string> ();
    value.actor_id = json.at ("actorId").get<std::string> ();
    value.actor_generation = json.at ("actorGeneration").get<std::uint64_t> ();
    value.payload = json.at ("payload").get<std::vector<std::uint8_t>> ();
}

void to_json (nlohmann::json &json, const spot_actor_packet_route_request_t &value)
{
    json = nlohmann::json{{"actorNodeRid", value.actor_node_rid},
                          {"actorType", value.actor_type},
                          {"actorId", value.actor_id},
                          {"actorGeneration", value.actor_generation},
                          {"spotId", value.spot_id},
                          {"packetName", value.packet_name_value},
                          {"contentType", value.content_type},
                          {"metadata", value.metadata},
                          {"payload", value.payload}};
}

void from_json (const nlohmann::json &json, spot_actor_packet_route_request_t &value)
{
    value.actor_node_rid = json.at ("actorNodeRid").get<std::string> ();
    value.actor_type = json.at ("actorType").get<std::string> ();
    value.actor_id = json.at ("actorId").get<std::string> ();
    value.actor_generation = json.at ("actorGeneration").get<std::uint64_t> ();
    value.spot_id = json.at ("spotId").get<std::string> ();
    value.packet_name_value = json.at ("packetName").get<std::string> ();
    value.content_type = json.value ("contentType", "application/json");
    value.metadata = json.value ("metadata", std::map<std::string, std::string>{});
    value.payload = json.at ("payload").get<std::vector<std::uint8_t>> ();
}

void to_json (nlohmann::json &json, const spot_actor_packet_route_reply_t &value)
{
    json = nlohmann::json{{"actorRefPresent", value.actor_ref_present},
                          {"actorNodeRid", value.actor_node_rid},
                          {"actorType", value.actor_type},
                          {"actorId", value.actor_id},
                          {"actorGeneration", value.actor_generation},
                          {"hasReply", value.has_reply},
                          {"payload", value.payload}};
}

void from_json (const nlohmann::json &json, spot_actor_packet_route_reply_t &value)
{
    value.actor_ref_present = json.value ("actorRefPresent", false);
    value.actor_node_rid = json.value ("actorNodeRid", "");
    value.actor_type = json.value ("actorType", "");
    value.actor_id = json.value ("actorId", "");
    value.actor_generation = json.value ("actorGeneration", std::uint64_t{0});
    value.has_reply = json.at ("hasReply").get<bool> ();
    value.payload = json.at ("payload").get<std::vector<std::uint8_t>> ();
}

void to_json (nlohmann::json &json, const spot_actor_disconnect_route_request_t &value)
{
    json = nlohmann::json{{"actorNodeRid", value.actor_node_rid},
                          {"actorType", value.actor_type},
                          {"actorId", value.actor_id},
                          {"actorGeneration", value.actor_generation}};
}

void from_json (const nlohmann::json &json, spot_actor_disconnect_route_request_t &value)
{
    value.actor_node_rid = json.at ("actorNodeRid").get<std::string> ();
    value.actor_type = json.at ("actorType").get<std::string> ();
    value.actor_id = json.at ("actorId").get<std::string> ();
    value.actor_generation = json.at ("actorGeneration").get<std::uint64_t> ();
}

void to_json (nlohmann::json &json, const spot_actor_disconnect_route_reply_t &value)
{
    json = nlohmann::json{{"accepted", value.accepted}};
}

void from_json (const nlohmann::json &json, spot_actor_disconnect_route_reply_t &value)
{
    value.accepted = json.value ("accepted", true);
}

void to_json (nlohmann::json &json, const actor_bound_session_route_request_t &value)
{
    json = nlohmann::json{{"actorNodeRid", value.actor_node_rid},
                          {"actorType", value.actor_type},
                          {"actorId", value.actor_id},
                          {"actorGeneration", value.actor_generation},
                          {"packetName", value.packet_name_value},
                          {"payload", value.payload}};
}

void from_json (const nlohmann::json &json, actor_bound_session_route_request_t &value)
{
    value.actor_node_rid = json.at ("actorNodeRid").get<std::string> ();
    value.actor_type = json.at ("actorType").get<std::string> ();
    value.actor_id = json.at ("actorId").get<std::string> ();
    value.actor_generation = json.at ("actorGeneration").get<std::uint64_t> ();
    value.packet_name_value = json.at ("packetName").get<std::string> ();
    value.payload = json.at ("payload").get<std::vector<std::uint8_t>> ();
}

void to_json (nlohmann::json &json, const actor_bound_session_bind_route_request_t &value)
{
    json = nlohmann::json{{"actorNodeRid", value.actor_node_rid},
                          {"actorType", value.actor_type},
                          {"actorId", value.actor_id},
                          {"actorGeneration", value.actor_generation},
                          {"sessionNodeRid", value.session_node_rid}};
}

void from_json (const nlohmann::json &json, actor_bound_session_bind_route_request_t &value)
{
    value.actor_node_rid = json.at ("actorNodeRid").get<std::string> ();
    value.actor_type = json.at ("actorType").get<std::string> ();
    value.actor_id = json.at ("actorId").get<std::string> ();
    value.actor_generation = json.at ("actorGeneration").get<std::uint64_t> ();
    value.session_node_rid = json.at ("sessionNodeRid").get<std::string> ();
}

void to_json (nlohmann::json &json, const actor_bound_session_route_reply_t &value)
{
    json = nlohmann::json{{"accepted", value.accepted}};
}

void from_json (const nlohmann::json &json, actor_bound_session_route_reply_t &value)
{
    value.accepted = json.value ("accepted", true);
}

zlink::message_t message_from_bytes (const std::vector<std::uint8_t> &bytes)
{
    return zlink::message_t::from (bytes);
}

spot_actor_join_route_request_t
make_spot_actor_join_route_request (const actor_ref_t &actor_ref,
                                    spot_id_t spot_id,
                                    const zlink::message_t &payload,
                                    const std::optional<zlink::message_t> &actor_snapshot)
{
    return spot_actor_join_route_request_t{
      .actor_node_rid = std::string (actor_ref.node_rid ().value ()),
      .actor_type = std::string (actor_ref.actor_type ()),
      .actor_id = std::string (actor_ref.actor_id ()),
      .actor_generation = actor_ref.generation (),
      .spot_id = std::string (spot_id),
      .payload = payload.to_bytes (),
      .actor_snapshot_present = actor_snapshot.has_value (),
      .actor_snapshot = actor_snapshot ? actor_snapshot->to_bytes () : std::vector<std::uint8_t>{}};
}

actor_ref_t actor_ref_from_spot_route (const spot_actor_join_route_request_t &request)
{
    return actor_ref_t (node_rid_t::from_string (request.actor_node_rid), request.actor_type,
                        request.actor_id, request.actor_generation);
}

actor_ref_t actor_ref_from_spot_route (const spot_actor_admission_route_request_t &request)
{
    return actor_ref_t (node_rid_t::from_string (request.actor_node_rid), request.actor_type,
                        request.actor_id, request.actor_generation);
}

actor_ref_t actor_ref_from_spot_route (const spot_actor_commit_route_request_t &request)
{
    return actor_ref_t (node_rid_t::from_string (request.actor_node_rid), request.actor_type,
                        request.actor_id, request.actor_generation);
}

spot_actor_join_route_reply_t make_spot_actor_join_route_reply (const actor_join_reply_t &reply)
{
    return spot_actor_join_route_reply_t{.result_code = reply.result_code,
                                         .actor_node_rid =
                                           std::string (reply.actor.node_rid ().value ()),
                                         .actor_type = std::string (reply.actor.actor_type ()),
                                         .actor_id = std::string (reply.actor.actor_id ()),
                                         .actor_generation = reply.actor.generation (),
                                         .payload = reply.reply.to_bytes ()};
}

actor_join_reply_t actor_join_reply_from_spot_route (const spot_actor_join_route_reply_t &reply)
{
    return actor_join_reply_t{reply.result_code,
                              actor_ref_t (node_rid_t::from_string (reply.actor_node_rid),
                                           reply.actor_type, reply.actor_id,
                                           reply.actor_generation),
                              message_from_bytes (reply.payload)};
}

spot_actor_packet_route_request_t
make_spot_actor_packet_route_request (const actor_ref_t &actor_ref,
                                      spot_id_t spot_id,
                                      std::string_view packet_name,
                                      const zlink::message_t &payload,
                                      const spot_actor_message_metadata_t &metadata)
{
    return spot_actor_packet_route_request_t{.actor_node_rid =
                                               std::string (actor_ref.node_rid ().value ()),
                                             .actor_type = std::string (actor_ref.actor_type ()),
                                             .actor_id = std::string (actor_ref.actor_id ()),
                                             .actor_generation = actor_ref.generation (),
                                             .spot_id = std::string (spot_id),
                                             .packet_name_value = std::string (packet_name),
                                             .content_type = metadata.content_type,
                                             .metadata = metadata.values,
                                             .payload = payload.to_bytes ()};
}

actor_ref_t actor_ref_from_spot_route (const spot_actor_packet_route_request_t &request)
{
    return actor_ref_t (node_rid_t::from_string (request.actor_node_rid), request.actor_type,
                        request.actor_id, request.actor_generation);
}

spot_actor_disconnect_route_request_t
make_spot_actor_disconnect_route_request (const actor_ref_t &actor_ref)
{
    return spot_actor_disconnect_route_request_t{
      .actor_node_rid = std::string (actor_ref.node_rid ().value ()),
      .actor_type = std::string (actor_ref.actor_type ()),
      .actor_id = std::string (actor_ref.actor_id ()),
      .actor_generation = actor_ref.generation ()};
}

actor_ref_t actor_ref_from_spot_route (const spot_actor_disconnect_route_request_t &request)
{
    return actor_ref_t (node_rid_t::from_string (request.actor_node_rid), request.actor_type,
                        request.actor_id, request.actor_generation);
}

actor_bound_session_route_request_t make_actor_bound_session_route_request (
  const actor_ref_t &actor_ref, std::string_view packet_name, const zlink::message_t &payload)
{
    return actor_bound_session_route_request_t{.actor_node_rid =
                                                 std::string (actor_ref.node_rid ().value ()),
                                               .actor_type = std::string (actor_ref.actor_type ()),
                                               .actor_id = std::string (actor_ref.actor_id ()),
                                               .actor_generation = actor_ref.generation (),
                                               .packet_name_value = std::string (packet_name),
                                               .payload = payload.to_bytes ()};
}

actor_ref_t actor_ref_from_bound_session_route (const actor_bound_session_route_request_t &request)
{
    return actor_ref_t (node_rid_t::from_string (request.actor_node_rid), request.actor_type,
                        request.actor_id, request.actor_generation);
}

actor_ref_t
actor_ref_from_bound_session_route (const actor_bound_session_bind_route_request_t &request)
{
    return actor_ref_t (node_rid_t::from_string (request.actor_node_rid), request.actor_type,
                        request.actor_id, request.actor_generation);
}

void register_spot_route_packet_serializers (serializer_registry_t &serializers)
{
    if (!serializers.contains (std::type_index (typeid (spot_actor_admission_route_request_t)))) {
        serializers.add<spot_actor_admission_route_request_t> (
          [] (const spot_actor_admission_route_request_t &value) {
              return encoded_payload_t::from_string (nlohmann::json (value).dump ());
          },
          [] (const encoded_payload_t &payload) {
              return nlohmann::json::parse (payload.to_string ())
                .get<spot_actor_admission_route_request_t> ();
          });
    }
    if (!serializers.contains (std::type_index (typeid (spot_actor_admission_route_reply_t)))) {
        serializers.add<spot_actor_admission_route_reply_t> (
          [] (const spot_actor_admission_route_reply_t &value) {
              return encoded_payload_t::from_string (nlohmann::json (value).dump ());
          },
          [] (const encoded_payload_t &payload) {
              return nlohmann::json::parse (payload.to_string ())
                .get<spot_actor_admission_route_reply_t> ();
          });
    }
    if (!serializers.contains (std::type_index (typeid (spot_actor_commit_route_request_t)))) {
        serializers.add<spot_actor_commit_route_request_t> (
          [] (const spot_actor_commit_route_request_t &value) {
              return encoded_payload_t::from_string (nlohmann::json (value).dump ());
          },
          [] (const encoded_payload_t &payload) {
              return nlohmann::json::parse (payload.to_string ())
                .get<spot_actor_commit_route_request_t> ();
          });
    }
    if (!serializers.contains (std::type_index (typeid (spot_actor_join_route_request_t)))) {
        serializers.add<spot_actor_join_route_request_t> (
          [] (const spot_actor_join_route_request_t &value) {
              return encoded_payload_t::from_string (nlohmann::json (value).dump ());
          },
          [] (const encoded_payload_t &payload) {
              return nlohmann::json::parse (payload.to_string ())
                .get<spot_actor_join_route_request_t> ();
          });
    }
    if (!serializers.contains (std::type_index (typeid (spot_actor_join_route_reply_t)))) {
        serializers.add<spot_actor_join_route_reply_t> (
          [] (const spot_actor_join_route_reply_t &value) {
              return encoded_payload_t::from_string (nlohmann::json (value).dump ());
          },
          [] (const encoded_payload_t &payload) {
              return nlohmann::json::parse (payload.to_string ())
                .get<spot_actor_join_route_reply_t> ();
          });
    }
    if (!serializers.contains (std::type_index (typeid (spot_actor_packet_route_request_t)))) {
        serializers.add<spot_actor_packet_route_request_t> (
          [] (const spot_actor_packet_route_request_t &value) {
              return encoded_payload_t::from_string (nlohmann::json (value).dump ());
          },
          [] (const encoded_payload_t &payload) {
              return nlohmann::json::parse (payload.to_string ())
                .get<spot_actor_packet_route_request_t> ();
          });
    }
    if (!serializers.contains (std::type_index (typeid (spot_actor_packet_route_reply_t)))) {
        serializers.add<spot_actor_packet_route_reply_t> (
          [] (const spot_actor_packet_route_reply_t &value) {
              return encoded_payload_t::from_string (nlohmann::json (value).dump ());
          },
          [] (const encoded_payload_t &payload) {
              return nlohmann::json::parse (payload.to_string ())
                .get<spot_actor_packet_route_reply_t> ();
          });
    }
    if (!serializers.contains (std::type_index (typeid (spot_actor_disconnect_route_request_t)))) {
        serializers.add<spot_actor_disconnect_route_request_t> (
          [] (const spot_actor_disconnect_route_request_t &value) {
              return encoded_payload_t::from_string (nlohmann::json (value).dump ());
          },
          [] (const encoded_payload_t &payload) {
              return nlohmann::json::parse (payload.to_string ())
                .get<spot_actor_disconnect_route_request_t> ();
          });
    }
    if (!serializers.contains (std::type_index (typeid (spot_actor_disconnect_route_reply_t)))) {
        serializers.add<spot_actor_disconnect_route_reply_t> (
          [] (const spot_actor_disconnect_route_reply_t &value) {
              return encoded_payload_t::from_string (nlohmann::json (value).dump ());
          },
          [] (const encoded_payload_t &payload) {
              return nlohmann::json::parse (payload.to_string ())
                .get<spot_actor_disconnect_route_reply_t> ();
          });
    }
    if (!serializers.contains (std::type_index (typeid (actor_bound_session_route_request_t)))) {
        serializers.add<actor_bound_session_route_request_t> (
          [] (const actor_bound_session_route_request_t &value) {
              return encoded_payload_t::from_string (nlohmann::json (value).dump ());
          },
          [] (const encoded_payload_t &payload) {
              return nlohmann::json::parse (payload.to_string ())
                .get<actor_bound_session_route_request_t> ();
          });
    }
    if (!serializers.contains (
          std::type_index (typeid (actor_bound_session_bind_route_request_t)))) {
        serializers.add<actor_bound_session_bind_route_request_t> (
          [] (const actor_bound_session_bind_route_request_t &value) {
              return encoded_payload_t::from_string (nlohmann::json (value).dump ());
          },
          [] (const encoded_payload_t &payload) {
              return nlohmann::json::parse (payload.to_string ())
                .get<actor_bound_session_bind_route_request_t> ();
          });
    }
    if (!serializers.contains (std::type_index (typeid (actor_bound_session_route_reply_t)))) {
        serializers.add<actor_bound_session_route_reply_t> (
          [] (const actor_bound_session_route_reply_t &value) {
              return encoded_payload_t::from_string (nlohmann::json (value).dump ());
          },
          [] (const encoded_payload_t &payload) {
              return nlohmann::json::parse (payload.to_string ())
                .get<actor_bound_session_route_reply_t> ();
          });
    }
}

} // namespace zlink::framework::detail
