/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../Actors/player_actor.hpp"
#include "../../../../../Configuration/sample_names.hpp"
#include "../../../../../Configuration/sample_topology.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace zlink::samples::bingo
{

using namespace framework;
using framework::actor_ref_t;
using framework::message_t;

class bingo_entry_spot_t : public entry_spot_t
{
  public:
    bingo_entry_spot_t (sample_topology_t topology, service_provider_t services) :
        _topology (std::move (topology)),
        _services (services.create_scope (service_scope_kind_t::entry_spot))
    {
    }

    void configure (entry_spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_handler<&bingo_entry_spot_t::ensure_player_actor> ();
        context.handlers ().add_actor_request<&bingo_entry_spot_t::match_bingo> ();
        context.handlers ().add_actor_request<&bingo_entry_spot_t::observe_bingo_events> ();
    }

    task_t<observe_bingo_events_res_t>
    observe_bingo_events (const player_actor_t &actor,
                          spot_actor_request_context_t &context,
                          const observe_bingo_events_req_t &request);

    void configure (spot_context_t &context)
    {
        entry_spot_context_t entry_context (context);
        configure (entry_context);
    }

    task_t<match_bingo_res_t> match_bingo (const player_actor_t &actor,
                                           spot_actor_request_context_t &context,
                                           const match_bingo_req_t &request);

    task_t<ensure_player_actor_res_t>
    ensure_player_actor (const ensure_player_actor_req_t &request);

    void on_create_actor (player_actor_t &actor, const message_t &create_request)
    {
        const auto request = create_request.decode<ensure_player_actor_req_t> ();
        actor.display_name =
          request.display_name.empty () ? request.actor_id : request.display_name;
        created_actor_ids.push_back (actor.actor.actor_id);
    }

    task_t<void> on_actor_joined (const player_actor_t &actor)
    {
        joined_actor_ids.push_back (actor.actor.actor_id);
        if (!actor.destroy_after_entry_spot_join) {
            co_return;
        }
        const auto actor_id = actor.actor.actor_id;
        std::cout << "entry spot: actor destroy requested. actor=" << actor_id << std::endl;
        co_await _context.destroy_actor (const_cast<player_actor_t &> (actor));
        std::cout << "entry spot: actor destroy completed. actor=" << actor_id << std::endl;
    }

    task_t<void> on_leave_actor (const player_actor_t &actor)
    {
        joined_actor_ids.erase (
          std::remove (joined_actor_ids.begin (), joined_actor_ids.end (), actor.actor.actor_id),
          joined_actor_ids.end ());
        co_return;
    }

    task_t<void> on_disconnect_actor (const player_actor_t &actor)
    {
        actor.mark_disconnected ();
        co_return;
    }

    std::vector<std::string> created_actor_ids;
    std::vector<std::string> joined_actor_ids;

  private:
    static spot_rid_t observer_room_rid (const std::string &room_id, node_rid_t node_rid)
    {
        return spot_rid_t::from_string ("observe:" + room_id + ":"
                                        + std::string (node_rid.value ()));
    }

    static actor_ref_t actor_ref_for (const player_actor_t &actor)
    {
        return actor_ref_t (actor.actor.node_rid,
                            sample_names_t::player_actor_type, actor.actor.actor_id,
                            actor.actor.generation);
    }

    entry_spot_context_t _context;
    sample_topology_t _topology;
    service_scope_t _services;
};

} // namespace zlink::samples::bingo

#include "Handlers/ensure_player_actor_handler.hpp"
#include "Handlers/match_bingo_actor_handler.hpp"
#include "Handlers/observe_bingo_events_handler.hpp"
