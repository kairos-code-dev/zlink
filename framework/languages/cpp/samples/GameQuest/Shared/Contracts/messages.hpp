/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace zlink::samples::gamequest
{

struct quest_ids_t
{
    static constexpr const char *first_hunt = "first-hunt";
    static constexpr const char *open_auction = "open-auction";
    static constexpr const char *herb_gathering = "herb-gathering";
    static constexpr const char *clear_tutorial = "clear-tutorial";
    static constexpr const char *visit_ruins = "visit-ruins";
};

/* 공통 sample spec §11.4: 진행 중(Active) → 조건 충족(Completed) → 보상 지급(RewardGranted). */
struct quest_status_t
{
    static constexpr const char *active = "Active";
    static constexpr const char *completed = "Completed";
    static constexpr const char *reward_granted = "RewardGranted";
};

struct kill_monster_req_t
{
    static constexpr const char *packet_name = "KillMonsterReq";
    std::string player_id;
    std::string monster_id;
    std::string area_id;
    std::string idempotency_key;
};

struct kill_monster_res_t
{
    static constexpr const char *packet_name = "KillMonsterRes";
    std::string event_id;
};

struct collect_item_req_t
{
    static constexpr const char *packet_name = "CollectItemReq";
    std::string player_id;
    std::string item_id;
    int count = 0;
    std::string idempotency_key;
};

struct collect_item_res_t
{
    static constexpr const char *packet_name = "CollectItemRes";
    std::string event_id;
};

struct complete_mission_req_t
{
    static constexpr const char *packet_name = "CompleteMissionReq";
    std::string player_id;
    std::string mission_id;
    std::string idempotency_key;
};

struct complete_mission_res_t
{
    static constexpr const char *packet_name = "CompleteMissionRes";
    std::string event_id;
};

struct enter_area_req_t
{
    static constexpr const char *packet_name = "EnterAreaReq";
    std::string player_id;
    std::string area_id;
    std::string idempotency_key;
};

struct enter_area_res_t
{
    static constexpr const char *packet_name = "EnterAreaRes";
    std::string event_id;
};

struct unlock_feature_req_t
{
    static constexpr const char *packet_name = "UnlockFeatureReq";
    std::string player_id;
    std::string feature_id;
    std::string idempotency_key;
};

struct unlock_feature_res_t
{
    static constexpr const char *packet_name = "UnlockFeatureRes";
    std::string event_id;
};

struct join_session_req_t
{
    static constexpr const char *packet_name = "JoinSessionReq";
    std::string player_id;
};

/* 공통 sample spec §11.3: quest domain event stream(append-only SoR). projection은 이 stream을
 * fold해 만든다. */
struct stored_quest_event_t
{
    static constexpr const char *progressed = "QuestProgressed";
    static constexpr const char *completed = "QuestCompleted";
    static constexpr const char *reward_granted = "QuestRewardGranted";
    static constexpr const char *reconciled = "QuestReconciled";

    std::string event_id;
    std::string player_id;
    std::string quest_id;
    std::string type;
    std::string source_event_id;
    int delta = 0;
    int current_count = 0;
    int required_count = 0;
    long long version = 0;
    long long created_at_unix_ms = 0;
};

struct quest_progress_t
{
    std::string player_id;
    std::string quest_id;
    std::string status;
    int current_count = 0;
    int required_count = 0;
    /* 이 진행을 만든 마지막 gameplay event(idempotency 판정용)와 event stream fold의 버전. */
    std::string last_source_event_id;
    long long version = 0;
    long long updated_at_unix_ms = 0;
};

struct join_session_res_t
{
    static constexpr const char *packet_name = "JoinSessionRes";
    std::vector<quest_progress_t> active_quests;
};

struct get_quest_progress_req_t
{
    static constexpr const char *packet_name = "GetQuestProgressReq";
    std::string player_id;
};

struct get_quest_progress_res_t
{
    static constexpr const char *packet_name = "GetQuestProgressRes";
    std::vector<quest_progress_t> active_quests;
};

struct sync_quest_progress_req_t
{
    static constexpr const char *packet_name = "SyncQuestProgressReq";
    std::string player_id;
};

struct sync_quest_progress_res_t
{
    static constexpr const char *packet_name = "SyncQuestProgressRes";
    std::vector<quest_progress_t> updated_quests;
};

struct ensure_player_quest_spot_req_t
{
    static constexpr const char *packet_name = "EnsurePlayerQuestSpotReq";
    std::string player_id;
};

struct ensure_player_quest_spot_res_t
{
    static constexpr const char *packet_name = "EnsurePlayerQuestSpotRes";
    bool ok = false;
};

struct player_quest_spot_create_req_t
{
    static constexpr const char *packet_name = "PlayerQuestSpotCreateReq";
    std::string player_id;
};

struct quest_progress_notify_t
{
    static constexpr const char *packet_name = "QuestProgressNotify";
    std::string player_id;
    std::string target_connection_id;
    quest_progress_t progress;
};

struct quest_completed_notify_t
{
    static constexpr const char *packet_name = "QuestCompletedNotify";
    std::string player_id;
    std::string target_connection_id;
    quest_progress_t progress;
    bool reward_granted = false;
};

struct gameplay_event_envelope_t
{
    std::string event_id;
    std::string player_id;
    std::string idempotency_key;
    std::string event_type;
    std::string value;
    int count = 0;
    std::string source_api;
    long long created_at_unix_ms = 0;
};

struct notify_quest_progress_req_t
{
    static constexpr const char *packet_name = "NotifyQuestProgressReq";
    std::string player_id;
    std::vector<quest_progress_t> projection;
    std::string completed_quest_id;
};

struct notify_quest_progress_res_t
{
    static constexpr const char *packet_name = "NotifyQuestProgressRes";
    bool delivered = false;
};

struct apply_gameplay_event_req_t
{
    static constexpr const char *packet_name = "ApplyGameplayEventReq";
    gameplay_event_envelope_t event;
};

struct apply_gameplay_event_res_t
{
    static constexpr const char *packet_name = "ApplyGameplayEventRes";
    bool applied = false;
    std::vector<quest_progress_t> projection;
    std::string completed_quest_id;
};

struct server_assertion_req_t
{
    static constexpr const char *packet_name = "GameQuestServerAssertReq";
};

struct server_assertion_res_t
{
    static constexpr const char *packet_name = "GameQuestServerAssertRes";
    bool passed = false;
    std::vector<std::string> evidence;
};

inline void to_json (nlohmann::json &json, const kill_monster_req_t &value)
{
    json = {{"playerId", value.player_id}, {"monsterId", value.monster_id},
            {"areaId", value.area_id}, {"idempotencyKey", value.idempotency_key}};
}
inline void from_json (const nlohmann::json &json, kill_monster_req_t &value)
{
    json.at ("playerId").get_to (value.player_id);
    json.at ("monsterId").get_to (value.monster_id);
    json.at ("areaId").get_to (value.area_id);
    json.at ("idempotencyKey").get_to (value.idempotency_key);
}
inline void to_json (nlohmann::json &json, const kill_monster_res_t &value)
{
    json = {{"eventId", value.event_id}};
}
inline void from_json (const nlohmann::json &json, kill_monster_res_t &value)
{
    json.at ("eventId").get_to (value.event_id);
}
inline void to_json (nlohmann::json &json, const collect_item_req_t &value)
{
    json = {{"playerId", value.player_id}, {"itemId", value.item_id},
            {"count", value.count}, {"idempotencyKey", value.idempotency_key}};
}
inline void from_json (const nlohmann::json &json, collect_item_req_t &value)
{
    json.at ("playerId").get_to (value.player_id);
    json.at ("itemId").get_to (value.item_id);
    json.at ("count").get_to (value.count);
    json.at ("idempotencyKey").get_to (value.idempotency_key);
}
inline void to_json (nlohmann::json &json, const collect_item_res_t &value)
{
    json = {{"eventId", value.event_id}};
}
inline void from_json (const nlohmann::json &json, collect_item_res_t &value)
{
    json.at ("eventId").get_to (value.event_id);
}
inline void to_json (nlohmann::json &json, const complete_mission_req_t &value)
{
    json = {{"playerId", value.player_id}, {"missionId", value.mission_id},
            {"idempotencyKey", value.idempotency_key}};
}
inline void from_json (const nlohmann::json &json, complete_mission_req_t &value)
{
    json.at ("playerId").get_to (value.player_id);
    json.at ("missionId").get_to (value.mission_id);
    json.at ("idempotencyKey").get_to (value.idempotency_key);
}
inline void to_json (nlohmann::json &json, const complete_mission_res_t &value)
{
    json = {{"eventId", value.event_id}};
}
inline void from_json (const nlohmann::json &json, complete_mission_res_t &value)
{
    json.at ("eventId").get_to (value.event_id);
}
inline void to_json (nlohmann::json &json, const enter_area_req_t &value)
{
    json = {{"playerId", value.player_id}, {"areaId", value.area_id},
            {"idempotencyKey", value.idempotency_key}};
}
inline void from_json (const nlohmann::json &json, enter_area_req_t &value)
{
    json.at ("playerId").get_to (value.player_id);
    json.at ("areaId").get_to (value.area_id);
    json.at ("idempotencyKey").get_to (value.idempotency_key);
}
inline void to_json (nlohmann::json &json, const enter_area_res_t &value)
{
    json = {{"eventId", value.event_id}};
}
inline void from_json (const nlohmann::json &json, enter_area_res_t &value)
{
    json.at ("eventId").get_to (value.event_id);
}
inline void to_json (nlohmann::json &json, const unlock_feature_req_t &value)
{
    json = {{"playerId", value.player_id}, {"featureId", value.feature_id},
            {"idempotencyKey", value.idempotency_key}};
}
inline void from_json (const nlohmann::json &json, unlock_feature_req_t &value)
{
    json.at ("playerId").get_to (value.player_id);
    json.at ("featureId").get_to (value.feature_id);
    json.at ("idempotencyKey").get_to (value.idempotency_key);
}
inline void to_json (nlohmann::json &json, const unlock_feature_res_t &value)
{
    json = {{"eventId", value.event_id}};
}
inline void from_json (const nlohmann::json &json, unlock_feature_res_t &value)
{
    json.at ("eventId").get_to (value.event_id);
}
inline void to_json (nlohmann::json &json, const join_session_req_t &value)
{
    json = {{"playerId", value.player_id}};
}
inline void from_json (const nlohmann::json &json, join_session_req_t &value)
{
    json.at ("playerId").get_to (value.player_id);
}
inline void to_json (nlohmann::json &json, const stored_quest_event_t &value)
{
    json = {{"eventId", value.event_id},
            {"playerId", value.player_id},
            {"questId", value.quest_id},
            {"type", value.type},
            {"sourceEventId", value.source_event_id},
            {"delta", value.delta},
            {"currentCount", value.current_count},
            {"requiredCount", value.required_count},
            {"version", value.version},
            {"createdAtUnixMs", value.created_at_unix_ms}};
}
inline void from_json (const nlohmann::json &json, stored_quest_event_t &value)
{
    json.at ("eventId").get_to (value.event_id);
    json.at ("playerId").get_to (value.player_id);
    json.at ("questId").get_to (value.quest_id);
    json.at ("type").get_to (value.type);
    value.source_event_id = json.value ("sourceEventId", std::string{});
    value.delta = json.value ("delta", 0);
    value.current_count = json.value ("currentCount", 0);
    value.required_count = json.value ("requiredCount", 0);
    value.version = json.value ("version", 0LL);
    value.created_at_unix_ms = json.value ("createdAtUnixMs", 0LL);
}
inline void to_json (nlohmann::json &json, const quest_progress_t &value)
{
    json = {{"playerId", value.player_id}, {"questId", value.quest_id},
            {"status", value.status}, {"currentCount", value.current_count},
            {"requiredCount", value.required_count},
            {"lastSourceEventId", value.last_source_event_id}, {"version", value.version},
            {"updatedAtUnixMs", value.updated_at_unix_ms}};
}
inline void from_json (const nlohmann::json &json, quest_progress_t &value)
{
    json.at ("playerId").get_to (value.player_id);
    json.at ("questId").get_to (value.quest_id);
    json.at ("status").get_to (value.status);
    json.at ("currentCount").get_to (value.current_count);
    json.at ("requiredCount").get_to (value.required_count);
    value.last_source_event_id = json.value ("lastSourceEventId", std::string{});
    value.version = json.value ("version", 0LL);
    json.at ("updatedAtUnixMs").get_to (value.updated_at_unix_ms);
}
inline void to_json (nlohmann::json &json, const join_session_res_t &value)
{
    json = {{"activeQuests", value.active_quests}};
}
inline void from_json (const nlohmann::json &json, join_session_res_t &value)
{
    json.at ("activeQuests").get_to (value.active_quests);
}
inline void to_json (nlohmann::json &json, const get_quest_progress_req_t &value)
{
    json = {{"playerId", value.player_id}};
}
inline void from_json (const nlohmann::json &json, get_quest_progress_req_t &value)
{
    json.at ("playerId").get_to (value.player_id);
}
inline void to_json (nlohmann::json &json, const get_quest_progress_res_t &value)
{
    json = {{"activeQuests", value.active_quests}};
}
inline void from_json (const nlohmann::json &json, get_quest_progress_res_t &value)
{
    json.at ("activeQuests").get_to (value.active_quests);
}
inline void to_json (nlohmann::json &json, const sync_quest_progress_req_t &value)
{
    json = {{"playerId", value.player_id}};
}
inline void from_json (const nlohmann::json &json, sync_quest_progress_req_t &value)
{
    json.at ("playerId").get_to (value.player_id);
}
inline void to_json (nlohmann::json &json, const sync_quest_progress_res_t &value)
{
    json = {{"updatedQuests", value.updated_quests}};
}
inline void from_json (const nlohmann::json &json, sync_quest_progress_res_t &value)
{
    json.at ("updatedQuests").get_to (value.updated_quests);
}
inline void to_json (nlohmann::json &json, const ensure_player_quest_spot_req_t &value)
{
    json = {{"playerId", value.player_id}};
}
inline void from_json (const nlohmann::json &json, ensure_player_quest_spot_req_t &value)
{
    json.at ("playerId").get_to (value.player_id);
}
inline void to_json (nlohmann::json &json, const ensure_player_quest_spot_res_t &value)
{
    json = {{"ok", value.ok}};
}
inline void from_json (const nlohmann::json &json, ensure_player_quest_spot_res_t &value)
{
    json.at ("ok").get_to (value.ok);
}
inline void to_json (nlohmann::json &json, const player_quest_spot_create_req_t &value)
{
    json = {{"playerId", value.player_id}};
}
inline void from_json (const nlohmann::json &json, player_quest_spot_create_req_t &value)
{
    json.at ("playerId").get_to (value.player_id);
}
inline void to_json (nlohmann::json &json, const quest_progress_notify_t &value)
{
    json = {{"playerId", value.player_id}, {"targetConnectionId", value.target_connection_id},
            {"progress", value.progress}};
}
inline void from_json (const nlohmann::json &json, quest_progress_notify_t &value)
{
    json.at ("playerId").get_to (value.player_id);
    value.target_connection_id = json.value ("targetConnectionId", "");
    json.at ("progress").get_to (value.progress);
}
inline void to_json (nlohmann::json &json, const quest_completed_notify_t &value)
{
    json = {{"playerId", value.player_id}, {"targetConnectionId", value.target_connection_id},
            {"progress", value.progress}, {"rewardGranted", value.reward_granted}};
}
inline void from_json (const nlohmann::json &json, quest_completed_notify_t &value)
{
    json.at ("playerId").get_to (value.player_id);
    value.target_connection_id = json.value ("targetConnectionId", "");
    json.at ("progress").get_to (value.progress);
    json.at ("rewardGranted").get_to (value.reward_granted);
}
inline void to_json (nlohmann::json &json, const gameplay_event_envelope_t &value)
{
    json = {{"eventId", value.event_id}, {"playerId", value.player_id},
            {"idempotencyKey", value.idempotency_key}, {"eventType", value.event_type},
            {"value", value.value}, {"count", value.count}, {"sourceApi", value.source_api},
            {"createdAtUnixMs", value.created_at_unix_ms}};
}
inline void from_json (const nlohmann::json &json, gameplay_event_envelope_t &value)
{
    json.at ("eventId").get_to (value.event_id);
    json.at ("playerId").get_to (value.player_id);
    json.at ("idempotencyKey").get_to (value.idempotency_key);
    json.at ("eventType").get_to (value.event_type);
    json.at ("value").get_to (value.value);
    json.at ("count").get_to (value.count);
    json.at ("sourceApi").get_to (value.source_api);
    json.at ("createdAtUnixMs").get_to (value.created_at_unix_ms);
}
inline void to_json (nlohmann::json &json, const notify_quest_progress_req_t &value)
{
    json = {{"playerId", value.player_id},
            {"projection", value.projection},
            {"completedQuestId", value.completed_quest_id}};
}
inline void from_json (const nlohmann::json &json, notify_quest_progress_req_t &value)
{
    json.at ("playerId").get_to (value.player_id);
    json.at ("projection").get_to (value.projection);
    value.completed_quest_id = json.value ("completedQuestId", "");
}
inline void to_json (nlohmann::json &json, const notify_quest_progress_res_t &value)
{
    json = {{"delivered", value.delivered}};
}
inline void from_json (const nlohmann::json &json, notify_quest_progress_res_t &value)
{
    json.at ("delivered").get_to (value.delivered);
}
inline void to_json (nlohmann::json &json, const apply_gameplay_event_req_t &value)
{
    json = {{"event", value.event}};
}
inline void from_json (const nlohmann::json &json, apply_gameplay_event_req_t &value)
{
    json.at ("event").get_to (value.event);
}
inline void to_json (nlohmann::json &json, const apply_gameplay_event_res_t &value)
{
    json = {{"applied", value.applied}, {"projection", value.projection},
            {"completedQuestId", value.completed_quest_id}};
}
inline void from_json (const nlohmann::json &json, apply_gameplay_event_res_t &value)
{
    json.at ("applied").get_to (value.applied);
    json.at ("projection").get_to (value.projection);
    value.completed_quest_id = json.value ("completedQuestId", "");
}
inline void to_json (nlohmann::json &json, const server_assertion_req_t &)
{
    json = nlohmann::json::object ();
}
inline void from_json (const nlohmann::json &, server_assertion_req_t &) {}
inline void to_json (nlohmann::json &json, const server_assertion_res_t &value)
{
    json = {{"passed", value.passed}, {"evidence", value.evidence}};
}
inline void from_json (const nlohmann::json &json, server_assertion_res_t &value)
{
    json.at ("passed").get_to (value.passed);
    json.at ("evidence").get_to (value.evidence);
}

} // namespace zlink::samples::gamequest
