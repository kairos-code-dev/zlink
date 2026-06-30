/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <stdexcept>
#include <string>

namespace zlink::samples::deliverydispatch
{

struct sample_names_t
{
    static constexpr const char *dispatch_route_channel = "deliverydispatch.dispatch";
    static constexpr const char *courier_a_channel = "deliverydispatch.courier.a";
    static constexpr const char *courier_b_channel = "deliverydispatch.courier.b";
    static constexpr const char *tracking_route_channel = "deliverydispatch.tracking";
    static constexpr const char *status_fanout_channel = "deliverydispatch.status";
    static constexpr const char *status_topic = "delivery-status";
    static constexpr const char *customer_stream_node = "delivery-customer-stream";
    static constexpr const char *customer_id = "customer-1";

    static std::string courier_channel (const std::string &courier_id)
    {
        if (courier_id == "courier-a") {
            return courier_a_channel;
        }
        if (courier_id == "courier-b") {
            return courier_b_channel;
        }
        throw std::runtime_error ("unknown courier '" + courier_id + "'");
    }
};

} // namespace zlink::samples::deliverydispatch
