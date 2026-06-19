/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/framework/codecs/json.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <array>

namespace zlink::samples::bingo
{

struct bingo_sample_modes_t
{
    static constexpr const char *two_player = "two-player";
};

struct bingo_sample_players_t
{
    static constexpr const char *player1 = "player-1";
    static constexpr const char *player2 = "player-2";
    static constexpr const char *observer = "observer";
};

struct bingo_reward_items_t
{
    static constexpr const char *golden_dauber_id = "rare-golden-dauber";
    static constexpr const char *golden_dauber_name = "Golden Dauber";
    static constexpr const char *legendary_rarity = "Legendary";
};

struct bingo_room_status_t
{
    static constexpr const char *waiting = "waiting";
    static constexpr const char *running = "running";
    static constexpr const char *finished = "finished";
};

struct authenticate_req_t
{
    static constexpr const char *packet_name = "AuthenticateReq";
    std::string access_token;
};

struct authenticate_res_t
{
    static constexpr const char *packet_name = "AuthenticateRes";
    std::string actor_id;
    std::string display_name;
    std::string actor_node_rid;
};

struct authenticate_player_req_t
{
    static constexpr const char *packet_name = "AuthenticatePlayerReq";
    std::string access_token;
};

struct authenticate_player_res_t
{
    static constexpr const char *packet_name = "AuthenticatePlayerRes";
    bool accepted = false;
    std::string actor_id;
    std::string display_name;
    std::string reason;
};

struct ensure_player_actor_req_t
{
    static constexpr const char *packet_name = "EnsurePlayerActorReq";
    std::string actor_id;
    std::string display_name;
    std::string preferred_actor_node_rid;
};

struct actor_ref_snapshot_t
{
    std::string node_rid;
    std::string actor_id;
    unsigned long long generation = 0;
};

struct ensure_player_actor_res_t
{
    static constexpr const char *packet_name = "EnsurePlayerActorRes";
    std::string actor_id;
    std::string actor_type;
    actor_ref_snapshot_t actor;
};

struct remote_actor_packet_req_t
{
    static constexpr const char *packet_name = "RemoteActorPacketReq";
    std::string actor_node_rid;
    std::string actor_type;
    std::string actor_id;
    unsigned long long actor_generation = 0;
    int header_kind = 0;
    int header_codec = 0;
    int header_flags = 0;
    bool request_seq_present = false;
    unsigned long long request_seq = 0;
    std::string relayed_packet_name;
    std::string bound_session_route_channel;
    std::string bound_session_node_rid;
    std::map<std::string, std::string> metadata;
    std::vector<unsigned char> payload;
};

struct remote_actor_packet_res_t
{
    static constexpr const char *packet_name = "RemoteActorPacketRes";
    bool actor_ref_present = false;
    std::string actor_node_rid;
    std::string actor_type;
    std::string actor_id;
    unsigned long long actor_generation = 0;
    bool has_reply = false;
    std::vector<unsigned char> reply_payload;
};

struct match_bingo_req_t
{
    static constexpr const char *packet_name = "MatchBingoReq";
    std::string mode;
};

struct match_bingo_api_req_t
{
    static constexpr const char *packet_name = "MatchBingoApiReq";
    std::string actor_id;
    std::string display_name;
    std::string mode;
    std::string actor_node_rid;
};

struct match_bingo_api_res_t
{
    static constexpr const char *packet_name = "MatchBingoApiRes";
    std::string room_id;
    std::string room_owner_node_rid;
};

struct allocate_bingo_room_req_t
{
    static constexpr const char *packet_name = "AllocateBingoRoomReq";
    std::string mode;
    std::string actor_id;
    std::string preferred_owner_node_rid;
};

struct allocate_bingo_room_res_t
{
    static constexpr const char *packet_name = "AllocateBingoRoomRes";
    std::string room_id;
    std::string room_owner_node_rid;
};

struct bingo_room_settings_payload_t
{
    static constexpr const char *packet_name = "BingoRoomSettingsPayload";
    std::string room_name;
    std::string mode;
    int required_players = 2;
    int max_draw_number = 75;
    std::string purpose = "Game";
    std::string observed_room_id;
};

struct bingo_player_state_t
{
    std::string actor_id;
    std::string display_name;
    int seat = 0;
    bool is_host = false;
    std::vector<int> card;
    std::vector<bool> marks;
    int completed_lines = 0;
};

struct bingo_room_state_t
{
    std::string room_id;
    std::string status = bingo_room_status_t::waiting;
    std::string host_actor_id;
    bool can_start = false;
    int draw_seq = 0;
    int last_drawn_number = 0;
    std::vector<int> drawn_numbers;
    std::vector<bingo_player_state_t> players;
    std::vector<std::string> winners;
};

struct match_bingo_res_t
{
    static constexpr const char *packet_name = "MatchBingoRes";
    std::string room_id;
    bingo_room_state_t state;
    std::string room_owner_node_rid;
};

struct bingo_room_join_req_t
{
    static constexpr const char *packet_name = "BingoRoomJoinReq";
    std::string room_id;
    std::string actor_id;
    std::string display_name;
    bool observe_only = false;
};

struct bingo_room_join_res_t
{
    static constexpr const char *packet_name = "BingoRoomJoinRes";
    bingo_room_state_t state;
};

struct submit_bingo_card_req_t
{
    static constexpr const char *packet_name = "SubmitBingoCardReq";
    std::string room_id;
    std::vector<int> card;
};

struct submit_bingo_card_res_t
{
    static constexpr const char *packet_name = "SubmitBingoCardRes";
    bingo_room_state_t state;
};

struct observe_bingo_events_req_t
{
    static constexpr const char *packet_name = "ObserveBingoEventsReq";
    std::string room_id;
};

struct observe_bingo_events_res_t
{
    static constexpr const char *packet_name = "ObserveBingoEventsRes";
    bool subscribed = false;
    std::string observer_node_rid;
};

struct stop_observing_bingo_events_req_t
{
    static constexpr const char *packet_name = "StopObservingBingoEventsReq";
    std::string room_id;
};

struct stop_observing_bingo_events_res_t
{
    static constexpr const char *packet_name = "StopObservingBingoEventsRes";
    bool stopped = false;
    std::string observer_node_rid;
};

struct player_joined_notify_t
{
    static constexpr const char *packet_name = "PlayerJoinedNotify";
    std::string room_id;
    std::string actor_id;
    std::string display_name;
    int seat = 0;
    bool is_host = false;
    bingo_room_state_t state;
};

struct game_started_notify_t
{
    static constexpr const char *packet_name = "BingoGameStartedNotify";
    bingo_room_state_t state;
};

struct number_drawn_notify_t
{
    static constexpr const char *packet_name = "BingoNumberDrawnNotify";
    std::string room_id;
    int draw_seq = 0;
    int number = 0;
    bingo_room_state_t state;
};

struct state_notify_t
{
    static constexpr const char *packet_name = "BingoStateNotify";
    bingo_room_state_t state;
};

struct game_ended_notify_t
{
    static constexpr const char *packet_name = "BingoGameEndedNotify";
    bingo_room_state_t state;
};

struct bingo_reward_announced_notify_t
{
    static constexpr const char *packet_name = "BingoRewardAnnouncedNotify";
    std::string room_id;
    std::string actor_id;
    int draw_seq = 0;
    std::string item_id;
    std::string item_name;
    std::string rarity;
    std::string receiving_spot_node_rid;
};

struct bingo_reward_acquired_event_t
{
    static constexpr const char *packet_name = "BingoRewardAcquiredEvent";
    std::string room_id;
    std::string actor_id;
    int draw_seq = 0;
    std::string item_id;
    std::string item_name;
    std::string rarity;
};

inline void to_json (nlohmann::json &json, const authenticate_req_t &value)
{
    json = {{"accessToken", value.access_token}};
}

inline void from_json (const nlohmann::json &json, authenticate_req_t &value)
{
    value.access_token = json.value ("accessToken", "");
}

inline void to_json (nlohmann::json &json, const authenticate_player_req_t &value)
{
    json = {{"accessToken", value.access_token}};
}

inline void from_json (const nlohmann::json &json, authenticate_player_req_t &value)
{
    value.access_token = json.value ("accessToken", "");
}

inline void to_json (nlohmann::json &json, const authenticate_player_res_t &value)
{
    json = {{"accepted", value.accepted},
            {"actorId", value.actor_id},
            {"displayName", value.display_name},
            {"reason", value.reason}};
}

inline void from_json (const nlohmann::json &json, authenticate_player_res_t &value)
{
    value.accepted = json.value ("accepted", false);
    value.actor_id = json.value ("actorId", "");
    value.display_name = json.value ("displayName", "");
    value.reason = json.value ("reason", "");
}

inline void to_json (nlohmann::json &json, const ensure_player_actor_req_t &value)
{
    json = {{"actorId", value.actor_id},
            {"displayName", value.display_name},
            {"preferredActorNodeRid", value.preferred_actor_node_rid}};
}

inline void from_json (const nlohmann::json &json, ensure_player_actor_req_t &value)
{
    value.actor_id = json.value ("actorId", "");
    value.display_name = json.value ("displayName", "");
    value.preferred_actor_node_rid = json.value ("preferredActorNodeRid", "");
}

inline void to_json (nlohmann::json &json, const actor_ref_snapshot_t &value)
{
    json = {
      {"nodeRid", value.node_rid}, {"actorId", value.actor_id}, {"generation", value.generation}};
}

inline void from_json (const nlohmann::json &json, actor_ref_snapshot_t &value)
{
    value.node_rid = json.value ("nodeRid", "");
    value.actor_id = json.value ("actorId", "");
    value.generation = json.value ("generation", 0ULL);
}

inline void to_json (nlohmann::json &json, const ensure_player_actor_res_t &value)
{
    json = {{"actorId", value.actor_id}, {"actorType", value.actor_type}, {"actor", value.actor}};
}

inline void from_json (const nlohmann::json &json, ensure_player_actor_res_t &value)
{
    value.actor_id = json.value ("actorId", "");
    value.actor_type = json.value ("actorType", "");
    value.actor = json.value ("actor", actor_ref_snapshot_t{});
}

inline void to_json (nlohmann::json &json, const remote_actor_packet_req_t &value)
{
    json = {{"actorNodeRid", value.actor_node_rid},
            {"actorType", value.actor_type},
            {"actorId", value.actor_id},
            {"actorGeneration", value.actor_generation},
            {"headerKind", value.header_kind},
            {"headerCodec", value.header_codec},
            {"headerFlags", value.header_flags},
            {"requestSeqPresent", value.request_seq_present},
            {"requestSeq", value.request_seq},
            {"packetName", value.relayed_packet_name},
            {"boundSessionRouteChannel", value.bound_session_route_channel},
            {"boundSessionNodeRid", value.bound_session_node_rid},
            {"metadata", value.metadata},
            {"payload", value.payload}};
}

inline void from_json (const nlohmann::json &json, remote_actor_packet_req_t &value)
{
    value.actor_node_rid = json.value ("actorNodeRid", "");
    value.actor_type = json.value ("actorType", "");
    value.actor_id = json.value ("actorId", "");
    value.actor_generation = json.value ("actorGeneration", 0ULL);
    value.header_kind = json.value ("headerKind", 0);
    value.header_codec = json.value ("headerCodec", 0);
    value.header_flags = json.value ("headerFlags", 0);
    value.request_seq_present = json.value ("requestSeqPresent", false);
    value.request_seq = json.value ("requestSeq", 0ULL);
    value.relayed_packet_name = json.value ("packetName", "");
    value.bound_session_route_channel = json.value ("boundSessionRouteChannel", "");
    value.bound_session_node_rid = json.value ("boundSessionNodeRid", "");
    value.metadata = json.value ("metadata", std::map<std::string, std::string>{});
    value.payload = json.value ("payload", std::vector<unsigned char>{});
}

inline void to_json (nlohmann::json &json, const remote_actor_packet_res_t &value)
{
    json = {{"actorRefPresent", value.actor_ref_present},
            {"actorNodeRid", value.actor_node_rid},
            {"actorType", value.actor_type},
            {"actorId", value.actor_id},
            {"actorGeneration", value.actor_generation},
            {"hasReply", value.has_reply},
            {"replyPayload", value.reply_payload}};
}

inline void from_json (const nlohmann::json &json, remote_actor_packet_res_t &value)
{
    value.actor_ref_present = json.value ("actorRefPresent", false);
    value.actor_node_rid = json.value ("actorNodeRid", "");
    value.actor_type = json.value ("actorType", "");
    value.actor_id = json.value ("actorId", "");
    value.actor_generation = json.value ("actorGeneration", 0ULL);
    value.has_reply = json.value ("hasReply", false);
    value.reply_payload = json.value ("replyPayload", std::vector<unsigned char>{});
}

inline void to_json (nlohmann::json &json, const match_bingo_req_t &value)
{
    json = {{"mode", value.mode}};
}

inline void from_json (const nlohmann::json &json, match_bingo_req_t &value)
{
    value.mode = json.value ("mode", "");
}

inline void to_json (nlohmann::json &json, const match_bingo_api_req_t &value)
{
    json = {{"actorId", value.actor_id},
            {"displayName", value.display_name},
            {"mode", value.mode},
            {"actorNodeRid", value.actor_node_rid}};
}

inline void from_json (const nlohmann::json &json, match_bingo_api_req_t &value)
{
    value.actor_id = json.value ("actorId", "");
    value.display_name = json.value ("displayName", "");
    value.mode = json.value ("mode", "");
    value.actor_node_rid = json.value ("actorNodeRid", "");
}

inline void to_json (nlohmann::json &json, const match_bingo_api_res_t &value)
{
    json = {{"roomId", value.room_id}, {"roomOwnerNodeRid", value.room_owner_node_rid}};
}

inline void from_json (const nlohmann::json &json, match_bingo_api_res_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.room_owner_node_rid = json.value ("roomOwnerNodeRid", "");
}

inline void to_json (nlohmann::json &json, const allocate_bingo_room_req_t &value)
{
    json = {{"mode", value.mode},
            {"actorId", value.actor_id},
            {"preferredOwnerNodeRid", value.preferred_owner_node_rid}};
}

inline void from_json (const nlohmann::json &json, allocate_bingo_room_req_t &value)
{
    value.mode = json.value ("mode", "");
    value.actor_id = json.value ("actorId", "");
    value.preferred_owner_node_rid = json.value ("preferredOwnerNodeRid", "");
}

inline void to_json (nlohmann::json &json, const allocate_bingo_room_res_t &value)
{
    json = {{"roomId", value.room_id}, {"roomOwnerNodeRid", value.room_owner_node_rid}};
}

inline void from_json (const nlohmann::json &json, allocate_bingo_room_res_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.room_owner_node_rid = json.value ("roomOwnerNodeRid", "");
}

inline void to_json (nlohmann::json &json, const bingo_room_settings_payload_t &value)
{
    json = {{"roomName", value.room_name},
            {"mode", value.mode},
            {"requiredPlayers", value.required_players},
            {"maxDrawNumber", value.max_draw_number},
            {"purpose", value.purpose}};
    if (!value.observed_room_id.empty ()) {
        json["observedRoomId"] = value.observed_room_id;
    }
}

inline void from_json (const nlohmann::json &json, bingo_room_settings_payload_t &value)
{
    value.room_name = json.value ("roomName", "");
    value.mode = json.value ("mode", "");
    value.required_players = json.value ("requiredPlayers", 2);
    value.max_draw_number = json.value ("maxDrawNumber", 75);
    value.purpose = json.value ("purpose", "Game");
    value.observed_room_id = json.value ("observedRoomId", "");
}

inline void to_json (nlohmann::json &json, const bingo_room_join_req_t &value)
{
    json = {{"roomId", value.room_id},
            {"actorId", value.actor_id},
            {"displayName", value.display_name},
            {"observeOnly", value.observe_only}};
}

inline void from_json (const nlohmann::json &json, bingo_room_join_req_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.actor_id = json.value ("actorId", "");
    value.display_name = json.value ("displayName", "");
    value.observe_only = json.value ("observeOnly", false);
}

inline void to_json (nlohmann::json &json, const submit_bingo_card_req_t &value)
{
    json = {{"roomId", value.room_id}, {"card", value.card}};
}

inline void from_json (const nlohmann::json &json, submit_bingo_card_req_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.card = json.value ("card", std::vector<int>{});
}

inline void to_json (nlohmann::json &json, const bingo_player_state_t &value)
{
    json = {{"actorId", value.actor_id},
            {"displayName", value.display_name},
            {"seat", value.seat},
            {"isHost", value.is_host},
            {"card", value.card},
            {"marks", value.marks},
            {"completedLines", value.completed_lines}};
}

inline void from_json (const nlohmann::json &json, bingo_player_state_t &value)
{
    value.actor_id = json.value ("actorId", "");
    value.display_name = json.value ("displayName", "");
    value.seat = json.value ("seat", 0);
    value.is_host = json.value ("isHost", false);
    value.card = json.value ("card", std::vector<int>{});
    value.marks = json.value ("marks", std::vector<bool>{});
    value.completed_lines = json.value ("completedLines", 0);
}

inline void to_json (nlohmann::json &json, const bingo_room_state_t &value)
{
    json = {{"roomId", value.room_id},
            {"status", value.status},
            {"hostActorId", value.host_actor_id},
            {"canStart", value.can_start},
            {"drawSeq", value.draw_seq},
            {"lastDrawnNumber", value.last_drawn_number},
            {"drawnNumbers", value.drawn_numbers},
            {"players", value.players},
            {"winners", value.winners}};
}

inline void from_json (const nlohmann::json &json, bingo_room_state_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.status = json.value ("status", "");
    value.host_actor_id = json.value ("hostActorId", "");
    value.can_start = json.value ("canStart", false);
    value.draw_seq = json.value ("drawSeq", 0);
    value.last_drawn_number = json.value ("lastDrawnNumber", 0);
    value.drawn_numbers = json.value ("drawnNumbers", std::vector<int>{});
    value.players = json.value ("players", std::vector<bingo_player_state_t>{});
    value.winners = json.value ("winners", std::vector<std::string>{});
}

inline void to_json (nlohmann::json &json, const authenticate_res_t &value)
{
    json = {{"actorId", value.actor_id},
            {"displayName", value.display_name},
            {"actorNodeRid", value.actor_node_rid}};
}

inline void from_json (const nlohmann::json &json, authenticate_res_t &value)
{
    value.actor_id = json.value ("actorId", json.value ("ActorId", ""));
    value.display_name = json.value ("displayName", json.value ("DisplayName", ""));
    value.actor_node_rid = json.value ("actorNodeRid", "");
}

inline void to_json (nlohmann::json &json, const match_bingo_res_t &value)
{
    json = {{"roomId", value.room_id},
            {"state", value.state},
            {"roomOwnerNodeRid", value.room_owner_node_rid}};
}

inline void from_json (const nlohmann::json &json, match_bingo_res_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.state = json.value ("state", bingo_room_state_t{});
    value.room_owner_node_rid = json.value ("roomOwnerNodeRid", "");
}

inline void to_json (nlohmann::json &json, const bingo_room_join_res_t &value)
{
    json = {{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, bingo_room_join_res_t &value)
{
    value.state = json.value ("state", bingo_room_state_t{});
}

inline void to_json (nlohmann::json &json, const submit_bingo_card_res_t &value)
{
    json = {{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, submit_bingo_card_res_t &value)
{
    value.state = json.value ("state", bingo_room_state_t{});
}

inline void to_json (nlohmann::json &json, const observe_bingo_events_req_t &value)
{
    json = {{"roomId", value.room_id}};
}

inline void from_json (const nlohmann::json &json, observe_bingo_events_req_t &value)
{
    value.room_id = json.value ("roomId", "");
}

inline void to_json (nlohmann::json &json, const observe_bingo_events_res_t &value)
{
    json = {{"subscribed", value.subscribed}, {"observerNodeRid", value.observer_node_rid}};
}

inline void from_json (const nlohmann::json &json, observe_bingo_events_res_t &value)
{
    value.subscribed = json.value ("subscribed", false);
    value.observer_node_rid = json.value ("observerNodeRid", "");
}

inline void to_json (nlohmann::json &json, const stop_observing_bingo_events_req_t &value)
{
    json = {{"roomId", value.room_id}};
}

inline void from_json (const nlohmann::json &json, stop_observing_bingo_events_req_t &value)
{
    value.room_id = json.value ("roomId", "");
}

inline void to_json (nlohmann::json &json, const stop_observing_bingo_events_res_t &value)
{
    json = {{"stopped", value.stopped}, {"observerNodeRid", value.observer_node_rid}};
}

inline void from_json (const nlohmann::json &json, stop_observing_bingo_events_res_t &value)
{
    value.stopped = json.value ("stopped", false);
    value.observer_node_rid = json.value ("observerNodeRid", "");
}

inline void to_json (nlohmann::json &json, const player_joined_notify_t &value)
{
    json = {
      {"roomId", value.room_id}, {"actorId", value.actor_id}, {"displayName", value.display_name},
      {"seat", value.seat},      {"isHost", value.is_host},   {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, player_joined_notify_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.actor_id = json.value ("actorId", "");
    value.display_name = json.value ("displayName", "");
    value.seat = json.value ("seat", 0);
    value.is_host = json.value ("isHost", false);
    value.state = json.value ("state", bingo_room_state_t{});
}

inline void to_json (nlohmann::json &json, const game_started_notify_t &value)
{
    json = {{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, game_started_notify_t &value)
{
    value.state = json.value ("state", bingo_room_state_t{});
}

inline void to_json (nlohmann::json &json, const state_notify_t &value)
{
    json = {{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, state_notify_t &value)
{
    value.state = json.value ("state", bingo_room_state_t{});
}

inline void to_json (nlohmann::json &json, const number_drawn_notify_t &value)
{
    json = {{"roomId", value.room_id},
            {"drawSeq", value.draw_seq},
            {"number", value.number},
            {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, number_drawn_notify_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.draw_seq = json.value ("drawSeq", 0);
    value.number = json.value ("number", 0);
    value.state = json.value ("state", bingo_room_state_t{});
}

inline void to_json (nlohmann::json &json, const game_ended_notify_t &value)
{
    json = {{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, game_ended_notify_t &value)
{
    value.state = json.value ("state", bingo_room_state_t{});
}

inline void to_json (nlohmann::json &json, const bingo_reward_announced_notify_t &value)
{
    json = {{"roomId", value.room_id},
            {"actorId", value.actor_id},
            {"drawSeq", value.draw_seq},
            {"itemId", value.item_id},
            {"itemName", value.item_name},
            {"rarity", value.rarity},
            {"receivingSpotNodeRid", value.receiving_spot_node_rid}};
}

inline void from_json (const nlohmann::json &json, bingo_reward_announced_notify_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.actor_id = json.value ("actorId", "");
    value.draw_seq = json.value ("drawSeq", 0);
    value.item_id = json.value ("itemId", "");
    value.item_name = json.value ("itemName", "");
    value.rarity = json.value ("rarity", "");
    value.receiving_spot_node_rid = json.value ("receivingSpotNodeRid", "");
}

inline void to_json (nlohmann::json &json, const bingo_reward_acquired_event_t &value)
{
    json = {{"roomId", value.room_id},
            {"actorId", value.actor_id},
            {"drawSeq", value.draw_seq},
            {"itemId", value.item_id},
            {"itemName", value.item_name},
            {"rarity", value.rarity}};
}

inline void from_json (const nlohmann::json &json, bingo_reward_acquired_event_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.actor_id = json.value ("actorId", "");
    value.draw_seq = json.value ("drawSeq", 0);
    value.item_id = json.value ("itemId", "");
    value.item_name = json.value ("itemName", "");
    value.rarity = json.value ("rarity", "");
}

namespace detail
{

inline void append_protobuf_varint (std::vector<std::uint8_t> &bytes, std::size_t value)
{
    while (value >= 0x80) {
        bytes.push_back (static_cast<std::uint8_t> ((value & 0x7f) | 0x80));
        value >>= 7;
    }
    bytes.push_back (static_cast<std::uint8_t> (value));
}

inline std::size_t read_protobuf_varint (const std::vector<std::uint8_t> &bytes,
                                         std::size_t &offset)
{
    std::size_t value = 0;
    int shift = 0;
    while (offset < bytes.size ()) {
        const auto byte = bytes[offset++];
        value |= static_cast<std::size_t> (byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            return value;
        }
        shift += 7;
        if (shift >= static_cast<int> (sizeof (std::size_t) * 8)) {
            throw std::runtime_error ("protobuf payload varint is too large");
        }
    }
    throw std::runtime_error ("protobuf payload varint is truncated");
}

inline zlink::message_t json_to_protobuf_payload (const nlohmann::json &json)
{
    const auto text = json.dump ();
    std::vector<std::uint8_t> bytes;
    bytes.reserve (1 + text.size () + 8);
    bytes.push_back (0x0a);
    append_protobuf_varint (bytes, text.size ());
    bytes.insert (bytes.end (), text.begin (), text.end ());
    return zlink::message_t::from (bytes);
}

inline nlohmann::json json_from_protobuf_payload (const zlink::message_t &payload)
{
    const auto bytes = payload.to_bytes ();
    std::size_t offset = 0;
    if (offset >= bytes.size () || bytes[offset++] != 0x0a) {
        throw std::runtime_error ("protobuf payload must contain field 1");
    }
    const auto size = read_protobuf_varint (bytes, offset);
    if (offset + size > bytes.size ()) {
        throw std::runtime_error ("protobuf payload string is truncated");
    }
    return nlohmann::json::parse (bytes.begin () + static_cast<std::ptrdiff_t> (offset),
                                  bytes.begin ()
                                    + static_cast<std::ptrdiff_t> (offset + size));
}

} // namespace detail

template <typename T> inline zlink::message_t to_stream_payload (const T &value)
{
    return detail::json_to_protobuf_payload (nlohmann::json (value));
}

template <typename T> inline void from_stream_payload (const zlink::message_t &payload, T &value)
{
    value = detail::json_from_protobuf_payload (payload).template get<T> ();
}

} // namespace zlink::samples::bingo
