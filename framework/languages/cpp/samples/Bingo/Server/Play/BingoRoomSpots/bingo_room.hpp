/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Contracts/messages.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace zlink::samples::bingo
{

class bingo_room_t
{
  public:
    explicit bingo_room_t (std::string room_id) : _state{std::move (room_id)} {}

    player_joined_notify_t join (std::string actor_id, std::string display_name)
    {
        const bool host = _state.players.empty ();
        const int seat = static_cast<int> (_state.players.size ()) + 1;
        if (host) {
            _state.host_actor_id = actor_id;
        }
        _state.players.push_back ({actor_id, display_name, seat, host, make_card (seat), {}, 0});
        _state.can_start = _state.players.size () >= 2;
        return {_state.room_id, std::move (actor_id), std::move (display_name), seat, host, _state};
    }

    bingo_room_state_t start ()
    {
        if (!_state.can_start) {
            throw std::runtime_error ("bingo room needs at least two players");
        }
        _state.status = "playing";
        return _state;
    }

    number_drawn_notify_t draw (int number)
    {
        _state.drawn_numbers.push_back (number);
        _state.last_drawn_number = number;
        _state.draw_seq += 1;
        for (auto &player : _state.players) {
            for (std::size_t i = 0; i < player.card.size (); ++i) {
                if (player.card[i] == number) {
                    player.marks[i] = true;
                }
            }
            player.completed_lines = completed_lines (player.marks);
            if (player.completed_lines > 0
                && std::find (_state.winners.begin (), _state.winners.end (), player.actor_id)
                     == _state.winners.end ()) {
                _state.winners.push_back (player.actor_id);
            }
        }
        if (!_state.winners.empty ()) {
            _state.status = "ended";
        }
        return {_state.room_id, _state.draw_seq, number, _state};
    }

    bingo_room_state_t leave (const std::string &actor_id)
    {
        _state.players.erase (
          std::remove_if (_state.players.begin (), _state.players.end (),
                          [&] (const bingo_player_state_t &player) { return player.actor_id == actor_id; }),
          _state.players.end ());
        _state.can_start = _state.players.size () >= 2;
        return _state;
    }

    const bingo_room_state_t &snapshot () const noexcept { return _state; }

  private:
    static std::array<int, 25> make_card (int seat)
    {
        std::array<int, 25> card{};
        for (std::size_t i = 0; i < card.size (); ++i) {
            card[i] = static_cast<int> (i) + 1 + ((seat - 1) * 25);
        }
        return card;
    }

    static int completed_lines (const std::array<bool, 25> &marks)
    {
        int lines = 0;
        for (int row = 0; row < 5; ++row) {
            bool complete = true;
            for (int col = 0; col < 5; ++col) {
                complete = complete && marks[static_cast<std::size_t> (row * 5 + col)];
            }
            lines += complete ? 1 : 0;
        }
        for (int col = 0; col < 5; ++col) {
            bool complete = true;
            for (int row = 0; row < 5; ++row) {
                complete = complete && marks[static_cast<std::size_t> (row * 5 + col)];
            }
            lines += complete ? 1 : 0;
        }
        return lines;
    }

    bingo_room_state_t _state;
};

} // namespace zlink::samples::bingo
