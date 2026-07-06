/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/sample_names.hpp"
#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <iostream>
#include <map>
#include <mutex>
#include <string>

namespace zlink::samples::deliverydispatch
{

using namespace framework;

class courier_session_t final : public packet_stream_session_t
{
  public:
    using dependency_types =
      dependency_list_t<channel_client_t, session_actor_manager_t, actor_gateway_t>;

    courier_session_t (channel_client_t &channels,
                       session_actor_manager_t &actors,
                       actor_gateway_t &gateway) :
        _channels (channels), _actors (actors), _gateway (gateway)
    {
    }

    task_t<void> on_connected (stream_t &) override { co_return; }

    task_t<void> on_disconnected (stream_t &) override
    {
        for (const auto &[actor_id, _] : _bound_actors) {
            _gateway.unbind_session_stream (actor_id);
            _actors.unbind_session (actor_id);
        }
        _bound_actors.clear ();
        co_return;
    }

    task_t<void> on_error (stream_t &, const stream_error_t &) override { co_return; }

    task_t<void> on_packet (stream_t &stream,
                            const stream_dispatch_context_t &dispatch,
                            const zlink::message_t &payload) override
    {
        std::cerr << "deliverydispatch courier-session: dispatch packet="
                  << dispatch.packet_name () << "\n";
        if (dispatch.packet_name () == bind_courier_session_req_t::packet_name) {
            const auto request = payload.parse_json<bind_courier_session_req_t> ();
            auto bound = co_await request_courier_gateway<bind_courier_req_t, bind_courier_res_t> (
              bind_courier_req_t{request.courier_id, "courier-session:" + request.courier_id});
            auto actor =
              co_await _actors.bind_or_get (bound.actor.to_actor_ref (sample_names_t::courier_actor_type)).async ();
            const auto actor_id = std::string (actor.actor_id ());
            _gateway.bind_session_stream (actor_id, stream, stream_codec_t::json);
            _bound_actors[actor_id] = std::string (bound.actor.node_rid.value ());
            auto reply =
              co_await actor
                .relay_request (zlink::message_t::from_json (bind_courier_session_req_t{
                  bound.courier_id, bound.actor, bound.session_route}))
                .async ();
            stream.reply_packet (reply).submit ();
            std::cerr << "deliverydispatch courier-session: bound courier="
                      << request.courier_id << "\n";
            co_return;
        }
        if (dispatch.packet_name () == courier_decision_msg_t::packet_name) {
            auto actor = require_bound_actor (payload.parse_json<courier_decision_msg_t> ().courier_id);
            actor.relay (payload).submit ();
            co_return;
        }
    }

  private:
    template <typename TRequest, typename TReply> task_t<TReply> request_courier_gateway (TRequest request)
    {
        auto reply = co_await _channels.request (sample_names_t::courier_route_channel, request)
          .template async<TReply> ();
        co_return reply;
    }

    channel_client_t &_channels;
    session_actor_manager_t &_actors;
    actor_gateway_t &_gateway;
    std::map<std::string, std::string> _bound_actors;

    session_actor_t require_bound_actor (const std::string &actor_id)
    {
        if (!_bound_actors.contains (actor_id)) {
            throw framework_exception_t (framework_error_kind_t::actor_route_not_found,
                                         "courier actor is not bound: " + actor_id);
        }
        auto actor = _actors.find (actor_id);
        if (!actor) {
            throw framework_exception_t (framework_error_kind_t::actor_route_not_found,
                                         "bound courier actor route is not found: " + actor_id);
        }
        return *actor;
    }
};

} // namespace zlink::samples::deliverydispatch

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::deliverydispatch;

    const sample_topology_t topology;
    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (deliverydispatch_log_dir () + "/flow-courier-session.log")
          .trace_label ("deliverydispatch-courier-session");
        add_deliverydispatch_json_codecs (options.codecs ());
        add_deliverydispatch_location_store (options, topology);
        options.add_client_server_channel (sample_names_t::courier_route_channel)
          .enable_client ();
        options.add_spot_mesh (sample_names_t::courier_actor_discovery)
          .set_routing_id (zlink::routing_id_t::from (sample_names_t::courier_session_spot_node))
          .enable_router (topology.courier_session_spot_router_endpoint)
          .enable_pub_sub (topology.courier_session_spot_endpoint);
        options.add_stream_node (sample_names_t::courier_stream_node)
          .bind (topology.courier_stream_endpoint)
          .register_session<courier_session_t> ();
    });
    return app.run (argc, argv);
}
