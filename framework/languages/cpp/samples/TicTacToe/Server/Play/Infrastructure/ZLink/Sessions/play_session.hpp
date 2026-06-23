/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Handlers/authenticate_play_session_handler.hpp"

#include <optional>
#include <string>

namespace zlink::samples::tictactoe
{

class play_session_t final : public zlink::framework::packet_stream_session_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::session_actor_manager_t,
                                          authenticate_play_session_handler_t,
                                          zlink::framework::actor_gateway_t>;

    play_session_t (zlink::framework::session_actor_manager_t &actors,
                    authenticate_play_session_handler_t &authenticate,
                    zlink::framework::actor_gateway_t &gateway) :
        _actors (actors), _authenticate (authenticate), _gateway (gateway)
    {
    }

    zlink::framework::task_t<void> on_connected (zlink::framework::stream_t &) override
    {
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }

    zlink::framework::task_t<void> on_disconnected (zlink::framework::stream_t &) override
    {
        if (_bound_actor_id) {
            _gateway.unbind_session_stream (*_bound_actor_id);
            _actors.unbind_session (*_bound_actor_id);
            _bound_actor_id.reset ();
        }
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }

    zlink::framework::task_t<void> on_error (zlink::framework::stream_t &,
                                             const zlink::framework::stream_error_t &) override
    {
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }

    zlink::framework::task_t<void> on_packet (
      zlink::framework::stream_t &stream,
      const zlink::framework::stream_dispatch_context_t &dispatch,
      const zlink::message_t &payload) override
    {
        if (_authenticate.can_handle (dispatch)) {
            auto authenticated = co_await _authenticate.handle (_actors, stream, payload);
            _bound_actor_id = std::string (authenticated.actor_id ());
            _gateway.bind_session_stream (*_bound_actor_id, stream,
                                          zlink::framework::stream_codec_t::json);
            co_return;
        }

        auto actor = require_bound_actor (std::string ("dispatching packet '")
                                          + std::string (dispatch.packet_name ()) + "'");
        if (!actor) {
            co_return;
        }
        if (dispatch.can_reply ()) {
            auto reply = co_await actor.value ().relay_request (payload).async ();
            co_await stream.reply_packet (reply).async ();
            co_return;
        }
        co_await actor.value ().relay (payload).async ();
        co_return;
    }

  private:
    zlink::framework::result_t<zlink::framework::session_actor_t>
    require_bound_actor (const std::string &action) const
    {
        if (!_bound_actor_id) {
            return zlink::framework::result_t<zlink::framework::session_actor_t>::failure (
              zlink::framework::framework_error_kind_t::request_failed,
              "Client must authenticate before " + action + ".");
        }
        auto actor = _actors.find (*_bound_actor_id);
        if (!actor) {
            return zlink::framework::result_t<zlink::framework::session_actor_t>::failure (
              zlink::framework::framework_error_kind_t::actor_route_not_found,
              "Exactly one actor must be bound before " + action + ".");
        }
        return zlink::framework::result_t<zlink::framework::session_actor_t>::success (
          std::move (*actor));
    }

    zlink::framework::session_actor_manager_t &_actors;
    authenticate_play_session_handler_t &_authenticate;
    zlink::framework::actor_gateway_t &_gateway;
    std::optional<std::string> _bound_actor_id;
};

} // namespace zlink::samples::tictactoe
