/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../Shared/Contracts/messages.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace zlink::samples::tictactoe
{

class tictactoe_match_t
{
  public:
    explicit tictactoe_match_t (std::string room_id) :
        _state{.room_id = std::move (room_id)}
    {
    }

    create_game_res_t create (const create_game_req_t &request)
    {
        if (request.game_name.empty ()) {
            throw std::runtime_error ("game name must not be empty");
        }
        return {_state.room_id, "", request.game_name};
    }

    join_game_res_t join (const std::string &actor_id, const join_game_req_t &request)
    {
        if (request.room_id != _state.room_id) {
            throw std::runtime_error ("room id mismatch");
        }
        if (actor_id.empty ()) {
            throw std::runtime_error ("actor id must not be empty");
        }
        if (_state.x_actor_id.empty ()) {
            _state.x_actor_id = actor_id;
            _state.next_turn = actor_id;
            _state.status = "WaitingForPlayers";
            return {_state};
        }
        if (actor_id == _state.x_actor_id) {
            return {_state};
        }
        if (_state.o_actor_id.empty ()) {
            _state.o_actor_id = actor_id;
            _state.status = "InProgress";
            return {_state};
        }
        if (actor_id == _state.o_actor_id) {
            return {_state};
        }
        throw std::runtime_error ("match already has two players");
    }

    tictactoe_state_t place (const std::string &actor_id, const place_mark_req_t &request)
    {
        if (_state.status != "InProgress") {
            throw std::runtime_error ("match is not playing");
        }
        if (actor_id != _state.next_turn) {
            throw std::runtime_error ("not actor turn");
        }
        if (request.cell < 0 || request.cell >= 9
            || _state.board[static_cast<std::size_t> (request.cell)] != '.') {
            throw std::runtime_error ("invalid move");
        }

        const char mark = actor_id == _state.x_actor_id ? 'X' : 'O';
        _state.board[static_cast<std::size_t> (request.cell)] = mark;
        _state.last_move_cell = request.cell;
        _state.last_move_actor_id = actor_id;
        if (has_winner (mark)) {
            _state.status = "Won";
            _state.winner = actor_id;
        } else if (_state.board.find ('.') == std::string::npos) {
            _state.status = "Draw";
            _state.draw = true;
        } else {
            _state.next_turn =
              actor_id == _state.x_actor_id ? _state.o_actor_id : _state.x_actor_id;
        }
        return _state;
    }

    const tictactoe_state_t &snapshot () const noexcept { return _state; }

  private:
    bool has_winner (char mark) const
    {
        static constexpr std::array<std::array<int, 3>, 8> lines{{
          {{0, 1, 2}},
          {{3, 4, 5}},
          {{6, 7, 8}},
          {{0, 3, 6}},
          {{1, 4, 7}},
          {{2, 5, 8}},
          {{0, 4, 8}},
          {{2, 4, 6}},
        }};
        return std::any_of (lines.begin (), lines.end (), [&] (const auto &line) {
            return _state.board[static_cast<std::size_t> (line[0])] == mark
                   && _state.board[static_cast<std::size_t> (line[1])] == mark
                   && _state.board[static_cast<std::size_t> (line[2])] == mark;
        });
    }

    tictactoe_state_t _state;
};

} // namespace zlink::samples::tictactoe
