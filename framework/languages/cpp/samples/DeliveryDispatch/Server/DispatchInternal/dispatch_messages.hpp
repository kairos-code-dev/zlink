/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../Shared/Contracts/messages.hpp"

namespace zlink::samples::deliverydispatch
{

struct assign_delivery_result_t
{
    static constexpr const char *packet_name = "AssignDeliveryResult";
    std::string delivery_id;
    std::string courier_id;
};

inline void to_json (nlohmann::json &json, const assign_delivery_result_t &value)
{
    json = {{"deliveryId", value.delivery_id}, {"courierId", value.courier_id}};
}

inline void from_json (const nlohmann::json &json, assign_delivery_result_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.courier_id = json_string (json, "courierId", "courier_id");
}

} // namespace zlink::samples::deliverydispatch
