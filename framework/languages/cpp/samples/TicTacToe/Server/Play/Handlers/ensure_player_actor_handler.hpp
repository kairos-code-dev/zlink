/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Configuration/sample_names.hpp"
#include "../../../Shared/Contracts/messages.hpp"

namespace zlink::samples::tictactoe
{

class ensure_player_actor_handler_t
{
  public:
    using request_type = ensure_player_actor_req_t;
    using reply_type = ensure_player_actor_res_t;
    static constexpr const char *topic_name = "EnsurePlayerActor";

    ensure_player_actor_res_t handle (const ensure_player_actor_req_t &request)
    {
        return {
          request.actor_id, sample_names_t::actor_type, {{}, request.actor_id, ++_generation}};
    }

  private:
    unsigned long long _generation = 0;
};

} // namespace zlink::samples::tictactoe
