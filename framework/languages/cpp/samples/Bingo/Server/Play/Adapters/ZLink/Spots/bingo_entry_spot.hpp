/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Actors/player_actor.hpp"
#include "../../../../Configuration/sample_names.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace zlink::samples::bingo
{

class bingo_entry_spot_t : public zlink::framework::entry_spot_t
{
  public:
    void configure (zlink::framework::entry_spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_packet<&bingo_entry_spot_t::match_bingo> ();
        context.handlers ().add_actor_packet<&bingo_entry_spot_t::observe_bingo_events> ();
    }

    zlink::framework::task_t<observe_bingo_events_res_t>
    observe_bingo_events (const player_actor_t &actor,
                          zlink::framework::spot_actor_request_context_t &,
                          const observe_bingo_events_req_t &request)
    {
        const auto display_name =
          actor.display_name.empty () ? actor.actor.actor_id : actor.display_name;
        const auto observer_rid = observer_room_rid (request.room_id, _context.node_rid ());
        auto created = _context.manager ().get_or_create_spot (
          sample_names_t::room_spot, observer_rid,
          to_stream_payload (bingo_room_settings_payload_t{
            "Bingo Observer " + std::string (_context.node_rid ().value ()),
            bingo_sample_modes_t::two_player,
            0,
            75,
            "Observer",
            request.room_id}));
        if (created.state == zlink::framework::spot_create_state_t::rejected) {
            throw std::runtime_error ("observer BingoRoom creation was rejected");
        }
        auto joined =
          co_await actor.context
            .join_spot (observer_rid,
                        to_stream_payload (
                          bingo_room_join_req_t{request.room_id,
                                                actor.actor.actor_id,
                                                display_name,
                                                true}))
            .async ();
        bingo_room_join_res_t ignored;
        from_stream_payload (joined.reply, ignored);
        co_return observe_bingo_events_res_t{
          true, std::string (joined.actor.node_rid ().value ())};
    }

    void configure (zlink::framework::spot_context_t &context)
    {
        zlink::framework::entry_spot_context_t entry_context (context);
        configure (entry_context);
    }

    zlink::framework::task_t<match_bingo_res_t>
    match_bingo (const player_actor_t &actor,
                 zlink::framework::spot_actor_request_context_t &,
                 const match_bingo_req_t &request)
    {
        const auto display_name =
          actor.display_name.empty () ? actor.actor.actor_id : actor.display_name;
        auto matched =
          co_await _context.outbound ()
            .request (sample_names_t::api_channel,
                      match_bingo_api_req_t{actor.actor.actor_id,
                                            display_name,
                                            request.mode,
                                            actor.actor.node_rid})
            .async<match_bingo_api_res_t> ();
        const auto spot_rid =
          zlink::framework::spot_rid_t::from_string (matched.room_id);
        auto joined =
          co_await actor.context
            .join_spot (spot_rid,
                        to_stream_payload (
                          bingo_room_join_req_t{matched.room_id,
                                                actor.actor.actor_id,
                                                display_name}))
            .async ();
        bingo_room_join_res_t reply;
        from_stream_payload (joined.reply, reply);
        co_return match_bingo_res_t{matched.room_id, reply.state, matched.room_owner_node_rid};
    }

    void onCreateActor (const player_actor_t &actor)
    {
        created_actor_ids.push_back (actor.actor.actor_id);
    }

    void on_actor_joined (const player_actor_t &actor)
    {
        joined_actor_ids.push_back (actor.actor.actor_id);
        if (!actor.destroy_after_entry_spot_join) {
            return;
        }
        (void) _context.destroyActor (actor_ref_for (actor),
                                      const_cast<player_actor_t &> (actor));
    }

    void onLeaveActor (const player_actor_t &actor)
    {
        joined_actor_ids.erase (
          std::remove (joined_actor_ids.begin (), joined_actor_ids.end (), actor.actor.actor_id),
          joined_actor_ids.end ());
    }

    void onDisconnectActor (const player_actor_t &actor) { actor.mark_disconnected (); }

    std::vector<std::string> created_actor_ids;
    std::vector<std::string> joined_actor_ids;

  private:
    static zlink::framework::spot_rid_t
    observer_room_rid (const std::string &room_id, zlink::framework::node_rid_t node_rid)
    {
        return zlink::framework::spot_rid_t::from_string (
          "observe:" + room_id + ":" + std::string (node_rid.value ()));
    }

    static zlink::framework::actor_ref_t actor_ref_for (const player_actor_t &actor)
    {
        return zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string (sample_names_t::room_spot_node),
          sample_names_t::player_actor_type, actor.actor.actor_id, actor.actor.generation);
    }

    zlink::framework::entry_spot_context_t _context;
};

} // namespace zlink::samples::bingo
