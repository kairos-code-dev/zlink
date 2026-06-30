/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/evidence_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <iostream>
#include <utility>

namespace zlink::samples::deliverydispatch
{

using namespace framework;

class ensure_customer_actor_handler_t
{
  public:
    using request_type = ensure_customer_actor_t;
    using reply_type = customer_actor_ensured_t;
    static constexpr const char *topic_name = "EnsureCustomerActor";

    customer_actor_ensured_t handle (const ensure_customer_actor_t &request)
    {
        return {request.customer_id, {"tracking-spot", request.customer_id, 1}};
    }
};

class subscribe_customer_to_delivery_handler_t
{
  public:
    using request_type = subscribe_customer_to_delivery_t;
    using reply_type = customer_delivery_subscribed_t;
    static constexpr const char *topic_name = "SubscribeCustomerToDelivery";

    customer_delivery_subscribed_t handle (const subscribe_customer_to_delivery_t &request)
    {
        return {request.customer_id, request.delivery_id};
    }
};

class delivery_status_changed_handler_t
{
  public:
    using request_type = delivery_status_changed_t;
    using reply_type = delivery_status_ack_t;
    using dependency_types = dependency_list_t<evidence_store_t, publisher_t>;
    static constexpr const char *topic_name = "DeliveryStatusChanged";

    delivery_status_changed_handler_t (evidence_store_t &evidence, publisher_t &fanout) :
        _evidence (evidence), _fanout (fanout)
    {
    }

    task_t<delivery_status_ack_t> handle (const delivery_status_changed_t &request)
    {
        _evidence.append (request);
        delivery_status_notify_t notify{
          request.delivery_id, request.status, request.courier_id, request.occurred_at};
        co_await _fanout.publish (sample_names_t::status_fanout_channel, sample_names_t::status_topic,
                                  notify)
          .async ();
        std::cerr << "deliverydispatch tracking: status delivery=" << request.delivery_id
                  << " status=" << request.status << " courier=" << request.courier_id << "\n";
        co_return delivery_status_ack_t{request.delivery_id, request.status};
    }

  private:
    evidence_store_t &_evidence;
    publisher_t &_fanout;
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
          .trace_log_file (deliverydispatch_log_dir () + "/flow-tracking.log")
          .trace_label ("deliverydispatch-tracking");
        options.services ().add_singleton<evidence_store_t> ();
        add_deliverydispatch_json_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
        options.add_client_server_channel (sample_names_t::tracking_route_channel)
          .enable_server (topology.tracking_route_endpoint);
        options.add_fanout_channel (sample_names_t::status_fanout_channel)
          .enable_publisher (topology.status_fanout_endpoint);
        options.handlers ()
          .group ("tracking")
          .add<ensure_customer_actor_handler_t> ()
          .add<subscribe_customer_to_delivery_handler_t> ()
          .add<delivery_status_changed_handler_t> ();
        options.add_client_server_channel (sample_names_t::tracking_route_channel)
          .use_handler_group ("tracking");
    });
    return app.run (argc, argv);
}
