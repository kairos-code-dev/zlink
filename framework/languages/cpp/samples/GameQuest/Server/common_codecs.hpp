/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::gamequest
{

inline void add_gamequest_json_codecs (framework::codec_options_builder_t codecs)
{
    codecs.add_json<enter_area_req_t,
                    kill_monster_req_t,
                    collect_item_req_t,
                    complete_mission_req_t,
                    unlock_feature_req_t,
                    subscribe_quest_req_t,
                    subscribe_quest_res_t,
                    get_quest_progress_req_t,
                    get_quest_progress_res_t,
                    sync_quest_progress_req_t,
                    sync_quest_progress_res_t,
                    get_gameplay_snapshot_req_t,
                    get_gameplay_snapshot_res_t,
                    delete_quest_projection_req_t,
                    rebuild_quest_projection_req_t,
                    game_quest_server_assert_req_t,
                    game_quest_server_assert_res_t,
                    event_res_t,
                    quest_progress_t> ();
}

} // namespace zlink::samples::gamequest
