/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace zlink::samples::gamequest
{

struct quest_status_t
{
    static constexpr const char *in_progress = "InProgress";
    static constexpr const char *reward_granted = "RewardGranted";
};

struct quest_progress_t
{
    static constexpr const char *packet_name = "QuestProgress";
    std::string player_id;
    std::string quest_id;
    std::string status;
    int current_count{0};
    int required_count{0};
    std::string last_event_id;
    long long updated_at_unix_ms{0};
};

struct event_res_t
{
    std::string event_id;
};

struct enter_area_req_t
{
    static constexpr const char *packet_name = "EnterAreaReq";
    std::string player_id;
    std::string area_id;
    std::string idempotency_key;
};

struct kill_monster_req_t
{
    static constexpr const char *packet_name = "KillMonsterReq";
    std::string player_id;
    std::string monster_id;
    std::string area_id;
    std::string idempotency_key;
};

struct collect_item_req_t
{
    static constexpr const char *packet_name = "CollectItemReq";
    std::string player_id;
    std::string item_id;
    int count{0};
    std::string idempotency_key;
};

struct complete_mission_req_t
{
    static constexpr const char *packet_name = "CompleteMissionReq";
    std::string player_id;
    std::string mission_id;
    std::string idempotency_key;
};

struct unlock_feature_req_t
{
    static constexpr const char *packet_name = "UnlockFeatureReq";
    std::string player_id;
    std::string feature_id;
    std::string idempotency_key;
};

struct subscribe_quest_req_t
{
    static constexpr const char *packet_name = "SubscribeQuestReq";
    std::string player_id;
};

struct subscribe_quest_res_t
{
    std::vector<quest_progress_t> active_quests;
};

struct get_quest_progress_req_t
{
    static constexpr const char *packet_name = "GetQuestProgressReq";
    std::string player_id;
};

struct get_quest_progress_res_t
{
    std::vector<quest_progress_t> active_quests;
};

struct sync_quest_progress_req_t
{
    static constexpr const char *packet_name = "SyncQuestProgressReq";
    std::string player_id;
};

struct sync_quest_progress_res_t
{
    std::vector<quest_progress_t> updated_quests;
};

struct get_gameplay_snapshot_req_t
{
    static constexpr const char *packet_name = "GetGameplaySnapshotReq";
    std::string player_id;
};

struct get_gameplay_snapshot_res_t
{
    std::string player_id;
    std::vector<std::string> completed_mission_ids;
    std::vector<std::string> unlocked_feature_ids;
    std::vector<std::string> entered_area_ids;
    long long snapshot_version{0};
};

struct delete_quest_projection_req_t
{
    static constexpr const char *packet_name = "DeleteQuestProjectionReq";
    std::string player_id;
    std::string quest_id;
};

struct rebuild_quest_projection_req_t
{
    static constexpr const char *packet_name = "RebuildQuestProjectionReq";
    std::string player_id;
    std::string quest_id;
};

struct game_quest_server_assert_req_t
{
    static constexpr const char *packet_name = "GameQuestServerAssertReq";
};

struct game_quest_server_assert_res_t
{
    bool passed{false};
    std::vector<std::string> evidence;
};

inline void to_json (nlohmann::json &json, const quest_progress_t &value)
{
    json = nlohmann::json{{"playerId", value.player_id},
                          {"questId", value.quest_id},
                          {"status", value.status},
                          {"currentCount", value.current_count},
                          {"requiredCount", value.required_count},
                          {"lastEventId", value.last_event_id},
                          {"updatedAtUnixMs", value.updated_at_unix_ms}};
}

inline void from_json (const nlohmann::json &json, quest_progress_t &value)
{
    value.player_id = json.value ("playerId", "");
    value.quest_id = json.value ("questId", "");
    value.status = json.value ("status", "");
    value.current_count = json.value ("currentCount", 0);
    value.required_count = json.value ("requiredCount", 0);
    value.last_event_id = json.value ("lastEventId", "");
    value.updated_at_unix_ms = json.value ("updatedAtUnixMs", 0LL);
}

inline void to_json (nlohmann::json &json, const event_res_t &value)
{
    json = nlohmann::json{{"eventId", value.event_id}};
}

inline void from_json (const nlohmann::json &json, event_res_t &value)
{
    value.event_id = json.value ("eventId", "");
}

#define ZLINK_GAMEQUEST_JSON3(type_name, field1, json1, field2, json2, field3, json3) \
    inline void to_json (nlohmann::json &json, const type_name &value)               \
    {                                                                                \
        json = nlohmann::json{{json1, value.field1}, {json2, value.field2},          \
                              {json3, value.field3}};                                \
    }                                                                                \
    inline void from_json (const nlohmann::json &json, type_name &value)             \
    {                                                                                \
        value.field1 = json.value (json1, "");                                       \
        value.field2 = json.value (json2, "");                                       \
        value.field3 = json.value (json3, "");                                       \
    }

ZLINK_GAMEQUEST_JSON3 (enter_area_req_t, player_id, "playerId", area_id, "areaId",
                       idempotency_key, "idempotencyKey")
ZLINK_GAMEQUEST_JSON3 (complete_mission_req_t, player_id, "playerId", mission_id,
                       "missionId", idempotency_key, "idempotencyKey")
ZLINK_GAMEQUEST_JSON3 (unlock_feature_req_t, player_id, "playerId", feature_id,
                       "featureId", idempotency_key, "idempotencyKey")

#undef ZLINK_GAMEQUEST_JSON3

inline void to_json (nlohmann::json &json, const kill_monster_req_t &value)
{
    json = nlohmann::json{{"playerId", value.player_id},
                          {"monsterId", value.monster_id},
                          {"areaId", value.area_id},
                          {"idempotencyKey", value.idempotency_key}};
}

inline void from_json (const nlohmann::json &json, kill_monster_req_t &value)
{
    value.player_id = json.value ("playerId", "");
    value.monster_id = json.value ("monsterId", "");
    value.area_id = json.value ("areaId", "");
    value.idempotency_key = json.value ("idempotencyKey", "");
}

inline void to_json (nlohmann::json &json, const collect_item_req_t &value)
{
    json = nlohmann::json{{"playerId", value.player_id},
                          {"itemId", value.item_id},
                          {"count", value.count},
                          {"idempotencyKey", value.idempotency_key}};
}

inline void from_json (const nlohmann::json &json, collect_item_req_t &value)
{
    value.player_id = json.value ("playerId", "");
    value.item_id = json.value ("itemId", "");
    value.count = json.value ("count", 0);
    value.idempotency_key = json.value ("idempotencyKey", "");
}

#define ZLINK_GAMEQUEST_PLAYER_REQ(type_name)                                      \
    inline void to_json (nlohmann::json &json, const type_name &value)             \
    {                                                                              \
        json = nlohmann::json{{"playerId", value.player_id}};                     \
    }                                                                              \
    inline void from_json (const nlohmann::json &json, type_name &value)           \
    {                                                                              \
        value.player_id = json.value ("playerId", "");                           \
    }

ZLINK_GAMEQUEST_PLAYER_REQ (subscribe_quest_req_t)
ZLINK_GAMEQUEST_PLAYER_REQ (get_quest_progress_req_t)
ZLINK_GAMEQUEST_PLAYER_REQ (sync_quest_progress_req_t)
ZLINK_GAMEQUEST_PLAYER_REQ (get_gameplay_snapshot_req_t)

#undef ZLINK_GAMEQUEST_PLAYER_REQ

inline void to_json (nlohmann::json &json, const subscribe_quest_res_t &value)
{
    json = nlohmann::json{{"activeQuests", value.active_quests}};
}

inline void from_json (const nlohmann::json &json, subscribe_quest_res_t &value)
{
    value.active_quests =
      json.value ("activeQuests", std::vector<quest_progress_t>{});
}

inline void to_json (nlohmann::json &json, const get_quest_progress_res_t &value)
{
    json = nlohmann::json{{"activeQuests", value.active_quests}};
}

inline void from_json (const nlohmann::json &json, get_quest_progress_res_t &value)
{
    value.active_quests =
      json.value ("activeQuests", std::vector<quest_progress_t>{});
}

inline void to_json (nlohmann::json &json, const sync_quest_progress_res_t &value)
{
    json = nlohmann::json{{"updatedQuests", value.updated_quests}};
}

inline void from_json (const nlohmann::json &json, sync_quest_progress_res_t &value)
{
    value.updated_quests =
      json.value ("updatedQuests", std::vector<quest_progress_t>{});
}

inline void to_json (nlohmann::json &json, const get_gameplay_snapshot_res_t &value)
{
    json = nlohmann::json{{"playerId", value.player_id},
                          {"completedMissionIds", value.completed_mission_ids},
                          {"unlockedFeatureIds", value.unlocked_feature_ids},
                          {"enteredAreaIds", value.entered_area_ids},
                          {"snapshotVersion", value.snapshot_version}};
}

inline void from_json (const nlohmann::json &json, get_gameplay_snapshot_res_t &value)
{
    value.player_id = json.value ("playerId", "");
    value.completed_mission_ids =
      json.value ("completedMissionIds", std::vector<std::string>{});
    value.unlocked_feature_ids =
      json.value ("unlockedFeatureIds", std::vector<std::string>{});
    value.entered_area_ids = json.value ("enteredAreaIds", std::vector<std::string>{});
    value.snapshot_version = json.value ("snapshotVersion", 0LL);
}

inline void to_json (nlohmann::json &json, const delete_quest_projection_req_t &value)
{
    json = nlohmann::json{{"playerId", value.player_id}, {"questId", value.quest_id}};
}

inline void from_json (const nlohmann::json &json, delete_quest_projection_req_t &value)
{
    value.player_id = json.value ("playerId", "");
    value.quest_id = json.value ("questId", "");
}

inline void to_json (nlohmann::json &json, const rebuild_quest_projection_req_t &value)
{
    json = nlohmann::json{{"playerId", value.player_id}, {"questId", value.quest_id}};
}

inline void from_json (const nlohmann::json &json, rebuild_quest_projection_req_t &value)
{
    value.player_id = json.value ("playerId", "");
    value.quest_id = json.value ("questId", "");
}

inline void to_json (nlohmann::json &json, const game_quest_server_assert_req_t &)
{
    json = nlohmann::json::object ();
}

inline void from_json (const nlohmann::json &, game_quest_server_assert_req_t &) {}

inline void to_json (nlohmann::json &json, const game_quest_server_assert_res_t &value)
{
    json = nlohmann::json{{"passed", value.passed}, {"evidence", value.evidence}};
}

inline void from_json (const nlohmann::json &json, game_quest_server_assert_res_t &value)
{
    value.passed = json.value ("passed", false);
    value.evidence = json.value ("evidence", std::vector<std::string>{});
}

} // namespace zlink::samples::gamequest
