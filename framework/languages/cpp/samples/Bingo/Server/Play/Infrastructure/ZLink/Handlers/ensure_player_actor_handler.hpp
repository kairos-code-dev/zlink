/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../Configuration/sample_names.hpp"
#include "../../../../Configuration/sample_topology.hpp"
#include "../../../../../Shared/Contracts/messages.hpp"

#include <stdexcept>

namespace zlink::samples::bingo
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
        const auto node_rid = _topology.selected_play_node_rid ();
        if (!request.preferred_actor_node_rid.empty ()
            && request.preferred_actor_node_rid != node_rid) {
            throw std::runtime_error ("EnsurePlayerActor reached the wrong Play node.");
        }
        return {request.actor_id,
                sample_names_t::player_actor_type,
                {node_rid_t::from_string (node_rid), request.actor_id, ++_generation}};
    }

  private:
    sample_topology_t &_topology;
    unsigned long long _generation = 0;
};

} // namespace zlink::samples::bingo
