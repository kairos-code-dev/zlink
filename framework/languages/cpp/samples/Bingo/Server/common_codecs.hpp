/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <zlink/codecs/protobuf.hpp>
#include <zlink/framework.hpp>

namespace zlink::samples::bingo
{

template <typename TPayload>
inline void add_bingo_protobuf_serializer (framework::codec_options_builder_t &codecs)
{
    framework_codecs::protobuf_codec_extension_t::register_payload_serializer<TPayload> (codecs);
}

inline void add_bingo_protobuf_codecs (framework::codec_options_builder_t codecs)
{
    codecs.use (framework_codecs::protobuf ());
    add_bingo_protobuf_serializer<authenticate_req_t> (codecs);
    add_bingo_protobuf_serializer<authenticate_res_t> (codecs);
    add_bingo_protobuf_serializer<authenticate_player_req_t> (codecs);
    add_bingo_protobuf_serializer<authenticate_player_res_t> (codecs);
    add_bingo_protobuf_serializer<ensure_player_actor_req_t> (codecs);
    add_bingo_protobuf_serializer<ensure_player_actor_res_t> (codecs);
    add_bingo_protobuf_serializer<actor_ref_snapshot_t> (codecs);
    add_bingo_protobuf_serializer<match_bingo_req_t> (codecs);
    add_bingo_protobuf_serializer<match_bingo_res_t> (codecs);
    add_bingo_protobuf_serializer<match_bingo_api_req_t> (codecs);
    add_bingo_protobuf_serializer<match_bingo_api_res_t> (codecs);
    add_bingo_protobuf_serializer<allocate_bingo_room_req_t> (codecs);
    add_bingo_protobuf_serializer<allocate_bingo_room_res_t> (codecs);
    add_bingo_protobuf_serializer<bingo_room_settings_payload_t> (codecs);
    add_bingo_protobuf_serializer<bingo_player_state_t> (codecs);
    add_bingo_protobuf_serializer<bingo_room_state_t> (codecs);
    add_bingo_protobuf_serializer<bingo_room_join_req_t> (codecs);
    add_bingo_protobuf_serializer<bingo_room_join_res_t> (codecs);
    add_bingo_protobuf_serializer<submit_bingo_card_req_t> (codecs);
    add_bingo_protobuf_serializer<submit_bingo_card_res_t> (codecs);
    add_bingo_protobuf_serializer<observe_bingo_events_req_t> (codecs);
    add_bingo_protobuf_serializer<observe_bingo_events_res_t> (codecs);
    add_bingo_protobuf_serializer<stop_observing_bingo_events_req_t> (codecs);
    add_bingo_protobuf_serializer<stop_observing_bingo_events_res_t> (codecs);
    add_bingo_protobuf_serializer<player_joined_notify_t> (codecs);
    add_bingo_protobuf_serializer<game_started_notify_t> (codecs);
    add_bingo_protobuf_serializer<number_drawn_notify_t> (codecs);
    add_bingo_protobuf_serializer<state_notify_t> (codecs);
    add_bingo_protobuf_serializer<game_ended_notify_t> (codecs);
    add_bingo_protobuf_serializer<bingo_reward_announced_notify_t> (codecs);
    add_bingo_protobuf_serializer<bingo_reward_acquired_msg_t> (codecs);
}

} // namespace zlink::samples::bingo
