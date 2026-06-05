/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

namespace zlink::samples::bingo
{

struct sample_names_t
{
    static constexpr const char *api_channel = "bingo.api";
    static constexpr const char *play_channel = "bingo.play";
    static constexpr const char *router_channel = "bingo.gateway";
    static constexpr const char *stream_node = "bingo.client.stream";
    static constexpr const char *session_spot_node = "bingo.session.node";
    static constexpr const char *player_actor_type = "bingo.player";
    static constexpr const char *notification_channel = "bingo.notifications";
    static constexpr const char *room_spot_node = "bingo.room.node";
    static constexpr const char *room_spot = "bingo.room";
    static constexpr const char *room_spot_discovery = "bingo.rooms";
    static constexpr const char *player_joined_packet = "PlayerJoinedNotify";
    static constexpr const char *game_started_packet = "BingoGameStartedNotify";
    static constexpr const char *number_drawn_packet = "BingoNumberDrawnNotify";
    static constexpr const char *state_packet = "BingoStateNotify";
    static constexpr const char *game_ended_packet = "BingoGameEndedNotify";
};

} // namespace zlink::samples::bingo
