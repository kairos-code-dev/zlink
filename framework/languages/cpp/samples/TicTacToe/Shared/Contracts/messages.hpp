/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>

#include <array>
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
  std::array<unsigned char, 16> node_rid {};
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

inline std::string
json_field (const char *name, const std::string &value)
{
  return std::string ("\"") + name + "\":\"" + value + "\"";
}

inline std::string
json_field (const char *name, bool value)
{
  return std::string ("\"") + name + "\":" + (value ? "true" : "false");
}

inline std::string
json_field (const char *name, int value)
{
  return std::string ("\"") + name + "\":" + std::to_string (value);
}

inline std::string
to_stream_payload (const authenticate_req_t &message)
{
  return "{" + json_field ("actorId", message.actor_id) + "}";
}

inline std::string
to_stream_payload (const join_match_req_t &message)
{
  return "{" + json_field ("matchId", message.match_id) + "," +
         json_field ("actorId", message.actor_id) + "}";
}

inline std::string
to_stream_payload (const place_mark_req_t &message)
{
  return "{" + json_field ("matchId", message.match_id) + "," +
         json_field ("actorId", message.actor_id) + "," +
         json_field ("cell", message.cell) + "}";
}

inline std::string
to_stream_payload (const authenticate_res_t &message)
{
  return "{" + json_field ("actorId", message.actor_id) + "}";
}

inline std::string
to_stream_payload (const join_match_res_t &message)
{
  return "{" + json_field ("matchId", message.match_id) + "," +
         json_field ("actorId", message.actor_id) + "," +
         json_field ("mark", message.mark) + "}";
}

inline std::string
to_stream_payload (const place_mark_res_t &message)
{
  return "{" + json_field ("matchId", message.state.match_id) + "," +
         json_field ("cell", message.state.last_move_cell) + "}";
}

inline std::string
to_stream_payload (const opponent_joined_notify_t &message)
{
  return "{" + json_field ("matchId", message.match_id) + "," +
         json_field ("opponentActorId", message.opponent_actor_id) + "," +
         json_field ("mark", message.mark) + "}";
}

inline std::string
to_stream_payload (const turn_changed_notify_t &message)
{
  return "{" + json_field ("matchId", message.match_id) + "," +
         json_field ("turnActorId", message.turn_actor_id) + "}";
}

inline std::string
to_stream_payload (const game_ended_notify_t &message)
{
  return "{" + json_field ("matchId", message.match_id) + "," +
         json_field ("winnerActorId", message.winner_actor_id) + "," +
         json_field ("draw", message.draw) + "}";
}

inline void
from_stream_payload (const zlink::message_t &, authenticate_res_t &message)
{
  message.actor_id = "player";
}

inline void
from_stream_payload (const zlink::message_t &, join_match_res_t &message)
{
  message.match_id = "game-1";
  message.actor_id = "player";
  message.mark = "X";
  message.state.match_id = message.match_id;
}

inline void
from_stream_payload (const zlink::message_t &, place_mark_res_t &message)
{
  message.state.match_id = "game-1";
  message.state.status = "playing";
}

inline void
from_stream_payload (const zlink::message_t &, opponent_joined_notify_t &message)
{
  message.match_id = "game-1";
  message.opponent_actor_id = "player";
  message.mark = "O";
}

inline void
from_stream_payload (const zlink::message_t &, turn_changed_notify_t &message)
{
  message.match_id = "game-1";
  message.turn_actor_id = "player";
}

inline void
from_stream_payload (const zlink::message_t &, game_ended_notify_t &message)
{
  message.match_id = "game-1";
  message.winner_actor_id = "player";
  message.state.status = "ended";
}

} // namespace zlink::samples::tictactoe
