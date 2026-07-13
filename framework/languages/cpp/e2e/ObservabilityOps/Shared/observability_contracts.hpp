/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace zlink::framework::e2e::observability_ops
{

inline constexpr const char *spot_mesh = "obs.play";
inline constexpr const char *spot_route_channel = "obs.play.route";
inline constexpr const char *room_spot = "obs.room";
inline constexpr const char *spot_rid_metadata = "obs-spot-rid";

struct obs_action_req_t
{
    static constexpr const char *packet_name = "ObsActionReq";
    std::string spot_rid;
    std::string marker;
    int value = 0;
};

inline void to_json (nlohmann::json &json, const obs_action_req_t &value)
{
    json = {{"spotRid", value.spot_rid}, {"marker", value.marker}, {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, obs_action_req_t &value)
{
    json.at ("spotRid").get_to (value.spot_rid);
    json.at ("marker").get_to (value.marker);
    json.at ("value").get_to (value.value);
}

struct obs_action_res_t
{
    static constexpr const char *packet_name = "ObsActionRes";
    std::string spot_rid;
    std::string marker;
    int value = 0;
};

inline void to_json (nlohmann::json &json, const obs_action_res_t &value)
{
    json = {{"spotRid", value.spot_rid}, {"marker", value.marker}, {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, obs_action_res_t &value)
{
    json.at ("spotRid").get_to (value.spot_rid);
    json.at ("marker").get_to (value.marker);
    json.at ("value").get_to (value.value);
}

/* OBS-A2: intentionally has no registered handler on the room spot. */
struct obs_unknown_req_t
{
    static constexpr const char *packet_name = "ObsUnknownReq";
    std::string marker;
};

inline void to_json (nlohmann::json &json, const obs_unknown_req_t &value)
{
    json = {{"marker", value.marker}};
}

inline void from_json (const nlohmann::json &json, obs_unknown_req_t &value)
{
    json.at ("marker").get_to (value.marker);
}

struct create_room_req_t
{
    std::string spot_rid;
};

inline void to_json (nlohmann::json &json, const create_room_req_t &value)
{
    json = {{"spotRid", value.spot_rid}};
}

inline void from_json (const nlohmann::json &json, create_room_req_t &value)
{
    json.at ("spotRid").get_to (value.spot_rid);
}

struct create_room_res_t
{
    std::string spot_rid;
    std::string state;
};

inline void to_json (nlohmann::json &json, const create_room_res_t &value)
{
    json = {{"spotRid", value.spot_rid}, {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, create_room_res_t &value)
{
    json.at ("spotRid").get_to (value.spot_rid);
    json.at ("state").get_to (value.state);
}

struct drain_req_t
{
    int deadline_ms = 30000;
};

inline void to_json (nlohmann::json &json, const drain_req_t &value)
{
    json = {{"deadlineMs", value.deadline_ms}};
}

inline void from_json (const nlohmann::json &json, drain_req_t &value)
{
    if (json.contains ("deadlineMs")) {
        json.at ("deadlineMs").get_to (value.deadline_ms);
    }
}

} // namespace zlink::framework::e2e::observability_ops
