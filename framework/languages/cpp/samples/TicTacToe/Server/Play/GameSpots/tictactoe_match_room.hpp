/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Contracts/messages.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace zlink::samples::tictactoe
{

class tictactoe_match_room_t
{
  public:
    explicit tictactoe_match_room_t (std::string match_id) :
        _state{.match_id = std::move (match_id)}
    {
    }

    create_match_res_t create (const create_match_req_t &request)
    {
        if (request.owner_actor_id.empty ()) {
            throw std::runtime_error ("owner actor id must not be empty");
        }
        _state.x_actor_id = request.owner_actor_id;
        _state.turn_actor_id = request.owner_actor_id;
        return {_state.match_id, request.owner_actor_id, ""};
    }

    join_match_res_t join (const join_match_req_t &request)
    {
        if (request.actor_id == _state.x_actor_id) {
            return {_state.match_id, request.actor_id, "X", _state};
        }
        if (_state.o_actor_id.empty ()) {
            _state.o_actor_id = request.actor_id;
            _state.status = "playing";
            return {_state.match_id, request.actor_id, "O", _state};
        }
        throw std::runtime_error ("match already has two players");
    }

    tictactoe_state_t place (const place_mark_req_t &request)
    {
        if (_state.status != "playing") {
            throw std::runtime_error ("match is not playing");
        }
        if (request.actor_id != _state.turn_actor_id) {
            throw std::runtime_error ("not actor turn");
        }
        if (request.cell < 0 || request.cell >= 9
            || _state.board[static_cast<std::size_t> (request.cell)] != '.') {
            throw std::runtime_error ("invalid move");
        }

        const char mark = request.actor_id == _state.x_actor_id ? 'X' : 'O';
        _state.board[static_cast<std::size_t> (request.cell)] = mark;
        _state.last_move_cell = request.cell;
        _state.last_move_actor_id = request.actor_id;
        if (has_winner (mark)) {
            _state.status = "ended";
            _state.winner_actor_id = request.actor_id;
        } else if (_state.board.find ('.') == std::string::npos) {
            _state.status = "ended";
            _state.draw = true;
        } else {
            _state.turn_actor_id =
              request.actor_id == _state.x_actor_id ? _state.o_actor_id : _state.x_actor_id;
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
