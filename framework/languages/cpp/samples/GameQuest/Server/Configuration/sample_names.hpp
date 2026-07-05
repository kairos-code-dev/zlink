/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <string>

namespace zlink::samples::gamequest
{

struct sample_names_t
{
    static constexpr const char *quest_owner_channel_prefix = "gamequest.quest.owner.";
    static constexpr const char *stream_node = "gamequest.stream";
    static constexpr const char *mission_a_rid = "gamequest-mission-a";
    static constexpr const char *mission_b_rid = "gamequest-mission-b";
    static constexpr const char *api_a_rid = "gamequest-api-a";
    static constexpr const char *api_b_rid = "gamequest-api-b";
};

inline std::string quest_owner_channel_for (const std::string &instance_id)
{
    return std::string (sample_names_t::quest_owner_channel_prefix) + instance_id;
}

} // namespace zlink::samples::gamequest
