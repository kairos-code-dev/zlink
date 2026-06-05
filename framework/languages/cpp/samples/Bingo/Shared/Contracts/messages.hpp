/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <array>
#include <zlink/Contracts/Messaging/message.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace zlink::samples::bingo
{

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
};

struct actor_ref_snapshot_t
{
    std::array<unsigned char, 16> node_rid{};
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
};

struct match_bingo_api_res_t
{
    static constexpr const char *packet_name = "MatchBingoApiRes";
    std::string room_id;
};

struct allocate_bingo_room_req_t
{
    static constexpr const char *packet_name = "AllocateBingoRoomReq";
    std::string mode;
};

struct allocate_bingo_room_res_t
{
    static constexpr const char *packet_name = "AllocateBingoRoomRes";
    std::string room_id;
};

struct bingo_player_state_t
{
    std::string actor_id;
    std::string display_name;
    int seat = 0;
    bool host = false;
    std::array<int, 25> card{};
    std::array<bool, 25> marks{};
    int completed_lines = 0;
};

struct bingo_room_state_t
{
    std::string room_id;
    std::string status = "waiting";
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
};

struct bingo_room_join_req_t
{
    static constexpr const char *packet_name = "BingoRoomJoinReq";
    std::string room_id;
    std::string actor_id;
    std::string display_name;
};

struct bingo_room_join_res_t
{
    static constexpr const char *packet_name = "BingoRoomJoinRes";
    bingo_room_state_t state;
};

struct start_bingo_game_req_t
{
    static constexpr const char *packet_name = "StartBingoGameReq";
    std::string room_id;
};

struct start_bingo_game_res_t
{
    static constexpr const char *packet_name = "StartBingoGameRes";
    bingo_room_state_t state;
};

struct leave_room_req_t
{
    static constexpr const char *packet_name = "LeaveRoomReq";
    std::string room_id;
};

struct leave_room_res_t
{
    static constexpr const char *packet_name = "LeaveRoomRes";
    bingo_room_state_t state;
};

struct player_joined_notify_t
{
    static constexpr const char *packet_name = "PlayerJoinedNotify";
    std::string room_id;
    std::string actor_id;
    std::string display_name;
    int seat = 0;
    bool host = false;
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
    json = {{"actorId", value.actor_id}, {"displayName", value.display_name}};
}

inline void from_json (const nlohmann::json &json, ensure_player_actor_req_t &value)
{
    value.actor_id = json.value ("actorId", "");
    value.display_name = json.value ("displayName", "");
}

inline void to_json (nlohmann::json &json, const actor_ref_snapshot_t &value)
{
    json = {{"nodeRid", value.node_rid}, {"actorId", value.actor_id}, {"generation", value.generation}};
}

inline void from_json (const nlohmann::json &json, actor_ref_snapshot_t &value)
{
    value.node_rid = json.value ("nodeRid", std::array<unsigned char, 16>{});
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
    json = {{"actorId", value.actor_id}, {"displayName", value.display_name}, {"mode", value.mode}};
}

inline void from_json (const nlohmann::json &json, match_bingo_api_req_t &value)
{
    value.actor_id = json.value ("actorId", "");
    value.display_name = json.value ("displayName", "");
    value.mode = json.value ("mode", "");
}

inline void to_json (nlohmann::json &json, const match_bingo_api_res_t &value)
{
    json = {{"roomId", value.room_id}};
}

inline void from_json (const nlohmann::json &json, match_bingo_api_res_t &value)
{
    value.room_id = json.value ("roomId", "");
}

inline void to_json (nlohmann::json &json, const allocate_bingo_room_req_t &value)
{
    json = {{"mode", value.mode}};
}

inline void from_json (const nlohmann::json &json, allocate_bingo_room_req_t &value)
{
    value.mode = json.value ("mode", "");
}

inline void to_json (nlohmann::json &json, const allocate_bingo_room_res_t &value)
{
    json = {{"roomId", value.room_id}};
}

inline void from_json (const nlohmann::json &json, allocate_bingo_room_res_t &value)
{
    value.room_id = json.value ("roomId", "");
}

inline void to_json (nlohmann::json &json, const bingo_room_join_req_t &value)
{
    json = {{"roomId", value.room_id}, {"actorId", value.actor_id}, {"displayName", value.display_name}};
}

inline void from_json (const nlohmann::json &json, bingo_room_join_req_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.actor_id = json.value ("actorId", "");
    value.display_name = json.value ("displayName", "");
}

inline void to_json (nlohmann::json &json, const start_bingo_game_req_t &value)
{
    json = {{"roomId", value.room_id}};
}

inline void from_json (const nlohmann::json &json, start_bingo_game_req_t &value)
{
    value.room_id = json.value ("roomId", "");
}

inline void to_json (nlohmann::json &json, const leave_room_req_t &value)
{
    json = {{"roomId", value.room_id}};
}

inline void from_json (const nlohmann::json &json, leave_room_req_t &value)
{
    value.room_id = json.value ("roomId", "");
}

inline void to_json (nlohmann::json &json, const bingo_player_state_t &value)
{
    json = {{"actorId", value.actor_id},
            {"displayName", value.display_name},
            {"seat", value.seat},
            {"host", value.host},
            {"card", value.card},
            {"marks", value.marks},
            {"completedLines", value.completed_lines}};
}

inline void from_json (const nlohmann::json &json, bingo_player_state_t &value)
{
    value.actor_id = json.value ("actorId", "");
    value.display_name = json.value ("displayName", "");
    value.seat = json.value ("seat", 0);
    value.host = json.value ("host", false);
    value.card = json.value ("card", std::array<int, 25>{});
    value.marks = json.value ("marks", std::array<bool, 25>{});
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
    json = {{"actorId", value.actor_id}, {"displayName", value.display_name}};
}

inline void from_json (const nlohmann::json &json, authenticate_res_t &value)
{
    value.actor_id = json.value ("actorId", "");
    value.display_name = json.value ("displayName", "");
}

inline void to_json (nlohmann::json &json, const match_bingo_res_t &value)
{
    json = {{"roomId", value.room_id}, {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, match_bingo_res_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.state = json.value ("state", bingo_room_state_t{});
}

inline void to_json (nlohmann::json &json, const bingo_room_join_res_t &value)
{
    json = {{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, bingo_room_join_res_t &value)
{
    value.state = json.value ("state", bingo_room_state_t{});
}

inline void to_json (nlohmann::json &json, const start_bingo_game_res_t &value)
{
    json = {{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, start_bingo_game_res_t &value)
{
    value.state = json.value ("state", bingo_room_state_t{});
}

inline void to_json (nlohmann::json &json, const leave_room_res_t &value)
{
    json = {{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, leave_room_res_t &value)
{
    value.state = json.value ("state", bingo_room_state_t{});
}

inline void to_json (nlohmann::json &json, const player_joined_notify_t &value)
{
    json = {{"roomId", value.room_id}, {"actorId", value.actor_id}, {"displayName", value.display_name},
            {"seat", value.seat},      {"host", value.host},        {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, player_joined_notify_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.actor_id = json.value ("actorId", "");
    value.display_name = json.value ("displayName", "");
    value.seat = json.value ("seat", 0);
    value.host = json.value ("host", false);
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
    json = {{"roomId", value.room_id}, {"drawSeq", value.draw_seq}, {"number", value.number}, {"state", value.state}};
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

} // namespace zlink::samples::bingo
