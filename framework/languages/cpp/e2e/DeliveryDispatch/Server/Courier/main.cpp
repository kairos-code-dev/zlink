/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <cstdlib>
#include <iostream>

namespace zlink::samples::deliverydispatch
{

using namespace framework;

class offer_delivery_handler_t
{
  public:
    using request_type = offer_delivery_req_t;
    using reply_type = offer_delivery_res_t;
    static constexpr const char *topic_name = "OfferDelivery";

    offer_delivery_res_t handle (const offer_delivery_req_t &request)
    {
        const auto courier_id = env_or ("DELIVERYDISPATCH_COURIER_ID", "courier-a");
        const auto mode = env_or ("DELIVERYDISPATCH_COURIER_MODE", "accept");
        const bool accepted = mode == "timeout-reassign"
          ? request.delivery_id.find ("reassign") == std::string::npos
          : mode != "reject";
        std::cerr << "deliverydispatch courier: offer delivery=" << request.delivery_id
                  << " courier=" << courier_id << " accepted=" << accepted << "\n";
        return {request.delivery_id, courier_id, accepted, accepted ? "" : "rejected"};
    }
};

} // namespace zlink::samples::deliverydispatch

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::deliverydispatch;

    const sample_topology_t topology;
    const auto courier_id = env_or ("DELIVERYDISPATCH_COURIER_ID", "courier-a");
    const auto channel = sample_names_t::courier_channel (courier_id);
    const auto endpoint = courier_id == "courier-a" ? topology.courier_a_endpoint
                                                     : topology.courier_b_endpoint;
    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (deliverydispatch_log_dir () + "/flow-" + courier_id + ".log")
          .trace_label ("deliverydispatch-" + courier_id);
        add_deliverydispatch_json_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
        options.add_client_server_channel (channel).enable_server (endpoint).use_handler_group (
          "courier");
        options.handlers ().group ("courier").add<offer_delivery_handler_t> ();
    });
    return app.run (argc, argv);
}
