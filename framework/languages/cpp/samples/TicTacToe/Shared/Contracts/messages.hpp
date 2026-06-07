/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>

#include <array>
#include <nlohmann/json.hpp>
#include <string>

namespace zlink::samples::tictactoe
{

struct authenticate_req_t
{
    static constexpr const char *packet_name = "AuthenticateReq";
    std::string actor_id;
};

struct authenticate_res_t
{
    static constexpr const char *packet_name = "AuthenticateRes";
    std::string actor_id;
};

struct authenticate_actor_req_t
{
    static constexpr const char *packet_name = "AuthenticateActorReq";
    std::string actor_id;
};

struct authenticate_actor_res_t
{
    static constexpr const char *packet_name = "AuthenticateActorRes";
    bool accepted = false;
    std::string actor_id;
    std::string reason;
};

struct actor_ref_snapshot_t
{
    std::array<unsigned char, 16> node_rid{};
    std::string actor_id;
    unsigned long long generation = 0;
};

struct ensure_player_actor_req_t
{
    static constexpr const char *packet_name = "EnsurePlayerActorReq";
    std::string actor_id;
};

struct ensure_player_actor_res_t
{
    static constexpr const char *packet_name = "EnsurePlayerActorRes";
    std::string actor_id;
    std::string actor_type;
    actor_ref_snapshot_t actor;
};

struct create_match_req_t
{
    static constexpr const char *packet_name = "CreateMatchReq";
    std::string owner_actor_id;
};

struct create_match_res_t
{
    static constexpr const char *packet_name = "CreateMatchRes";
    std::string match_id;
    std::string owner_actor_id;
    std::string play_endpoint;
};

struct create_match_room_req_t
{
    static constexpr const char *packet_name = "CreateMatchRoomReq";
};

struct create_match_room_res_t
{
    static constexpr const char *packet_name = "CreateMatchRoomRes";
    std::string match_id;
};

struct join_match_req_t
{
    static constexpr const char *packet_name = "JoinMatchReq";
    std::string match_id;
    std::string actor_id;
};

struct tictactoe_state_t
{
    std::string match_id;
    std::string board = ".........";
    std::string status = "waiting";
    std::string turn_actor_id;
    std::string winner_actor_id;
    bool draw = false;
    std::string x_actor_id;
    std::string o_actor_id;
    std::string last_move_actor_id;
    int last_move_cell = -1;
};

struct join_match_res_t
{
    static constexpr const char *packet_name = "JoinMatchRes";
    std::string match_id;
    std::string actor_id;
    std::string mark;
    tictactoe_state_t state;
};

struct place_mark_req_t
{
    static constexpr const char *packet_name = "PlaceMarkReq";
    std::string match_id;
    std::string actor_id;
    int cell = 0;
};

struct place_mark_res_t
{
    static constexpr const char *packet_name = "PlaceMarkRes";
    tictactoe_state_t state;
};

struct opponent_joined_notify_t
{
    static constexpr const char *packet_name = "OpponentJoinedNotify";
    std::string match_id;
    std::string opponent_actor_id;
    std::string mark;
    tictactoe_state_t state;
};

struct turn_changed_notify_t
{
    static constexpr const char *packet_name = "TurnChangedNotify";
    std::string match_id;
    std::string turn_actor_id;
    tictactoe_state_t state;
};

struct game_ended_notify_t
{
    static constexpr const char *packet_name = "GameEndedNotify";
    std::string match_id;
    std::string winner_actor_id;
    bool draw = false;
    tictactoe_state_t state;
};

inline void to_json (nlohmann::json &json, const authenticate_req_t &value)
{
    json = {{"actorId", value.actor_id}};
}

inline void from_json (const nlohmann::json &json, authenticate_req_t &value)
{
    value.actor_id = json.value ("actorId", "");
}

inline void to_json (nlohmann::json &json, const authenticate_actor_req_t &value)
{
    json = {{"actorId", value.actor_id}};
}

inline void from_json (const nlohmann::json &json, authenticate_actor_req_t &value)
{
    value.actor_id = json.value ("actorId", "");
}

inline void to_json (nlohmann::json &json, const authenticate_actor_res_t &value)
{
    json = {{"accepted", value.accepted}, {"actorId", value.actor_id}, {"reason", value.reason}};
}

inline void from_json (const nlohmann::json &json, authenticate_actor_res_t &value)
{
    value.accepted = json.value ("accepted", false);
    value.actor_id = json.value ("actorId", "");
    value.reason = json.value ("reason", "");
}

inline void to_json (nlohmann::json &json, const actor_ref_snapshot_t &value)
{
    json = {
      {"nodeRid", value.node_rid}, {"actorId", value.actor_id}, {"generation", value.generation}};
}

inline void from_json (const nlohmann::json &json, actor_ref_snapshot_t &value)
{
    value.node_rid = json.value ("nodeRid", std::array<unsigned char, 16>{});
    value.actor_id = json.value ("actorId", "");
    value.generation = json.value ("generation", 0ULL);
}

inline void to_json (nlohmann::json &json, const ensure_player_actor_req_t &value)
{
    json = {{"actorId", value.actor_id}};
}

inline void from_json (const nlohmann::json &json, ensure_player_actor_req_t &value)
{
    value.actor_id = json.value ("actorId", "");
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

inline void to_json (nlohmann::json &json, const create_match_req_t &value)
{
    json = {{"ownerActorId", value.owner_actor_id}};
}

inline void from_json (const nlohmann::json &json, create_match_req_t &value)
{
    value.owner_actor_id = json.value ("ownerActorId", "");
}

inline void to_json (nlohmann::json &json, const create_match_room_req_t &)
{
    json = nlohmann::json::object ();
}

inline void from_json (const nlohmann::json &, create_match_room_req_t &)
{
}

inline void to_json (nlohmann::json &json, const create_match_room_res_t &value)
{
    json = {{"matchId", value.match_id}};
}

inline void from_json (const nlohmann::json &json, create_match_room_res_t &value)
{
    value.match_id = json.value ("matchId", "");
}

inline void to_json (nlohmann::json &json, const join_match_req_t &value)
{
    json = {{"matchId", value.match_id}, {"actorId", value.actor_id}};
}

inline void from_json (const nlohmann::json &json, join_match_req_t &value)
{
    value.match_id = json.value ("matchId", "");
    value.actor_id = json.value ("actorId", "");
}

inline void to_json (nlohmann::json &json, const place_mark_req_t &value)
{
    json = {{"matchId", value.match_id}, {"actorId", value.actor_id}, {"cell", value.cell}};
}

inline void from_json (const nlohmann::json &json, place_mark_req_t &value)
{
    value.match_id = json.value ("matchId", "");
    value.actor_id = json.value ("actorId", "");
    value.cell = json.value ("cell", 0);
}

inline void to_json (nlohmann::json &json, const tictactoe_state_t &value)
{
    json = {{"matchId", value.match_id},
            {"board", value.board},
            {"status", value.status},
            {"turnActorId", value.turn_actor_id},
            {"winnerActorId", value.winner_actor_id},
            {"draw", value.draw},
            {"xActorId", value.x_actor_id},
            {"oActorId", value.o_actor_id},
            {"lastMoveActorId", value.last_move_actor_id},
            {"lastMoveCell", value.last_move_cell}};
}

inline void from_json (const nlohmann::json &json, tictactoe_state_t &value)
{
    value.match_id = json.value ("matchId", "");
    value.board = json.value ("board", ".........");
    value.status = json.value ("status", "");
    value.turn_actor_id = json.value ("turnActorId", "");
    value.winner_actor_id = json.value ("winnerActorId", "");
    value.draw = json.value ("draw", false);
    value.x_actor_id = json.value ("xActorId", "");
    value.o_actor_id = json.value ("oActorId", "");
    value.last_move_actor_id = json.value ("lastMoveActorId", "");
    value.last_move_cell = json.value ("lastMoveCell", -1);
}

inline void to_json (nlohmann::json &json, const authenticate_res_t &value)
{
    json = {{"actorId", value.actor_id}};
}

inline void from_json (const nlohmann::json &json, authenticate_res_t &value)
{
    value.actor_id = json.value ("actorId", "");
}

inline void to_json (nlohmann::json &json, const create_match_res_t &value)
{
    json = {{"matchId", value.match_id},
            {"ownerActorId", value.owner_actor_id},
            {"playEndpoint", value.play_endpoint}};
}

inline void from_json (const nlohmann::json &json, create_match_res_t &value)
{
    value.match_id = json.value ("matchId", "");
    value.owner_actor_id = json.value ("ownerActorId", "");
    value.play_endpoint = json.value ("playEndpoint", "");
}

inline void to_json (nlohmann::json &json, const join_match_res_t &value)
{
    json = {{"matchId", value.match_id},
            {"actorId", value.actor_id},
            {"mark", value.mark},
            {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, join_match_res_t &value)
{
    value.match_id = json.value ("matchId", "");
    value.actor_id = json.value ("actorId", "");
    value.mark = json.value ("mark", "");
    value.state = json.value ("state", tictactoe_state_t{});
}

inline void to_json (nlohmann::json &json, const place_mark_res_t &value)
{
    json = {{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, place_mark_res_t &value)
{
    value.state = json.value ("state", tictactoe_state_t{});
}

inline void to_json (nlohmann::json &json, const opponent_joined_notify_t &value)
{
    json = {{"matchId", value.match_id},
            {"opponentActorId", value.opponent_actor_id},
            {"mark", value.mark},
            {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, opponent_joined_notify_t &value)
{
    value.match_id = json.value ("matchId", "");
    value.opponent_actor_id = json.value ("opponentActorId", "");
    value.mark = json.value ("mark", "");
    value.state = json.value ("state", tictactoe_state_t{});
}

inline void to_json (nlohmann::json &json, const turn_changed_notify_t &value)
{
    json = {
      {"matchId", value.match_id}, {"turnActorId", value.turn_actor_id}, {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, turn_changed_notify_t &value)
{
    value.match_id = json.value ("matchId", "");
    value.turn_actor_id = json.value ("turnActorId", "");
    value.state = json.value ("state", tictactoe_state_t{});
}

inline void to_json (nlohmann::json &json, const game_ended_notify_t &value)
{
    json = {{"matchId", value.match_id},
            {"winnerActorId", value.winner_actor_id},
            {"draw", value.draw},
            {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, game_ended_notify_t &value)
{
    value.match_id = json.value ("matchId", "");
    value.winner_actor_id = json.value ("winnerActorId", "");
    value.draw = json.value ("draw", false);
    value.state = json.value ("state", tictactoe_state_t{});
}

} // namespace zlink::samples::tictactoe
