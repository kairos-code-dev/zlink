/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

namespace zlink::samples::gamequest
{

struct sample_names_t
{
    static constexpr const char *quest_owner_route_channel = "gamequest.quest.owner";
    static constexpr const char *stream_node = "gamequest.stream";
    static constexpr const char *mission_a_rid = "gamequest-mission-a";
    static constexpr const char *mission_b_rid = "gamequest-mission-b";
};

struct quest_ids_t
{
    static constexpr const char *first_hunt = "first-hunt";
    static constexpr const char *open_auction = "open-auction";
    static constexpr const char *herb_gathering = "herb-gathering";
    static constexpr const char *clear_tutorial = "clear-tutorial";
    static constexpr const char *visit_ruins = "visit-ruins";
};

struct quest_status_t
{
    static constexpr const char *active = "Active";
    static constexpr const char *reward_granted = "RewardGranted";
};

} // namespace zlink::samples::gamequest
