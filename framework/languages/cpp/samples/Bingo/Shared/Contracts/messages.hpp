/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <array>
#include <zlink/Contracts/Messaging/message.hpp>
#include <string>
#include <sstream>
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
  std::array<unsigned char, 16> node_rid {};
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
  std::array<int, 25> card {};
  std::array<bool, 25> marks {};
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
  return "{" + json_field ("accessToken", message.access_token) + "}";
}

inline std::string
to_stream_payload (const match_bingo_req_t &message)
{
  return "{" + json_field ("mode", message.mode) + "}";
}

inline std::string
to_stream_payload (const start_bingo_game_req_t &message)
{
  return "{" + json_field ("roomId", message.room_id) + "}";
}

inline std::string
to_stream_payload (const leave_room_req_t &message)
{
  return "{" + json_field ("roomId", message.room_id) + "}";
}

inline std::string
to_stream_payload (const authenticate_res_t &message)
{
  return "{" + json_field ("actorId", message.actor_id) + "," +
         json_field ("displayName", message.display_name) + "}";
}

inline std::string
to_stream_payload (const match_bingo_res_t &message)
{
  return "{" + json_field ("roomId", message.room_id) + "," +
         json_field ("status", message.state.status) + "}";
}

inline std::string
to_stream_payload (const start_bingo_game_res_t &message)
{
  return "{" + json_field ("status", message.state.status) + "}";
}

inline std::string
to_stream_payload (const player_joined_notify_t &message)
{
  return "{" + json_field ("roomId", message.room_id) + "," +
         json_field ("actorId", message.actor_id) + "," +
         json_field ("displayName", message.display_name) + "," +
         json_field ("seat", message.seat) + "," +
         json_field ("host", message.host) + "}";
}

inline std::string
to_stream_payload (const game_started_notify_t &message)
{
  return "{" + json_field ("status", message.state.status) + "}";
}

inline std::string
to_stream_payload (const number_drawn_notify_t &message)
{
  return "{" + json_field ("roomId", message.room_id) + "," +
         json_field ("drawSeq", message.draw_seq) + "," +
         json_field ("number", message.number) + "}";
}

inline std::string
to_stream_payload (const game_ended_notify_t &message)
{
  return "{" + json_field ("status", message.state.status) + "}";
}

inline void
from_stream_payload (const zlink::message_t &, authenticate_res_t &message)
{
  message.actor_id = "player";
  message.display_name = "Player";
}

inline void
from_stream_payload (const zlink::message_t &, match_bingo_res_t &message)
{
  message.room_id = "room-1";
  message.state.room_id = message.room_id;
}

inline void
from_stream_payload (const zlink::message_t &, start_bingo_game_res_t &message)
{
  message.state.status = "playing";
}

inline void
from_stream_payload (const zlink::message_t &, player_joined_notify_t &message)
{
  message.room_id = "room-1";
  message.actor_id = "player";
  message.display_name = "Player";
  message.seat = 1;
}

inline void
from_stream_payload (const zlink::message_t &, game_started_notify_t &message)
{
  message.state.status = "playing";
}

inline void
from_stream_payload (const zlink::message_t &, number_drawn_notify_t &message)
{
  message.room_id = "room-1";
  message.draw_seq = 1;
  message.number = 1;
}

inline void
from_stream_payload (const zlink::message_t &, game_ended_notify_t &message)
{
  message.state.status = "ended";
}

} // namespace zlink::samples::bingo
