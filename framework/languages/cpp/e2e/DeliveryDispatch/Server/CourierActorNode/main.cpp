/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <iostream>
#include <string>

namespace zlink::samples::deliverydispatch
{

using namespace framework;

static std::string g_node_rid;

class ensure_courier_actor_handler_t
{
  public:
    using request_type = ensure_courier_actor_req_t;
    using reply_type = ensure_courier_actor_res_t;
    static constexpr const char *topic_name = ensure_courier_actor_req_t::packet_name;

    ensure_courier_actor_res_t handle (const ensure_courier_actor_req_t &request,
                                       const route_handler_context_t &)
    {
        return {request.courier_id, actor_ref_snapshot_t{g_node_rid, request.courier_id, 1}};
    }
};

class actor_node_offer_delivery_handler_t
{
  public:
    using dependency_types = dependency_list_t<channel_client_t>;
    using request_type = offer_delivery_req_t;
    using reply_type = offer_delivery_res_t;
    static constexpr const char *topic_name = offer_delivery_req_t::packet_name;

    explicit actor_node_offer_delivery_handler_t (channel_client_t &channels) : _channels (channels) {}

    task_t<offer_delivery_res_t> handle (const offer_delivery_req_t &request)
    {
        auto reply =
          co_await _channels.request (sample_names_t::courier_session_route_channel, request)
            .template async<offer_delivery_res_t> ();
        co_return reply;
    }

  private:
    channel_client_t &_channels;
};

} // namespace zlink::samples::deliverydispatch

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::deliverydispatch;

    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <node-rid>\n";
        return 2;
    }

    const std::string node_rid = argv[1];
    g_node_rid = node_rid;
    const sample_topology_t topology;
    const auto route_endpoint =
      node_rid == sample_names_t::courier_actor_node_1
        ? topology.courier_actor_node_1_route_endpoint
        : topology.courier_actor_node_2_route_endpoint;

    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (deliverydispatch_log_dir () + "/flow-" + node_rid + ".log")
          .trace_label ("deliverydispatch-" + node_rid);
        add_deliverydispatch_json_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
        options.services ().add_transient<ensure_courier_actor_handler_t> ();
        options.services ().add_transient<actor_node_offer_delivery_handler_t, channel_client_t> ();
        options.add_client_server_channel (sample_names_t::courier_session_route_channel)
          .enable_client ();
        options.add_route_mesh (sample_names_t::courier_actor_node_route_channel)
          .enable_server (route_endpoint)
          .enable_client ()
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .add_request_handler<ensure_courier_actor_handler_t, ensure_courier_actor_req_t,
                               ensure_courier_actor_res_t> (
            ensure_courier_actor_req_t::packet_name, &ensure_courier_actor_handler_t::handle)
          .add_request_handler<actor_node_offer_delivery_handler_t, offer_delivery_req_t,
                               offer_delivery_res_t> (
            offer_delivery_req_t::packet_name, &actor_node_offer_delivery_handler_t::handle);
    });
    return app.run (argc, argv);
}
