/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <string>

namespace zlink::samples::bingo
{

struct bingo_match_reservation_t
{
    std::string room_id;
    std::string owner_play_node_rid;
    bool created_local_room = false;
};

class bingo_match_queue_t
{
  public:
    virtual ~bingo_match_queue_t () = default;

    virtual bingo_match_reservation_t reserve (const std::string &mode,
                                               const std::string &actor_id,
                                               const std::string &preferred_owner_node_rid,
                                               const std::string &new_room_id,
                                               int required_players) = 0;
};

} // namespace zlink::samples::bingo
