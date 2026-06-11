/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

namespace zlink::samples::tictactoe
{

struct sample_names_t
{
    static constexpr const char *api_channel = "tictactoe.api";
    static constexpr const char *play_channel = "tictactoe.play";
    static constexpr const char *router_channel = "tictactoe.gateway";
    static constexpr const char *stream_name = "client.stream";
    static constexpr const char *session_spot_node = "tictactoe.session.node";
    static constexpr const char *actor_type = "player";
    static constexpr const char *match_spot = "tictactoe.game";
    static constexpr const char *spot_node = "tictactoe.game.node";
    static constexpr const char *game_spot_discovery = "tictactoe.game.rooms";
    static constexpr const char *entry_spot_routing_id = "3301";
    static constexpr const char *x_actor_id = "player-x";
    static constexpr const char *o_actor_id = "player-o";
    static constexpr const char *game_state_packet = "GameStateNotify";
    static constexpr const char *player_joined_packet = "PlayerJoinedNotify";
    static constexpr const char *game_ended_packet = "GameEndedNotify";
};

} // namespace zlink::samples::tictactoe
