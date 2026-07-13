/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>
#include <zlink/codecs/protobuf.hpp>

namespace zlink::samples::bingo
{

struct bingo_protobuf_codecs_t
{
    template <typename TRegistrar> void register_framework_codecs (TRegistrar &registrar) const
    {
#define ZLINK_BINGO_REGISTER_PROTOBUF(type_name)                                                \
    zlink::framework_codecs::protobuf_codec_extension_t::register_payload_serializer<type_name> ( \
      registrar)
        ZLINK_BINGO_REGISTER_PROTOBUF (authenticate_req_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (authenticate_res_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (authenticate_player_req_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (authenticate_player_res_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (ensure_player_actor_req_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (ensure_player_actor_res_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (match_bingo_req_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (match_bingo_res_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (match_bingo_api_req_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (match_bingo_api_res_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (allocate_bingo_room_req_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (allocate_bingo_room_res_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (bingo_room_settings_payload_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (bingo_room_join_req_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (bingo_room_join_res_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (submit_bingo_card_req_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (submit_bingo_card_res_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (observe_bingo_events_req_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (observe_bingo_events_res_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (stop_observing_bingo_events_req_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (stop_observing_bingo_events_res_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (player_joined_notify_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (game_started_notify_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (number_drawn_notify_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (game_ended_notify_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (bingo_reward_announced_notify_t);
        ZLINK_BINGO_REGISTER_PROTOBUF (bingo_reward_acquired_msg_t);
#undef ZLINK_BINGO_REGISTER_PROTOBUF
    }
};

inline void use_default_bingo_codecs (framework::codec_options_builder_t codecs)
{
    codecs.use (bingo_protobuf_codecs_t{});
}

} // namespace zlink::samples::bingo
