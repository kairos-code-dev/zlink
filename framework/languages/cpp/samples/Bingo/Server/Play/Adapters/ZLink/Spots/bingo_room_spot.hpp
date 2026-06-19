/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Actors/player_actor.hpp"
#include "../../../../Configuration/sample_names.hpp"
#include "../../../Domain/Bingo/bingo_room_game.hpp"

#include <zlink/framework.hpp>

#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace zlink::samples::bingo
{

class bingo_room_spot_t : public zlink::framework::spot_t, public bingo_room_game_t
{
  public:
    bingo_room_spot_t () = default;

    explicit bingo_room_spot_t (std::string room_id) :
        bingo_room_game_t (std::move (room_id))
    {
    }

    void configure (zlink::framework::spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_packet<&bingo_room_spot_t::submit_card> ();
        context.handlers ().add_actor_packet<&bingo_room_spot_t::observe_events> ();
        context.handlers ().add_actor_packet<&bingo_room_spot_t::stop_observing_events> ();
        context.handlers ().add_subscribe<&bingo_room_spot_t::on_reward_acquired> (
          sample_names_t::reward_topic);
    }

    zlink::framework::spot_create_response_t on_create (const zlink::message_t &request)
    {
        if (request.valid () && request.size () > 0) {
            bingo_room_settings_payload_t settings;
            from_stream_payload (request, settings);
            _is_observer = settings.purpose == "Observer";
            _observed_room_id = settings.observed_room_id;
        }
        return zlink::framework::spot_create_response_t::accept ();
    }

    zlink::framework::spot_actor_join_response_t
    on_actor_join (const player_actor_t &actor, const zlink::message_t &request_message)
    {
        bingo_room_join_req_t request;
        from_stream_payload (request_message, request);
        const auto actor_id =
          actor.actor.actor_id.empty () ? request.actor_id : actor.actor.actor_id;
        const auto display_name =
          actor.display_name.empty () ? request.display_name : actor.display_name;
        set_room_id_if_empty (request.room_id);
        if (request.observe_only) {
            if (!_is_observer || request.room_id != _observed_room_id) {
                throw std::runtime_error ("observe-only actor can join only its observer room");
            }
            observers[actor_id] = const_cast<player_actor_t *> (&actor);
            return zlink::framework::spot_actor_join_response_t::accept (
              to_stream_payload (bingo_room_join_res_t{
                bingo_room_state_t{request.room_id, bingo_room_status_t::running}}));
        }
        if (_is_observer) {
            throw std::runtime_error ("player actor cannot join an observer room");
        }
        join (actor_id, display_name);
        std::cerr << "bingo room: actor joined. room=" << snapshot ().room_id
                  << ", actor=" << actor_id
                  << ", requestActor=" << request.actor_id
                  << ", count=" << snapshot ().players.size ()
                  << ", status=" << snapshot ().status
                  << ", nodeRid=" << _context.node_rid ().value () << '\n';
        return zlink::framework::spot_actor_join_response_t::accept (
          to_stream_payload (bingo_room_join_res_t{snapshot ()}));
    }

    observe_bingo_events_res_t observe_events (
      const player_actor_t &actor,
      const zlink::framework::spot_actor_request_context_t &,
      const observe_bingo_events_req_t &request)
    {
        set_room_id_if_empty (request.room_id);
        observers[actor.actor.actor_id] = const_cast<player_actor_t *> (&actor);
        return {true, actor.actor.node_rid};
    }

    stop_observing_bingo_events_res_t stop_observing_events (
      const player_actor_t &actor,
      const zlink::framework::spot_actor_request_context_t &,
      const stop_observing_bingo_events_req_t &request)
    {
        (void) request;
        observers.erase (actor.actor.actor_id);
        (void) _context.leaveActor (actor_ref_for (actor), const_cast<player_actor_t &> (actor));
        return {true, actor.actor.node_rid};
    }

    submit_bingo_card_res_t
    submit_card (const player_actor_t &actor,
                 const zlink::framework::spot_actor_request_context_t &context,
                 const submit_bingo_card_req_t &request)
    {
        if (context.packet_name.empty ()) {
            throw std::runtime_error ("packet name is required");
        }
        (void) bingo_room_game_t::submit_card (actor.actor.actor_id, request.card);
        if (should_draw ()) {
            while (const auto drawn = draw_next ()) {
                if (!drawn->state.winners.empty ()) {
                    publish_reward (*drawn);
                    leave_finished_actors ();
                    break;
                }
            }
        }
        return submit_bingo_card_res_t{snapshot ()};
    }

    void on_actor_joined (const player_actor_t &actor)
    {
        actors[actor.actor.actor_id] = const_cast<player_actor_t *> (&actor);
        const auto &state = snapshot ();
        auto joined = std::find_if (
          state.players.begin (), state.players.end (),
          [&actor] (const bingo_player_state_t &player) {
              return player.actor_id == actor.actor.actor_id;
          });
        if (joined != state.players.end ()) {
            player_joined_notify_t notify{state.room_id,
                                          joined->actor_id,
                                          joined->display_name,
                                          joined->seat,
                                          joined->is_host,
                                          state};
            send_to_players (notify, joined->actor_id);
        }
        if (state.players.size () == 2) {
            game_started_notify_t started{state};
            send_to_players (started);
        }
    }

    void onLeaveActor (const player_actor_t &actor)
    {
        actors.erase (actor.actor.actor_id);
        observers.erase (actor.actor.actor_id);
        leave (actor.actor.actor_id);
    }

    void onDisconnectActor (const player_actor_t &actor) { actor.mark_disconnected (); }

  private:
    void publish_reward (const number_drawn_notify_t &drawn)
    {
        if (drawn.state.winners.empty ()) {
            return;
        }
        auto published =
          _context
            .publish (sample_names_t::reward_topic,
                      bingo_reward_acquired_event_t{drawn.state.room_id,
                                                    drawn.state.winners.front (),
                                                    drawn.state.draw_seq,
                                                    bingo_reward_items_t::golden_dauber_id,
                                                    bingo_reward_items_t::golden_dauber_name,
                                                    bingo_reward_items_t::legendary_rarity})
            .async ()
            .result ();
        std::cerr << "bingo reward: published. room=" << drawn.state.room_id
                  << ", actor=" << drawn.state.winners.front ()
                  << ", item=" << bingo_reward_items_t::golden_dauber_id
                  << ", nodeRid=" << _context.node_rid ().value () << '\n';
        (void) published;
    }

    template <typename TNotify>
    void send_to_players (const TNotify &notify, const std::string &excluded_actor_id = {})
    {
        for (auto &[actor_id, actor] : actors) {
            if (!excluded_actor_id.empty () && actor_id == excluded_actor_id) {
                continue;
            }
            auto sent = actor->context.bound_session ().send (notify).async ().result ();
            if (!sent) {
                std::cerr << "bingo notify: send failed. actor=" << actor_id
                          << ", packet=" << TNotify::packet_name
                          << ", error="
                          << (sent.error () ? sent.error ()->what () : "unknown") << '\n';
            }
            (void) sent;
        }
    }

    void on_reward_acquired (const bingo_reward_acquired_event_t &event)
    {
        if (!_is_observer || event.room_id != _observed_room_id) {
            std::cerr << "bingo reward: ignored. room=" << event.room_id
                      << ", actor=" << event.actor_id
                      << ", item=" << event.item_id
                      << ", observer=" << (_is_observer ? "true" : "false")
                      << ", observedRoom=" << _observed_room_id
                      << ", nodeRid=" << _context.node_rid ().value () << '\n';
            return;
        }
        for (auto &[_, actor] : observers) {
            std::cerr << "bingo reward: announcing. room=" << event.room_id
                      << ", actor=" << event.actor_id
                      << ", item=" << event.item_id
                      << ", observer=" << actor->actor.actor_id
                      << ", nodeRid=" << _context.node_rid ().value () << '\n';
            auto sent =
              actor->context.bound_session ()
                .send (bingo_reward_announced_notify_t{event.room_id,
                                                       event.actor_id,
                                                       event.draw_seq,
                                                       event.item_id,
                                                       event.item_name,
                                                       event.rarity,
                                                       std::string (_context.node_rid ().value ())})
                .async ()
                .result ();
            if (!sent) {
                std::cerr << "bingo reward: announce send failed. room=" << event.room_id
                          << ", observer=" << actor->actor.actor_id
                          << ", error="
                          << (sent.error () ? sent.error ()->what () : "unknown") << '\n';
            } else {
                std::cerr << "bingo reward: announce sent. room=" << event.room_id
                          << ", observer=" << actor->actor.actor_id << '\n';
            }
            (void) sent;
        }
    }

    void leave_finished_actors ()
    {
        if (cleanup_started || snapshot ().winners.empty ()) {
            return;
        }
        cleanup_started = true;
        std::vector<player_actor_t *> leaving;
        for (auto &[_, actor] : actors) {
            leaving.push_back (actor);
        }
        for (auto *actor : leaving) {
            actor->mark_for_destroy_after_room_leave ();
            (void) _context.leaveActor (actor_ref_for (*actor), *actor);
        }
    }

    static zlink::framework::actor_ref_t actor_ref_for (const player_actor_t &actor)
    {
        return zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string (actor.actor.node_rid),
          sample_names_t::player_actor_type, actor.actor.actor_id, actor.actor.generation);
    }

    zlink::framework::spot_context_t _context;
    std::map<std::string, player_actor_t *> actors;
    std::map<std::string, player_actor_t *> observers;
    bool _is_observer = false;
    std::string _observed_room_id;
    bool cleanup_started = false;
};

} // namespace zlink::samples::bingo
