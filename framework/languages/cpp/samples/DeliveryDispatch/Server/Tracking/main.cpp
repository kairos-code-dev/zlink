/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/evidence_store.hpp"
#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../common_codecs.hpp"
#include "Handlers/tracking_handlers.hpp"
#include "Spots/DeliveryTrackingSpot/delivery_spot_directory.hpp"

#include <zlink/framework.hpp>

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::deliverydispatch;

    const sample_topology_t topology;
    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (deliverydispatch_log_dir () + "/flow-tracking.log")
          .trace_label ("deliverydispatch-tracking");
        options.services ().add_singleton<evidence_store_t> ();
        options.services ().add_singleton<delivery_spot_directory_t> ();
        add_deliverydispatch_json_codecs (options.codecs ());
        add_deliverydispatch_location_store (options, topology);
        options.add_client_server_channel (sample_names_t::tracking_route_channel)
          .enable_server (topology.tracking_route_endpoint)
          .use_handler_group ("tracking");
        options.add_fanout_channel (sample_names_t::status_fanout_channel)
          .enable_publisher (topology.status_fanout_endpoint);
        options.handlers ()
          .group ("tracking")
          .add<ensure_customer_actor_handler_t> ()
          .add<delivery_status_changed_handler_t> ();
    });
    return app.run (argc, argv);
}
