/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <stdexcept>
#include <string>

namespace zlink::samples::deliverydispatch
{

struct sample_names_t
{
    static constexpr const char *dispatch_route_channel = "deliverydispatch.dispatch";
    static constexpr const char *courier_route_channel = "deliverydispatch.courier";
    static constexpr const char *courier_actor_node_route_channel = "delivery-couriers.route";
    static constexpr const char *customer_actor_discovery = "delivery-customers";
    static constexpr const char *courier_actor_discovery = "delivery-couriers";
    static constexpr const char *courier_session_route_channel = "deliverydispatch.courier.session";
    static constexpr const char *tracking_route_channel = "deliverydispatch.tracking";
    static constexpr const char *status_fanout_channel = "deliverydispatch.status";
    static constexpr const char *status_topic = "delivery-status";
    static constexpr const char *customer_stream_node = "delivery-customer-stream";
    static constexpr const char *courier_stream_node = "delivery-courier-stream";
    static constexpr const char *customer_spot_node = "delivery-customer-node";
    static constexpr const char *customer_actor_type = "delivery-customer";
    static constexpr const char *courier_actor_node_1 = "delivery-courier-node-1";
    static constexpr const char *courier_actor_node_2 = "delivery-courier-node-2";
    static constexpr const char *courier_session_spot_node = "delivery-courier-session-node";
    static constexpr const char *courier_actor_type = "delivery-courier";
    static constexpr const char *customer_id = "customer-1";

    static std::string courier_actor_node (const std::string &courier_id)
    {
        if (courier_id == "courier-a") {
            return courier_actor_node_1;
        }
        if (courier_id == "courier-b") {
            return courier_actor_node_2;
        }
        throw std::runtime_error ("unknown courier '" + courier_id + "'");
    }
};

} // namespace zlink::samples::deliverydispatch
