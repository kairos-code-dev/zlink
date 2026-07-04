/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace zlink::e2e::to_actor_messaging
{

struct actor_notify_t
{
    static constexpr const char *packet_name = "ActorNotify";
    std::string scenario;
    std::string actor_id;
    std::string value;
};

struct actor_ask_t
{
    static constexpr const char *packet_name = "ActorAsk";
    std::string scenario;
    std::string actor_id;
    std::string value;
};

struct actor_reply_t
{
    static constexpr const char *packet_name = "ActorReply";
    std::string scenario;
    std::string actor_id;
    std::string value;
};

struct caller_request_t
{
    static constexpr const char *packet_name = "CallerRequest";
    std::string scenario;
    std::string actor_id;
    std::string value;
};

struct caller_reply_t
{
    static constexpr const char *packet_name = "CallerReply";
    std::string scenario;
    std::string actor_id;
    std::string value;
};

inline void to_json (nlohmann::json &json, const actor_notify_t &value)
{
    json = {{"scenario", value.scenario}, {"actorId", value.actor_id}, {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, actor_notify_t &value)
{
    value.scenario = json.value ("scenario", "");
    value.actor_id = json.contains ("actorId") ? json.value ("actorId", "")
                                                : json.value ("actor_id", "");
    value.value = json.value ("value", "");
}

inline void to_json (nlohmann::json &json, const actor_ask_t &value)
{
    json = {{"scenario", value.scenario}, {"actorId", value.actor_id}, {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, actor_ask_t &value)
{
    value.scenario = json.value ("scenario", "");
    value.actor_id = json.contains ("actorId") ? json.value ("actorId", "")
                                                : json.value ("actor_id", "");
    value.value = json.value ("value", "");
}

inline void to_json (nlohmann::json &json, const actor_reply_t &value)
{
    json = {{"scenario", value.scenario}, {"actorId", value.actor_id}, {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, actor_reply_t &value)
{
    value.scenario = json.value ("scenario", "");
    value.actor_id = json.contains ("actorId") ? json.value ("actorId", "")
                                                : json.value ("actor_id", "");
    value.value = json.value ("value", "");
}

inline void to_json (nlohmann::json &json, const caller_request_t &value)
{
    json = {{"scenario", value.scenario}, {"actorId", value.actor_id}, {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, caller_request_t &value)
{
    value.scenario = json.value ("scenario", "");
    value.actor_id = json.contains ("actorId") ? json.value ("actorId", "")
                                                : json.value ("actor_id", "");
    value.value = json.value ("value", "");
}

inline void to_json (nlohmann::json &json, const caller_reply_t &value)
{
    json = {{"scenario", value.scenario}, {"actorId", value.actor_id}, {"value", value.value}};
}

inline void from_json (const nlohmann::json &json, caller_reply_t &value)
{
    value.scenario = json.value ("scenario", "");
    value.actor_id = json.contains ("actorId") ? json.value ("actorId", "")
                                                : json.value ("actor_id", "");
    value.value = json.value ("value", "");
}

} // namespace zlink::e2e::to_actor_messaging
