/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../Configuration/sample_names.hpp"
#include "../../../../Configuration/sample_topology.hpp"
#include "../../../../../Shared/Contracts/messages.hpp"

namespace zlink::samples::tictactoe
{

using namespace framework;

class ensure_player_actor_handler_t
{
  public:
    using request_type = ensure_player_actor_req_t;
    using reply_type = ensure_player_actor_res_t;
    using dependency_types = dependency_list_t<sample_topology_t>;
    static constexpr const char *topic_name = "EnsurePlayerActor";

    explicit ensure_player_actor_handler_t (sample_topology_t &topology) : _topology (topology) {}

    ensure_player_actor_res_t handle (const ensure_player_actor_req_t &request)
    {
        return {request.actor_id,
                sample_names_t::actor_type,
                {node_rid_t::from_string (_topology.selected_play_node_rid ()),
                 request.actor_id, ++_generation}};
    }

  private:
    sample_topology_t &_topology;
    unsigned long long _generation = 0;
};

} // namespace zlink::samples::tictactoe
