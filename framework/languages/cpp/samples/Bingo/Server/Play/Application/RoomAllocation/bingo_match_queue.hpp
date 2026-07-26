/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <string>

namespace zlink::samples::bingo
{

struct bingo_match_reservation_t
{
    std::string room_id;
};

class bingo_match_queue_t
{
  public:
    virtual ~bingo_match_queue_t () = default;

    virtual bingo_match_reservation_t reserve (const std::string &mode,
                                               const std::string &actor_id,
                                               const std::string &new_room_id,
                                               int required_players) = 0;
};

} // namespace zlink::samples::bingo
