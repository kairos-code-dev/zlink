/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../DispatchInternal/dispatch_messages.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>

namespace zlink::samples::deliverydispatch
{

using namespace framework;

inline std::string now_text ()
{
    return std::to_string (static_cast<long long> (std::time (nullptr)));
}

class assign_delivery_handler_t
{
  public:
    using request_type = assign_delivery_req_t;
    using reply_type = assign_delivery_result_t;
    using dependency_types = dependency_list_t<channel_client_t>;
    static constexpr const char *topic_name = "AssignDelivery";

    explicit assign_delivery_handler_t (channel_client_t &channels) : _channels (channels) {}

    task_t<assign_delivery_result_t> handle (const assign_delivery_req_t &request)
    {
        std::cerr << "deliverydispatch dispatch: assign delivery=" << request.delivery_id
                  << " customer=" << request.customer_id << "\n";
        co_await publish_status (request.delivery_id, request.customer_id,
                                 delivery_status_t::assigned, "courier-a");
        auto first = co_await offer (request, "courier-a");
        if (first.accepted) {
            co_await continue_delivery (request.delivery_id, request.customer_id,
                                        first.courier_id);
            co_return assign_delivery_result_t{request.delivery_id, first.courier_id};
        }

        co_await publish_status (request.delivery_id, request.customer_id,
                                 delivery_status_t::reassigned, "courier-b");
        auto second = co_await offer (request, "courier-b");
        if (!second.accepted) {
            co_await publish_status (request.delivery_id, request.customer_id,
                                     delivery_status_t::failed,
                                     second.courier_id);
            throw std::runtime_error ("delivery was rejected by all couriers");
        }
        co_await continue_delivery (request.delivery_id, request.customer_id, second.courier_id);
        co_return assign_delivery_result_t{request.delivery_id, second.courier_id};
    }

  private:
    task_t<offer_delivery_res_t> offer (const assign_delivery_req_t &request,
                                        const std::string &courier_id)
    {
        offer_delivery_req_t offer{courier_id, request.delivery_id, request.pickup_address,
                                   request.dropoff_address};
        auto result =
          co_await _channels.request (sample_names_t::courier_route_channel, offer)
            .async<offer_delivery_res_t> ();
        if (result.courier_id.empty ()) {
            result.courier_id = courier_id;
        }
        co_return result;
    }

    task_t<void> continue_delivery (const std::string &delivery_id,
                                    const std::string &customer_id,
                                    const std::string &courier_id)
    {
        co_await publish_status (delivery_id, customer_id, delivery_status_t::accepted,
                                 courier_id);
        co_await publish_status (delivery_id, customer_id, delivery_status_t::picked_up,
                                 courier_id);
        co_await publish_status (delivery_id, customer_id, delivery_status_t::delivered,
                                 courier_id);
    }

    task_t<void> publish_status (const std::string &delivery_id,
                                 const std::string &customer_id,
                                 const std::string &status,
                                 const std::string &courier_id)
    {
        delivery_status_changed_req_t changed{delivery_id, customer_id, status, courier_id,
                                              now_text ()};
        (void) co_await _channels.request (sample_names_t::tracking_route_channel, changed)
          .async<delivery_status_changed_res_t> ();
    }

    channel_client_t &_channels;
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
          .trace_log_file (deliverydispatch_log_dir () + "/flow-dispatch-center.log")
          .trace_label ("deliverydispatch-dispatch-center");
        add_deliverydispatch_json_codecs (options.codecs ());
        add_deliverydispatch_location_store (options, topology);
        options.add_client_server_channel (sample_names_t::dispatch_route_channel)
          .enable_server (topology.dispatch_center_route_endpoint)
          .use_handler_group ("dispatch");
        options.add_client_server_channel (sample_names_t::courier_route_channel).enable_client ();
        options.add_client_server_channel (sample_names_t::tracking_route_channel).enable_client ();
        options.handlers ().group ("dispatch").add<assign_delivery_handler_t> ();
    });
    return app.run (argc, argv);
}
