/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace zlink::framework::e2e::spot_service
{

inline constexpr const char *route_channel = "spot.service.play";
inline constexpr const char *api_channel = "spot.service.api";
inline constexpr const char *spot_mesh = "spot.service.mesh";
inline constexpr const char *publisher_channel = "spot.service.publisher";
inline constexpr const char *handler_group = "spot-service";
inline constexpr const char *actor_type = "scenario-player";
inline constexpr const char *user_spot = "user";
inline constexpr const char *alternate_spot = "alternate-user";
inline constexpr const char *mesh_topic = "spot-service-topic";

struct actor_ref_dto_t
{
    std::string node_rid;
    std::string actor_type;
    std::string actor_id;
    std::uint64_t generation = 0;
};

struct ensure_actor_req_t
{
    std::string actor_id;
    std::string display_name;
};

struct ensure_actor_res_t
{
    actor_ref_dto_t actor;
};

struct join_req_t
{
    std::string key;
    std::string actor_id;
    std::string display_name;
    int level = 0;
    std::vector<std::string> tags;
};

struct join_res_t
{
    std::string spot_rid;
    std::string owner_node_rid;
    std::string actor_id;
    std::string display_name;
    int level = 0;
    std::vector<std::string> tags;
};

struct state_req_t
{
    std::string op;
    int amount = 0;
};

struct state_res_t
{
    std::string spot_rid;
    std::string owner_node_rid;
    int value = 0;
    int sequence = 0;
};

struct leave_req_t
{
    std::string reason;
};

struct leave_res_t
{
    bool left = false;
    std::string actor_id;
};

struct destroy_actor_req_t
{
    std::string reason;
};

struct destroy_actor_res_t
{
    bool destroyed = false;
    std::string actor_id;
};

struct disconnect_req_t
{
    std::string reason;
};

struct disconnect_res_t
{
    bool disconnected = false;
    std::string actor_id;
};

struct channel_echo_req_t
{
    std::string value;
};

struct channel_echo_res_t
{
    std::string value;
    std::string handled_by;
};

struct channel_command_t
{
    std::string command_id;
};

struct mesh_event_t
{
    std::string event_id;
    std::string value;
};

struct outbound_req_t
{
    std::string value;
};

struct outbound_res_t
{
    std::string channel_reply;
    bool command_sent = false;
    bool published = false;
};

struct worker_req_t
{
    int delta = 0;
    int delay_ms = 0;
};

struct worker_res_t
{
    int snapshot = 0;
    int worker_result = 0;
    int final_value = 0;
    int sequence = 0;
};

struct direct_spot_req_t
{
    std::string source_actor_id;
    std::string value;
};

struct direct_spot_res_t
{
    std::string spot_rid;
    std::string owner_node_rid;
    std::string value;
};

struct direct_spot_command_t
{
    std::string source_actor_id;
    std::string value;
};

struct slow_spot_req_t
{
    std::string value;
};

struct unhandled_spot_req_t
{
    std::string value;
};

struct spot_to_spot_req_t
{
    std::string target_key;
    std::string value;
};

struct spot_to_spot_res_t
{
    std::string request_reply;
    bool command_sent = false;
    bool published = false;
    bool missing_rejected = false;
    bool timeout_rejected = false;
};

struct type_mismatch_req_t
{
    std::string probe;
};

struct type_mismatch_res_t
{
    bool rejected = false;
    std::string error_kind;
    std::string spot_name;
    int value = 0;
};

struct lifecycle_req_t
{
    std::string key;
};

struct lifecycle_res_t
{
    std::string spot_rid;
    bool created = false;
    bool closed = false;
};

struct stream_auth_req_t
{
    std::string target_node_rid;
    std::string actor_id;
    std::string display_name;
    actor_ref_dto_t actor;
};

struct stream_auth_res_t
{
    actor_ref_dto_t actor;
    std::string session_node_rid;
};

struct actor_push_req_t
{
    std::string value;
};

struct actor_push_res_t
{
    bool pushed = false;
    std::string actor_id;
};

struct actor_push_notify_t
{
    std::string actor_id;
    std::string value;
};

struct evidence_entry_t
{
    std::string marker;
    std::string node_rid;
    std::string actor_id;
    std::string spot_rid;
    std::string value;
};

struct evidence_snapshot_t
{
    std::string node_rid;
    std::vector<evidence_entry_t> entries;
};

inline void to_json (nlohmann::json &json, const actor_ref_dto_t &value)
{
    json = nlohmann::json{{"node_rid", value.node_rid},
                          {"actor_type", value.actor_type},
                          {"actor_id", value.actor_id},
                          {"generation", value.generation}};
}

inline void from_json (const nlohmann::json &json, actor_ref_dto_t &value)
{
    json.at ("node_rid").get_to (value.node_rid);
    json.at ("actor_type").get_to (value.actor_type);
    json.at ("actor_id").get_to (value.actor_id);
    json.at ("generation").get_to (value.generation);
}

inline void to_json (nlohmann::json &json, const ensure_actor_req_t &value)
{
    json = nlohmann::json{{"actor_id", value.actor_id}, {"display_name", value.display_name}};
}

inline void from_json (const nlohmann::json &json, ensure_actor_req_t &value)
{
    json.at ("actor_id").get_to (value.actor_id);
    json.at ("display_name").get_to (value.display_name);
}

inline void to_json (nlohmann::json &json, const ensure_actor_res_t &value)
{
    json = nlohmann::json{{"actor", value.actor}};
}

inline void from_json (const nlohmann::json &json, ensure_actor_res_t &value)
{
    json.at ("actor").get_to (value.actor);
}

inline void to_json (nlohmann::json &json, const join_req_t &value)
{
    json = nlohmann::json{{"key", value.key},
                          {"actor_id", value.actor_id},
                          {"display_name", value.display_name},
                          {"level", value.level},
                          {"tags", value.tags}};
}

inline void from_json (const nlohmann::json &json, join_req_t &value)
{
    json.at ("key").get_to (value.key);
    json.at ("actor_id").get_to (value.actor_id);
    json.at ("display_name").get_to (value.display_name);
    json.at ("level").get_to (value.level);
    json.at ("tags").get_to (value.tags);
}

inline void to_json (nlohmann::json &json, const join_res_t &value)
{
    json = nlohmann::json{{"spot_rid", value.spot_rid}, {"owner_node_rid", value.owner_node_rid},
                          {"actor_id", value.actor_id}, {"display_name", value.display_name},
                          {"level", value.level},       {"tags", value.tags}};
}

inline void from_json (const nlohmann::json &json, join_res_t &value)
{
    json.at ("spot_rid").get_to (value.spot_rid);
    json.at ("owner_node_rid").get_to (value.owner_node_rid);
    json.at ("actor_id").get_to (value.actor_id);
    json.at ("display_name").get_to (value.display_name);
    json.at ("level").get_to (value.level);
    json.at ("tags").get_to (value.tags);
}

inline void to_json (nlohmann::json &json, const state_req_t &value)
{
    json = nlohmann::json{{"op", value.op}, {"amount", value.amount}};
}

inline void from_json (const nlohmann::json &json, state_req_t &value)
{
    json.at ("op").get_to (value.op);
    json.at ("amount").get_to (value.amount);
}

inline void to_json (nlohmann::json &json, const state_res_t &value)
{
    json = nlohmann::json{{"spot_rid", value.spot_rid},
                          {"owner_node_rid", value.owner_node_rid},
                          {"value", value.value},
                          {"sequence", value.sequence}};
}

inline void from_json (const nlohmann::json &json, state_res_t &value)
{
    json.at ("spot_rid").get_to (value.spot_rid);
    json.at ("owner_node_rid").get_to (value.owner_node_rid);
    json.at ("value").get_to (value.value);
    json.at ("sequence").get_to (value.sequence);
}

inline void to_json (nlohmann::json &json, const leave_req_t &value)
{
    json = nlohmann::json{{"reason", value.reason}};
}

inline void from_json (const nlohmann::json &json, leave_req_t &value)
{
    json.at ("reason").get_to (value.reason);
}

inline void to_json (nlohmann::json &json, const leave_res_t &value)
{
    json = nlohmann::json{{"left", value.left}, {"actor_id", value.actor_id}};
}

inline void from_json (const nlohmann::json &json, leave_res_t &value)
{
    json.at ("left").get_to (value.left);
    json.at ("actor_id").get_to (value.actor_id);
}

inline void to_json (nlohmann::json &json, const destroy_actor_req_t &value)
{
    json = nlohmann::json{{"reason", value.reason}};
}

inline void from_json (const nlohmann::json &json, destroy_actor_req_t &value)
{
    json.at ("reason").get_to (value.reason);
}

inline void to_json (nlohmann::json &json, const destroy_actor_res_t &value)
{
    json = nlohmann::json{{"destroyed", value.destroyed}, {"actor_id", value.actor_id}};
}

inline void from_json (const nlohmann::json &json, destroy_actor_res_t &value)
{
    json.at ("destroyed").get_to (value.destroyed);
    json.at ("actor_id").get_to (value.actor_id);
}

inline void to_json (nlohmann::json &json, const disconnect_req_t &value)
{
    json = nlohmann::json{{"reason", value.reason}};
}

inline void from_json (const nlohmann::json &json, disconnect_req_t &value)
{
    json.at ("reason").get_to (value.reason);
}

inline void to_json (nlohmann::json &json, const disconnect_res_t &value)
{
    json = nlohmann::json{{"disconnected", value.disconnected}, {"actor_id", value.actor_id}};
}

inline void from_json (const nlohmann::json &json, disconnect_res_t &value)
{
    json.at ("disconnected").get_to (value.disconnected);
    json.at ("actor_id").get_to (value.actor_id);
}

inline void to_json (nlohmann::json &json, const channel_echo_req_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, channel_echo_req_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const channel_echo_res_t &value)
{
    json = nlohmann::json{{"value", value.value}, {"handled_by", value.handled_by}};
}

inline void from_json (const nlohmann::json &json, channel_echo_res_t &value)
{
    json.at ("value").get_to (value.value);
    json.at ("handled_by").get_to (value.handled_by);
}

inline void to_json (nlohmann::json &json, const channel_command_t &value)
{
    json = nlohmann::json{{"command_id", value.command_id}};
}

inline void from_json (const nlohmann::json &json, channel_command_t &value)
{
    json.at ("command_id").get_to (value.command_id);
}

inline void to_json (nlohmann::json &json, const mesh_event_t &value)
{
    json = nlohmann::json{{"event_id", value.event_id}, {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, mesh_event_t &value)
{
    json.at ("event_id").get_to (value.event_id);
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const outbound_req_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, outbound_req_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const outbound_res_t &value)
{
    json = nlohmann::json{{"channel_reply", value.channel_reply},
                          {"command_sent", value.command_sent},
                          {"published", value.published}};
}

inline void from_json (const nlohmann::json &json, outbound_res_t &value)
{
    json.at ("channel_reply").get_to (value.channel_reply);
    json.at ("command_sent").get_to (value.command_sent);
    json.at ("published").get_to (value.published);
}

inline void to_json (nlohmann::json &json, const worker_req_t &value)
{
    json = nlohmann::json{{"delta", value.delta}, {"delay_ms", value.delay_ms}};
}

inline void from_json (const nlohmann::json &json, worker_req_t &value)
{
    json.at ("delta").get_to (value.delta);
    json.at ("delay_ms").get_to (value.delay_ms);
}

inline void to_json (nlohmann::json &json, const worker_res_t &value)
{
    json = nlohmann::json{{"snapshot", value.snapshot},
                          {"worker_result", value.worker_result},
                          {"final_value", value.final_value},
                          {"sequence", value.sequence}};
}

inline void from_json (const nlohmann::json &json, worker_res_t &value)
{
    json.at ("snapshot").get_to (value.snapshot);
    json.at ("worker_result").get_to (value.worker_result);
    json.at ("final_value").get_to (value.final_value);
    json.at ("sequence").get_to (value.sequence);
}

inline void to_json (nlohmann::json &json, const direct_spot_req_t &value)
{
    json = nlohmann::json{{"source_actor_id", value.source_actor_id}, {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, direct_spot_req_t &value)
{
    json.at ("source_actor_id").get_to (value.source_actor_id);
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const direct_spot_res_t &value)
{
    json = nlohmann::json{{"spot_rid", value.spot_rid},
                          {"owner_node_rid", value.owner_node_rid},
                          {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, direct_spot_res_t &value)
{
    json.at ("spot_rid").get_to (value.spot_rid);
    json.at ("owner_node_rid").get_to (value.owner_node_rid);
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const direct_spot_command_t &value)
{
    json = nlohmann::json{{"source_actor_id", value.source_actor_id}, {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, direct_spot_command_t &value)
{
    json.at ("source_actor_id").get_to (value.source_actor_id);
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const slow_spot_req_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, slow_spot_req_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const unhandled_spot_req_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, unhandled_spot_req_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const spot_to_spot_req_t &value)
{
    json = nlohmann::json{{"target_key", value.target_key}, {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, spot_to_spot_req_t &value)
{
    json.at ("target_key").get_to (value.target_key);
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const spot_to_spot_res_t &value)
{
    json = nlohmann::json{{"request_reply", value.request_reply},
                          {"command_sent", value.command_sent},
                          {"published", value.published},
                          {"missing_rejected", value.missing_rejected},
                          {"timeout_rejected", value.timeout_rejected}};
}

inline void from_json (const nlohmann::json &json, spot_to_spot_res_t &value)
{
    json.at ("request_reply").get_to (value.request_reply);
    json.at ("command_sent").get_to (value.command_sent);
    json.at ("published").get_to (value.published);
    json.at ("missing_rejected").get_to (value.missing_rejected);
    json.at ("timeout_rejected").get_to (value.timeout_rejected);
}

inline void to_json (nlohmann::json &json, const type_mismatch_req_t &value)
{
    json = nlohmann::json{{"probe", value.probe}};
}

inline void from_json (const nlohmann::json &json, type_mismatch_req_t &value)
{
    json.at ("probe").get_to (value.probe);
}

inline void to_json (nlohmann::json &json, const type_mismatch_res_t &value)
{
    json = nlohmann::json{{"rejected", value.rejected},
                          {"error_kind", value.error_kind},
                          {"spot_name", value.spot_name},
                          {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, type_mismatch_res_t &value)
{
    json.at ("rejected").get_to (value.rejected);
    json.at ("error_kind").get_to (value.error_kind);
    json.at ("spot_name").get_to (value.spot_name);
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const lifecycle_req_t &value)
{
    json = nlohmann::json{{"key", value.key}};
}

inline void from_json (const nlohmann::json &json, lifecycle_req_t &value)
{
    json.at ("key").get_to (value.key);
}

inline void to_json (nlohmann::json &json, const lifecycle_res_t &value)
{
    json = nlohmann::json{
      {"spot_rid", value.spot_rid}, {"created", value.created}, {"closed", value.closed}};
}

inline void from_json (const nlohmann::json &json, lifecycle_res_t &value)
{
    json.at ("spot_rid").get_to (value.spot_rid);
    json.at ("created").get_to (value.created);
    json.at ("closed").get_to (value.closed);
}

inline void to_json (nlohmann::json &json, const stream_auth_req_t &value)
{
    json = nlohmann::json{{"target_node_rid", value.target_node_rid},
                          {"actor_id", value.actor_id},
                          {"display_name", value.display_name},
                          {"actor", value.actor}};
}

inline void from_json (const nlohmann::json &json, stream_auth_req_t &value)
{
    json.at ("target_node_rid").get_to (value.target_node_rid);
    json.at ("actor_id").get_to (value.actor_id);
    json.at ("display_name").get_to (value.display_name);
    json.at ("actor").get_to (value.actor);
}

inline void to_json (nlohmann::json &json, const stream_auth_res_t &value)
{
    json = nlohmann::json{{"actor", value.actor}, {"session_node_rid", value.session_node_rid}};
}

inline void from_json (const nlohmann::json &json, stream_auth_res_t &value)
{
    json.at ("actor").get_to (value.actor);
    json.at ("session_node_rid").get_to (value.session_node_rid);
}

inline void to_json (nlohmann::json &json, const actor_push_req_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

inline void from_json (const nlohmann::json &json, actor_push_req_t &value)
{
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const actor_push_res_t &value)
{
    json = nlohmann::json{{"pushed", value.pushed}, {"actor_id", value.actor_id}};
}

inline void from_json (const nlohmann::json &json, actor_push_res_t &value)
{
    json.at ("pushed").get_to (value.pushed);
    json.at ("actor_id").get_to (value.actor_id);
}

inline void to_json (nlohmann::json &json, const actor_push_notify_t &value)
{
    json = nlohmann::json{{"actor_id", value.actor_id}, {"value", value.value}};
}

inline std::string to_stream_payload (const actor_push_notify_t &value)
{
    return nlohmann::json (value).dump ();
}

inline void from_stream_payload (const zlink::message_t &payload, actor_push_notify_t &value)
{
    const auto json = nlohmann::json::parse (payload.to_string ());
    json.at ("actor_id").get_to (value.actor_id);
    json.at ("value").get_to (value.value);
}

inline void from_json (const nlohmann::json &json, actor_push_notify_t &value)
{
    json.at ("actor_id").get_to (value.actor_id);
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const evidence_entry_t &value)
{
    json = nlohmann::json{{"marker", value.marker},
                          {"node_rid", value.node_rid},
                          {"actor_id", value.actor_id},
                          {"spot_rid", value.spot_rid},
                          {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, evidence_entry_t &value)
{
    json.at ("marker").get_to (value.marker);
    json.at ("node_rid").get_to (value.node_rid);
    json.at ("actor_id").get_to (value.actor_id);
    json.at ("spot_rid").get_to (value.spot_rid);
    json.at ("value").get_to (value.value);
}

inline void to_json (nlohmann::json &json, const evidence_snapshot_t &value)
{
    json = nlohmann::json{{"node_rid", value.node_rid}, {"entries", value.entries}};
}

inline void from_json (const nlohmann::json &json, evidence_snapshot_t &value)
{
    json.at ("node_rid").get_to (value.node_rid);
    json.at ("entries").get_to (value.entries);
}

} // namespace zlink::framework::e2e::spot_service
