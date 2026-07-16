/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_configuration.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <iostream>
#include <map>
#include <string>

namespace zlink::samples::deliverydispatch
{

using namespace framework;

class courier_session_t final : public packet_stream_session_t
{
  public:
    using dependency_types = dependency_list_t<route_client_t,
                                               spot_handle_resolver_t,
                                               session_actor_manager_t>;

    courier_session_t (route_client_t &routes,
                       spot_handle_resolver_t &spot_handles,
                       session_actor_manager_t &actors) :
        _routes (routes), _spot_handles (spot_handles), _actors (actors)
    {
    }

    task_t<void> on_connected (stream_t &) override { co_return; }

    task_t<void> on_disconnected (stream_t &) override
    {
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
            /* 공통 sample spec §8.1: 기존 actor를 먼저 찾고, 없을 때만 만든다. 배치 노드는
             * courier id가 정한다(placement policy). */
            const auto node = sample_names_t::courier_actor_node (request.courier_id);
            auto entry_spot = co_await resolve_courier_entry_spot (node);
            auto found =
              co_await _routes
                .request_to_spot (entry_spot, find_courier_actor_req_t{request.courier_id})
                .async<find_courier_actor_res_t> ();
            auto actor_ref = found.actor;
            if (!actor_ref) {
                auto ensured =
                  co_await _routes
                    .request_to_spot (entry_spot,
                                      ensure_courier_actor_req_t{request.courier_id})
                    .async<ensure_courier_actor_res_t> ();
                actor_ref = ensured.actor;
            }

            const std::string session_route = "courier-session:" + request.courier_id;
            auto actor =
              co_await _actors
                .bind_or_get (actor_ref->to_actor_ref (sample_names_t::courier_actor_type))
                .async ();
            const auto actor_id = std::string (actor.actor_id ());
            _bound_actors[actor_id] = std::string (actor_ref->node_rid.value ());
            /* location store 조회를 await한 뒤라 stream dispatch 문맥의 thread-local 헤더에
             * 기댈 수 없다. relay 대상 packet name을 명시하는 오버로드를 쓴다. */
            auto reply = co_await actor
                           .relay_request (bind_courier_session_req_t::packet_name,
                                           zlink::message_t::from_json (bind_courier_session_req_t{
                                             request.courier_id, *actor_ref, session_route}))
                           .async ();
            stream.reply_packet (reply).submit ();
            std::cerr << "deliverydispatch courier-session: bound courier=" << request.courier_id
                      << "\n";
            co_return;
        }
        if (dispatch.packet_name () == courier_decision_msg_t::packet_name) {
            auto actor =
              require_bound_actor (payload.parse_json<courier_decision_msg_t> ().courier_id);
            co_await actor.relay (payload);
            co_return;
        }
    }

  private:
    task_t<spot_handle_t> resolve_courier_entry_spot (const std::string &node_rid)
    {
        auto handle =
          co_await _spot_handles.resolve_spot_handle (spot_rid_t::from_string (node_rid));
        if (!handle) {
            throw framework_exception_t (framework_error_kind_t::spot_route_not_found,
                                         "courier entry spot has no live location row: "
                                           + node_rid);
        }
        co_return *handle;
    }

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

    route_client_t &_routes;
    spot_handle_resolver_t &_spot_handles;
    session_actor_manager_t &_actors;
    std::map<std::string, std::string> _bound_actors;
};

} // namespace zlink::samples::deliverydispatch

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::deliverydispatch;

    auto app = app_t::create ();
    const auto configuration = load_sample_configuration (app, argc, argv);
    const auto &topology = configuration.topology;
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (configuration.flow_log_path ())
          .trace_label ("deliverydispatch-courier-session");
        add_deliverydispatch_json_codecs (options.codecs ());
        add_deliverydispatch_location_store (options, topology);
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
