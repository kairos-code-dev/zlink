/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../Actors/player_actor.hpp"
#include "../../../../../Configuration/sample_names.hpp"
#include "../../../../Domain/Bingo/bingo_room_game.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

namespace zlink::samples::bingo
{

using namespace framework;
using framework::actor_ref_t;
using framework::message_t;

class bingo_room_spot_t;

class bingo_room_draw_timer_handler_t
{
  public:
    task_t<void> handle (bingo_room_spot_t &spot, const timer_tick_t &tick) const;
};

class bingo_room_spot_t : public spot_t
{
  public:
    bingo_room_spot_t () = default;

    explicit bingo_room_spot_t (std::string room_id) : _game (std::move (room_id)) {}

    void configure (spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_request<&bingo_room_spot_t::submit_card> ();
        context.handlers ().add_actor_request<&bingo_room_spot_t::observe_events> ();
        context.handlers ().add_actor_request<&bingo_room_spot_t::stop_observing_events> ();
        context.handlers ().add_subscribe<&bingo_room_spot_t::on_reward_acquired> (
          sample_names_t::reward_topic);
    }

    spot_create_response_t on_create (const message_t &request)
    {
        if (!request.empty ()) {
            auto settings = request.decode<bingo_room_settings_payload_t> ();
            _is_observer = settings.purpose == "Observer";
            _observed_room_id = settings.observed_room_id;
        }
        return spot_create_response_t::accept ();
    }

    void on_initialize ()
    {
        using namespace std::chrono_literals;
        _draw_timer =
          _context.add_timer<bingo_room_draw_timer_handler_t> ("bingo-draw", 200ms);
    }

    void on_closing () { _draw_timer.cancel (); }

    spot_actor_join_response_t on_actor_join (std::string_view actor_id,
                                              const message_t &request_message)
    {
        auto request = request_message.decode<bingo_room_join_req_t> ();
        const auto joined_actor_id =
          actor_id.empty () ? request.actor_id : std::string (actor_id);
        if (request.observe_only) {
            if (!_is_observer || request.room_id != _observed_room_id) {
                throw std::runtime_error ("observe-only actor can join only its observer room");
            }
            _pending_joins[joined_actor_id] = request;
            return spot_actor_join_response_t::accept (bingo_room_join_res_t{
              bingo_room_state_t{request.room_id, bingo_room_status_t::running}});
        }
        if (_is_observer) {
            throw std::runtime_error ("player actor cannot join an observer room");
        }
        auto projected = _game;
        projected.set_room_id_if_empty (request.room_id);
        projected.join (joined_actor_id, request.display_name);
        _pending_joins[joined_actor_id] = request;
        return spot_actor_join_response_t::accept (bingo_room_join_res_t{projected.snapshot ()});
    }

    observe_bingo_events_res_t observe_events (
      const player_actor_t &actor,
      const spot_actor_request_context_t &context,
      const observe_bingo_events_req_t &request);

    task_t<stop_observing_bingo_events_res_t> stop_observing_events (
      const player_actor_t &actor,
      const spot_actor_request_context_t &context,
      const stop_observing_bingo_events_req_t &request);

    task_t<submit_bingo_card_res_t> submit_card (
      const player_actor_t &actor,
      const spot_actor_request_context_t &context,
      const submit_bingo_card_req_t &request);

    task_t<void> on_actor_joined (const player_actor_t &actor)
    {
        const auto pending = _pending_joins.find (actor.actor.actor_id);
        if (pending == _pending_joins.end ()) {
            throw std::runtime_error ("accepted bingo actor admission is missing");
        }
        const auto request = pending->second;
        _game.set_room_id_if_empty (request.room_id);
        if (request.observe_only) {
            _pending_joins.erase (pending);
            observers[actor.actor.actor_id] = const_cast<player_actor_t *> (&actor);
        } else {
            get_player_record_res_t record;
            try {
                record = co_await _context.outbound ()
                           .request (sample_names_t::api_channel,
                                     get_player_record_req_t{actor.actor.actor_id})
                           .yield<get_player_record_res_t> ();
            }
            catch (...) {
                _pending_joins.erase (actor.actor.actor_id);
                throw;
            }
            const auto resumed = _pending_joins.find (actor.actor.actor_id);
            if (resumed == _pending_joins.end () || resumed->second.room_id != request.room_id
                || !_game.can_accept_player ()) {
                _pending_joins.erase (actor.actor.actor_id);
                (void) co_await _context.leave_actor (
                  actor_ref_for (actor), const_cast<player_actor_t &> (actor));
                co_return;
            }
            _pending_joins.erase (resumed);
            const auto display_name = actor.display_name.empty () ? request.display_name
                                                                  : actor.display_name;
            actors[actor.actor.actor_id] = const_cast<player_actor_t *> (&actor);
            const auto joined = _game.join (actor.actor.actor_id, display_name,
                                            record.wins, record.losses);
            send_to_players (joined.player_joined, actor.actor.actor_id);
            if (joined.game_started) {
                send_to_players (*joined.game_started, actor.actor.actor_id);
                actor.push (*joined.game_started);
            }
            std::cout << "bingo player record loaded actor=" << record.actor_id
                      << " wins=" << record.wins << " losses=" << record.losses << '\n';
        }
        co_return;
    }

    task_t<void> on_leave_actor (const player_actor_t &actor)
    {
        const auto player = actors.find (actor.actor.actor_id);
        if (player != actors.end ()) {
            const auto final_state = _game.snapshot ();
            const auto won = std::find (final_state.winners.begin (), final_state.winners.end (),
                                        actor.actor.actor_id)
                             != final_state.winners.end ();
            auto report_pending =
              _context.outbound ()
                .request (sample_names_t::api_channel,
                          report_bingo_result_req_t{final_state.room_id,
                                                    actor.actor.actor_id,
                                                    won,
                                                    final_state.draw_seq})
                .yield<report_bingo_result_res_t> ();
            const auto record = co_await report_pending;
            std::cout << "bingo result reported room=" << final_state.room_id
                      << " actor=" << record.actor_id << " won=" << (won ? "true" : "false")
                      << " wins=" << record.wins << " losses=" << record.losses << '\n';
        }
        actors.erase (actor.actor.actor_id);
        observers.erase (actor.actor.actor_id);
        _game.leave (actor.actor.actor_id);
        if (actors.empty () && observers.empty ()) {
            (void) co_await _context.close ();
        }
        co_return;
    }

    task_t<void> on_disconnect_actor (const player_actor_t &actor)
    {
        actor.mark_disconnected ();
        co_return;
    }

    const bingo_room_state_t &snapshot () const noexcept { return _game.snapshot (); }

    void record_observer_returned_to_entry_spot (const player_actor_t &actor) const
    {
        std::cout << "observer returned to entry spot actor=" << actor.actor.actor_id << '\n';
    }

  private:
    friend class bingo_room_draw_timer_handler_t;

    task_t<void> handle_draw_tick (const timer_tick_t &);

    void publish_reward (const number_drawn_notify_t &drawn)
    {
        if (drawn.state.winners.empty ()) {
            return;
        }
        const auto reward_event = bingo_reward_acquired_event_t{
            drawn.state.room_id,
            drawn.state.winners.front (),
            drawn.state.draw_seq,
            bingo_reward_items_t::golden_dauber_id,
            bingo_reward_items_t::golden_dauber_name,
            bingo_reward_items_t::legendary_rarity
        };
        _context.publish (sample_names_t::reward_topic, reward_event).submit ();
    }

    template <typename TNotify>
    void send_to_players (const TNotify &notify, const std::string &excluded_actor_id = {})
    {
        for (auto &[actor_id, actor] : actors) {
            if (!excluded_actor_id.empty () && actor_id == excluded_actor_id) {
                continue;
            }
            actor->push (notify);
        }
    }

    task_t<void> on_reward_acquired (const bingo_reward_acquired_event_t &event);

    task_t<void> leave_finished_actors ()
    {
        if (cleanup_started || snapshot ().winners.empty ()) {
            co_return;
        }
        cleanup_started = true;
        std::vector<player_actor_t *> leaving;
        for (auto &[_, actor] : actors) {
            leaving.push_back (actor);
        }
        for (auto *actor : leaving) {
            actor->mark_for_destroy_after_room_leave ();
            const auto before = actor_ref_for (*actor);
            (void) co_await _context.leave_actor (before, *actor);
        }
        co_return;
    }

    static actor_ref_t actor_ref_for (const player_actor_t &actor)
    {
        return actor_ref_t (actor.actor.node_rid,
                            sample_names_t::player_actor_type, actor.actor.actor_id,
                            actor.actor.generation);
    }

    spot_context_t _context;
    bingo_room_game_t _game;
    std::map<std::string, player_actor_t *> actors;
    std::map<std::string, player_actor_t *> observers;
    std::map<std::string, bingo_room_join_req_t> _pending_joins;
    bool _is_observer = false;
    std::string _observed_room_id;
    bool cleanup_started = false;
    framework::timer_t _draw_timer;
};

} // namespace zlink::samples::bingo

#include "Handlers/bingo_reward_acquired_event_handler.hpp"
#include "Handlers/bingo_room_draw_timer_handler.hpp"
#include "Handlers/observe_bingo_events_handler.hpp"
#include "Handlers/stop_observing_bingo_events_handler.hpp"
#include "Handlers/submit_bingo_card_handler.hpp"
