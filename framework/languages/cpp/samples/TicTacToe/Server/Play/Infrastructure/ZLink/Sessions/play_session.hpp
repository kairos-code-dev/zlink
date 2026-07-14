/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "Handlers/authenticate_play_session_handler.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace zlink::samples::tictactoe
{

using namespace framework;
using framework::actor_ref_t;
using framework::message_t;

class play_session_t final : public packet_stream_session_t
{
  public:
    using dependency_types = dependency_list_t<session_actor_manager_t,
                                               authenticate_play_session_handler_t,
                                               actor_gateway_t>;

    play_session_t (session_actor_manager_t &actors,
                    authenticate_play_session_handler_t &authenticate,
                    actor_gateway_t &gateway) :
        _actors (actors), _authenticate (authenticate), _gateway (gateway)
    {
    }

    task_t<void> on_connected (stream_t &) override
    {
        return task_t<void> (result_t<void>::success ());
    }

    task_t<void> on_disconnected (stream_t &) override
    {
        if (_bound_actor_id) {
            if (auto actor = _actors.find (*_bound_actor_id)) {
                co_await actor->notify_disconnected ();
            }
            _gateway.unbind_session_stream (*_bound_actor_id);
            _actors.unbind_session (*_bound_actor_id);
            _bound_actor_id.reset ();
        }
        co_return;
    }

    task_t<void> on_error (stream_t &, const stream_error_t &) override
    {
        return task_t<void> (result_t<void>::success ());
    }

    task_t<void> on_packet (stream_t &stream,
                            const stream_dispatch_context_t &dispatch,
                            const zlink::message_t &payload) override
    {
        if (_authenticate.can_handle (dispatch)) {
            auto authenticated = co_await _authenticate.handle (_actors, stream, payload);
            _bound_actor_id = std::string (authenticated.actor_id ());
            _gateway.bind_session_stream (*_bound_actor_id, stream, stream_codec_t::json);
            co_return;
        }

        auto actor = require_bound_actor (std::string ("dispatching packet '")
                                          + std::string (dispatch.packet_name ()) + "'");
        if (!actor) {
            co_return;
        }
        if (dispatch.can_reply ()) {
            auto reply =
              co_await actor.value ().relay_request (std::string (dispatch.packet_name ()), payload)
                .async ();
            stream.reply_packet (reply).submit ();
            co_return;
        }
        co_await actor.value ().relay (std::string (dispatch.packet_name ()), payload);
        co_return;
    }

  private:
    result_t<session_actor_t> require_bound_actor (const std::string &action) const
    {
        if (!_bound_actor_id) {
            return result_t<session_actor_t>::failure (framework_error_kind_t::request_failed,
                                                       "Client must authenticate before " + action
                                                         + ".");
        }
        auto actor = _actors.find (*_bound_actor_id);
        if (!actor) {
            return result_t<session_actor_t>::failure (
              framework_error_kind_t::actor_route_not_found,
              "Exactly one actor must be bound before " + action + ".");
        }
        return result_t<session_actor_t>::success (std::move (*actor));
    }

    session_actor_manager_t &_actors;
    authenticate_play_session_handler_t &_authenticate;
    actor_gateway_t &_gateway;
    std::optional<std::string> _bound_actor_id;
};

} // namespace zlink::samples::tictactoe
