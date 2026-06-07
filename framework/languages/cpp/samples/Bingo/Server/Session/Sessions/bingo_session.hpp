/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Handlers/authenticate_session_handler.hpp"

#include <optional>
#include <string>

namespace zlink::samples::bingo
{

using zlink::framework::task_t;

class bingo_session_t final : public zlink::framework::packet_stream_session_t
{
  public:
    bingo_session_t (zlink::framework::session_actor_manager_t &actors,
                     authenticate_session_handler_t &authenticate) :
        _actors (actors), _authenticate (authenticate)
    {
    }

    task_t<void> on_connected (zlink::framework::stream_t &) override
    {
        return task_t<void> (zlink::framework::result_t<void>::success ());
    }

    task_t<void> on_disconnected (zlink::framework::stream_t &) override
    {
        if (_bound_actor_id) {
            _actors.unbind_session (*_bound_actor_id);
            _bound_actor_id.reset ();
        }
        return task_t<void> (zlink::framework::result_t<void>::success ());
    }

    task_t<void> on_error (zlink::framework::stream_t &,
                           const zlink::framework::stream_error_t &) override
    {
        return task_t<void> (zlink::framework::result_t<void>::success ());
    }

    task_t<void> on_packet (zlink::framework::stream_t &stream,
                            const zlink::framework::stream_header_t &header,
                            const zlink::message_t &payload) override
    {
        if (_authenticate.can_handle (header)) {
            auto authenticated = co_await _authenticate.handle (_actors, stream, header, payload);
            _bound_actor_id = std::string (authenticated.actor_id ());
            co_return;
        }

        auto actor = require_bound_actor (std::string ("relaying packet '")
                                          + std::string (header.packet_name ()) + "'");
        if (!actor) {
            return task_t<void> (zlink::framework::result_t<void>::failure (
              actor.error_kind (), actor.error ()->what ()));
        }
        co_await actor.value ().relay (header, payload).submit ();
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
    authenticate_session_handler_t &_authenticate;
    std::optional<std::string> _bound_actor_id;
};

} // namespace zlink::samples::bingo
