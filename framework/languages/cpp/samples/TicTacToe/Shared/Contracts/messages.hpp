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

struct authenticate_player_req_t
{
    static constexpr const char *packet_name = "AuthenticatePlayerReq";
    std::string actor_id;
};

struct authenticate_player_res_t
{
    static constexpr const char *packet_name = "AuthenticatePlayerRes";
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

struct create_game_req_t
{
    static constexpr const char *packet_name = "CreateGameReq";
    std::string owner_actor_id;
};

struct create_game_res_t
{
    static constexpr const char *packet_name = "CreateGameRes";
    std::string room_id;
    std::string owner_actor_id;
    std::string play_endpoint;
};

struct create_game_room_req_t
{
    static constexpr const char *packet_name = "CreateGameRoomReq";
};

struct create_game_room_res_t
{
    static constexpr const char *packet_name = "CreateGameRoomRes";
    std::string room_id;
};

struct join_game_req_t
{
    static constexpr const char *packet_name = "JoinGameReq";
    std::string room_id;
    std::string actor_id;
};

struct tictactoe_state_t
{
    std::string room_id;
    std::string board = ".........";
    std::string status = "waiting";
    std::string next_turn;
    std::string winner;
    bool draw = false;
    std::string x_actor_id;
    std::string o_actor_id;
    std::string last_move_actor_id;
    int last_move_cell = -1;
};

struct join_game_res_t
{
    static constexpr const char *packet_name = "JoinGameRes";
    std::string room_id;
    std::string actor_id;
    std::string mark;
    tictactoe_state_t state;
};

struct place_mark_req_t
{
    static constexpr const char *packet_name = "PlaceMarkReq";
    std::string room_id;
    std::string actor_id;
    int cell = 0;
};

struct place_mark_res_t
{
    static constexpr const char *packet_name = "PlaceMarkRes";
    tictactoe_state_t state;
};

struct player_joined_notify_t
{
    static constexpr const char *packet_name = "PlayerJoinedNotify";
    std::string room_id;
    std::string actor_id;
    std::string mark;
    tictactoe_state_t state;
};

struct game_state_notify_t
{
    static constexpr const char *packet_name = "GameStateNotify";
    std::string room_id;
    std::string next_turn;
    tictactoe_state_t state;
};

struct game_ended_notify_t
{
    static constexpr const char *packet_name = "GameEndedNotify";
    std::string room_id;
    std::string winner;
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

inline void to_json (nlohmann::json &json, const authenticate_player_req_t &value)
{
    json = {{"actorId", value.actor_id}};
}

inline void from_json (const nlohmann::json &json, authenticate_player_req_t &value)
{
    value.actor_id = json.value ("actorId", "");
}

inline void to_json (nlohmann::json &json, const authenticate_player_res_t &value)
{
    json = {{"accepted", value.accepted}, {"actorId", value.actor_id}, {"reason", value.reason}};
}

inline void from_json (const nlohmann::json &json, authenticate_player_res_t &value)
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

inline void to_json (nlohmann::json &json, const create_game_req_t &value)
{
    json = {{"ownerActorId", value.owner_actor_id}};
}

inline void from_json (const nlohmann::json &json, create_game_req_t &value)
{
    value.owner_actor_id = json.value ("ownerActorId", "");
}

inline void to_json (nlohmann::json &json, const create_game_room_req_t &)
{
    json = nlohmann::json::object ();
}

inline void from_json (const nlohmann::json &, create_game_room_req_t &)
{
}

inline void to_json (nlohmann::json &json, const create_game_room_res_t &value)
{
    json = {{"roomId", value.room_id}};
}

inline void from_json (const nlohmann::json &json, create_game_room_res_t &value)
{
    value.room_id = json.value ("roomId", "");
}

inline void to_json (nlohmann::json &json, const join_game_req_t &value)
{
    json = {{"roomId", value.room_id}, {"actorId", value.actor_id}};
}

inline void from_json (const nlohmann::json &json, join_game_req_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.actor_id = json.value ("actorId", "");
}

inline void to_json (nlohmann::json &json, const place_mark_req_t &value)
{
    json = {{"roomId", value.room_id}, {"actorId", value.actor_id}, {"cell", value.cell}};
}

inline void from_json (const nlohmann::json &json, place_mark_req_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.actor_id = json.value ("actorId", "");
    value.cell = json.value ("cell", 0);
}

inline void to_json (nlohmann::json &json, const tictactoe_state_t &value)
{
    json = {{"roomId", value.room_id},
            {"board", value.board},
            {"status", value.status},
            {"nextTurn", value.next_turn},
            {"winner", value.winner},
            {"draw", value.draw},
            {"xActorId", value.x_actor_id},
            {"oActorId", value.o_actor_id},
            {"lastMoveActorId", value.last_move_actor_id},
            {"lastMoveCell", value.last_move_cell}};
}

inline void from_json (const nlohmann::json &json, tictactoe_state_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.board = json.value ("board", ".........");
    value.status = json.value ("status", "");
    value.next_turn = json.value ("nextTurn", "");
    value.winner = json.value ("winner", "");
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

inline void to_json (nlohmann::json &json, const create_game_res_t &value)
{
    json = {{"roomId", value.room_id},
            {"ownerActorId", value.owner_actor_id},
            {"playEndpoint", value.play_endpoint}};
}

inline void from_json (const nlohmann::json &json, create_game_res_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.owner_actor_id = json.value ("ownerActorId", "");
    value.play_endpoint = json.value ("playEndpoint", "");
}

inline void to_json (nlohmann::json &json, const join_game_res_t &value)
{
    json = {{"roomId", value.room_id},
            {"actorId", value.actor_id},
            {"mark", value.mark},
            {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, join_game_res_t &value)
{
    value.room_id = json.value ("roomId", "");
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

inline void to_json (nlohmann::json &json, const player_joined_notify_t &value)
{
    json = {{"roomId", value.room_id},
            {"actorId", value.actor_id},
            {"mark", value.mark},
            {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, player_joined_notify_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.actor_id = json.value ("actorId", "");
    value.mark = json.value ("mark", "");
    value.state = json.value ("state", tictactoe_state_t{});
}

inline void to_json (nlohmann::json &json, const game_state_notify_t &value)
{
    json = {{"roomId", value.room_id}, {"nextTurn", value.next_turn}, {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, game_state_notify_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.next_turn = json.value ("nextTurn", "");
    value.state = json.value ("state", tictactoe_state_t{});
}

inline void to_json (nlohmann::json &json, const game_ended_notify_t &value)
{
    json = {{"roomId", value.room_id},
            {"winner", value.winner},
            {"draw", value.draw},
            {"state", value.state}};
}

inline void from_json (const nlohmann::json &json, game_ended_notify_t &value)
{
    value.room_id = json.value ("roomId", "");
    value.winner = json.value ("winner", "");
    value.draw = json.value ("draw", false);
    value.state = json.value ("state", tictactoe_state_t{});
}

} // namespace zlink::samples::tictactoe
