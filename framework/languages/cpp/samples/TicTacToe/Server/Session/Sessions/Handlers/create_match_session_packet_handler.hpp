/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Api/Handlers/create_match_handler.hpp"

#include <zlink/codec/json.hpp>
#include <zlink/framework.hpp>

namespace zlink::samples::tictactoe
{

class create_match_session_packet_handler_t
{
  public:
    explicit create_match_session_packet_handler_t (create_match_handler_t &create_match) :
        _create_match (create_match)
    {
    }

    bool can_handle (const zlink::framework::stream_header_t &header) const
    {
        return header.packet_name () == create_match_req_t::packet_name;
    }

    zlink::framework::task_t<void> handle (const zlink::framework::session_actor_t &actor,
                                           zlink::framework::stream_t &stream,
                                           const zlink::framework::stream_header_t &header,
                                           const zlink::message_t &payload)
    {
        auto request = payload.parse_json<create_match_req_t> ();
        request.owner_actor_id = std::string (actor.actor_id ());
        const auto reply = _create_match.handle (request);
        co_await stream.reply_packet (header, zlink::message_t::from_json (reply)).submit ();
        co_return;
    }

  private:
    create_match_handler_t &_create_match;
};

} // namespace zlink::samples::tictactoe
