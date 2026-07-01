/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/evidence_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../DispatchInternal/dispatch_messages.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <iostream>

namespace zlink::samples::deliverydispatch
{

using namespace framework;

class create_delivery_http_handler_t
{
  public:
    using request_type = create_delivery_req_t;
    using reply_type = create_delivery_res_t;
    using dependency_types = dependency_list_t<channel_client_t>;
    static constexpr const char *topic_name = "CreateDeliveryReq";

    explicit create_delivery_http_handler_t (channel_client_t &channels) : _channels (channels) {}

    task_t<create_delivery_res_t> handle (const create_delivery_req_t &request)
    {
        assign_delivery_req_t assign{request.delivery_id,
                                     request.customer_id,
                                     request.pickup_address,
                                     request.dropoff_address};
        auto assigned = co_await _channels.request (sample_names_t::dispatch_route_channel, assign)
                          .async<assign_delivery_result_t> ();
        std::cerr << "deliverydispatch api: created delivery=" << assigned.delivery_id
                  << " courier=" << assigned.courier_id << "\n";
        co_return create_delivery_res_t{assigned.delivery_id};
    }

  private:
    channel_client_t &_channels;
};

class server_assertion_http_handler_t
{
  public:
    using request_type = server_assertion_req_t;
    using reply_type = server_assertion_res_t;
    using dependency_types = dependency_list_t<evidence_store_t>;
    static constexpr const char *topic_name = "ServerAssertionReq";

    explicit server_assertion_http_handler_t (evidence_store_t &evidence) : _evidence (evidence) {}

    server_assertion_res_t handle (const server_assertion_req_t &request)
    {
        std::cerr << "deliverydispatch api: assert successful="
                  << request.successful_delivery_id << " reassigned="
                  << request.reassigned_delivery_id << "\n";
        const auto success =
          _evidence.has_sequence (request.successful_delivery_id,
                                  {delivery_status_t::assigned,
                                   delivery_status_t::accepted,
                                   delivery_status_t::picked_up,
                                   delivery_status_t::delivered});
        const auto reassigned =
          _evidence.has_sequence (request.reassigned_delivery_id,
                                  {delivery_status_t::assigned,
                                   delivery_status_t::reassigned,
                                   delivery_status_t::accepted,
                                   delivery_status_t::picked_up,
                                   delivery_status_t::delivered});
        return {success && reassigned, _evidence.read_lines ()};
    }

  private:
    evidence_store_t &_evidence;
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
          .trace_log_file (deliverydispatch_log_dir () + "/flow-dispatch-api.log")
          .trace_label ("deliverydispatch-dispatch-api");
        options.services ().add_singleton<evidence_store_t> ();
        add_deliverydispatch_json_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
        options.add_client_server_channel (sample_names_t::dispatch_route_channel).enable_client ();
        options.http ()
          .listen (topology.dispatch_api_http_url)
          .map_health ("/health")
          .map_post<create_delivery_http_handler_t> ("/deliveries")
          .map_post<server_assertion_http_handler_t> ("/self-check/assert");
    });
    return app.run (argc, argv);
}
