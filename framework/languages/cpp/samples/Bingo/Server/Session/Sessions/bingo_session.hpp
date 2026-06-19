/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Configuration/sample_names.hpp"
#include "../../Configuration/sample_topology.hpp"
#include "Handlers/authenticate_session_handler.hpp"

#include <optional>
#include <string>

namespace zlink::samples::bingo
{

using zlink::framework::task_t;

class bingo_session_t final : public zlink::framework::packet_stream_session_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::session_actor_manager_t,
                                          zlink::framework::channel_client_t,
                                          authenticate_session_handler_t,
                                          zlink::framework::actor_gateway_t,
                                          sample_topology_t>;

    bingo_session_t (zlink::framework::session_actor_manager_t &actors,
                     zlink::framework::channel_client_t &client,
                     authenticate_session_handler_t &authenticate,
                     zlink::framework::actor_gateway_t &gateway,
                     sample_topology_t &topology) :
        _actors (actors), _client (client), _authenticate (authenticate), _gateway (gateway),
        _topology (topology)
    {
    }

    task_t<void> on_connected (zlink::framework::stream_t &) override
    {
        return task_t<void> (zlink::framework::result_t<void>::success ());
    }

    task_t<void> on_disconnected (zlink::framework::stream_t &) override
    {
        if (_bound_actor_id) {
            _gateway.unbind_session_stream (*_bound_actor_id);
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
            _gateway.bind_session_stream (*_bound_actor_id, stream,
                                          zlink::framework::stream_codec_t::protobuf);
            co_return;
        }

        auto actor = require_bound_actor (std::string ("relaying packet '")
                                          + std::string (header.packet_name ()) + "'");
        if (!actor) {
            co_return;
        }
        if (header.kind () == zlink::framework::stream_message_kind_t::request) {
            auto reply = co_await relay_remote_actor_packet (actor.value (), header, payload);
            co_await update_bound_actor_ref (reply).async ();
            if (!reply.has_reply) {
                throw zlink::framework::framework_exception_t (
                  zlink::framework::framework_error_kind_t::request_protocol_error,
                  "remote actor packet request did not return a reply");
            }
            co_await stream.reply_packet (header, zlink::message_t::from (reply.reply_payload))
              .async ();
            co_return;
        }
        auto reply = co_await relay_remote_actor_packet (actor.value (), header, payload);
        co_await update_bound_actor_ref (reply).async ();
        co_return;
    }

  private:
    zlink::framework::request_call_t<zlink::framework::session_actor_t>
    update_bound_actor_ref (const remote_actor_packet_res_t &reply)
    {
        if (!reply.actor_ref_present) {
            return zlink::framework::request_call_t<zlink::framework::session_actor_t> (
              zlink::framework::result_t<zlink::framework::session_actor_t>::success (
                zlink::framework::session_actor_t ()));
        }
        return _actors.bind (zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string (reply.actor_node_rid),
          reply.actor_type,
          reply.actor_id,
          reply.actor_generation));
    }

    zlink::framework::task_t<remote_actor_packet_res_t>
    relay_remote_actor_packet (const zlink::framework::session_actor_t &actor,
                               const zlink::framework::stream_header_t &header,
                               const zlink::message_t &payload)
    {
        auto request_seq = header.request_seq ();
        auto response = co_await _client
                          .request (
                            sample_names_t::play_channel,
                            remote_actor_packet_req_t{
                              std::string (actor.ref ().node_rid ().value ()),
                              std::string (actor.ref ().actor_type ()),
                              std::string (actor.ref ().actor_id ()),
                              actor.ref ().generation (),
                              static_cast<int> (header.kind ()),
                              static_cast<int> (header.codec ()),
                              static_cast<int> (header.flags ()),
                              request_seq.has_value (),
                              request_seq.value_or (0),
                              std::string (header.packet_name ()),
                              sample_names_t::play_route_channel,
                              _topology.selected_session_route_rid (),
                              header.metadata ().values (),
                              payload.to_bytes ()})
                          .async<remote_actor_packet_res_t> ();
        co_return zlink::framework::result_t<remote_actor_packet_res_t>::success (
          std::move (response));
    }

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
    zlink::framework::channel_client_t &_client;
    authenticate_session_handler_t &_authenticate;
    zlink::framework::actor_gateway_t &_gateway;
    sample_topology_t &_topology;
    std::optional<std::string> _bound_actor_id;
};

} // namespace zlink::samples::bingo
