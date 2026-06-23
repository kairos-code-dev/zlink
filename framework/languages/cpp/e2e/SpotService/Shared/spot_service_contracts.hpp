/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace zlink::framework::e2e::spot_service
{

inline constexpr const char *route_channel = "spot.service.play";
inline constexpr const char *api_channel = "spot.service.api";
inline constexpr const char *spot_mesh = "spot.service.mesh";
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
    json = nlohmann::json{{"spot_rid", value.spot_rid},
                          {"owner_node_rid", value.owner_node_rid},
                          {"actor_id", value.actor_id},
                          {"display_name", value.display_name},
                          {"level", value.level},
                          {"tags", value.tags}};
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
