/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace zlink::samples::deliverydispatch
{

struct delivery_status_t
{
    static constexpr const char *created = "Created";
    static constexpr const char *assigned = "Assigned";
    static constexpr const char *accepted = "Accepted";
    static constexpr const char *reassigned = "Reassigned";
    static constexpr const char *picked_up = "PickedUp";
    static constexpr const char *delivered = "Delivered";
    static constexpr const char *failed = "Failed";
};

struct delivery_state_t
{
    static constexpr const char *packet_name = "DeliveryState";
    std::string delivery_id;
    std::string customer_id;
    std::string pickup_address;
    std::string dropoff_address;
    std::string courier_id;
    std::string status;
};

struct create_delivery_req_t
{
    static constexpr const char *packet_name = "CreateDeliveryReq";
    std::string delivery_id;
    std::string customer_id;
    std::string pickup_address;
    std::string dropoff_address;
};

struct delivery_created_t
{
    static constexpr const char *packet_name = "DeliveryCreated";
    std::string delivery_id;
};

struct subscribe_delivery_req_t
{
    static constexpr const char *packet_name = "SubscribeDelivery";
    std::string delivery_id;
};

struct subscribe_delivery_accepted_t
{
    static constexpr const char *packet_name = "SubscribeDeliveryAccepted";
    std::string delivery_id;
};

struct assign_delivery_req_t
{
    static constexpr const char *packet_name = "AssignDeliveryReq";
    std::string delivery_id;
    std::string courier_id;
};

struct advance_delivery_req_t
{
    static constexpr const char *packet_name = "AdvanceDeliveryReq";
    std::string delivery_id;
    std::string status;
    std::string courier_id;
};

struct delivery_status_notify_t
{
    static constexpr const char *packet_name = "DeliveryStatusNotify";
    std::string delivery_id;
    std::string status;
    std::string courier_id;
    std::string occurred_at;
};

struct server_assertion_req_t
{
    static constexpr const char *packet_name = "ServerAssertionReq";
    std::string successful_delivery_id;
    std::string reassigned_delivery_id;
};

struct server_assertion_res_t
{
    static constexpr const char *packet_name = "ServerAssertionRes";
    bool passed{false};
    std::vector<std::string> evidence;
};

inline void to_json (nlohmann::json &json, const delivery_state_t &value)
{
    json = nlohmann::json{{"deliveryId", value.delivery_id},
                          {"customerId", value.customer_id},
                          {"pickupAddress", value.pickup_address},
                          {"dropoffAddress", value.dropoff_address},
                          {"courierId", value.courier_id},
                          {"status", value.status}};
}

inline void from_json (const nlohmann::json &json, delivery_state_t &value)
{
    value.delivery_id = json.value ("deliveryId", "");
    value.customer_id = json.value ("customerId", "");
    value.pickup_address = json.value ("pickupAddress", "");
    value.dropoff_address = json.value ("dropoffAddress", "");
    value.courier_id = json.value ("courierId", "");
    value.status = json.value ("status", "");
}

inline void to_json (nlohmann::json &json, const create_delivery_req_t &value)
{
    json = nlohmann::json{{"deliveryId", value.delivery_id},
                          {"customerId", value.customer_id},
                          {"pickupAddress", value.pickup_address},
                          {"dropoffAddress", value.dropoff_address}};
}

inline void from_json (const nlohmann::json &json, create_delivery_req_t &value)
{
    value.delivery_id = json.value ("deliveryId", "");
    value.customer_id = json.value ("customerId", "");
    value.pickup_address = json.value ("pickupAddress", "");
    value.dropoff_address = json.value ("dropoffAddress", "");
}

inline void to_json (nlohmann::json &json, const delivery_created_t &value)
{
    json = nlohmann::json{{"deliveryId", value.delivery_id}};
}

inline void from_json (const nlohmann::json &json, delivery_created_t &value)
{
    value.delivery_id = json.value ("deliveryId", "");
}

inline void to_json (nlohmann::json &json, const subscribe_delivery_req_t &value)
{
    json = nlohmann::json{{"deliveryId", value.delivery_id}};
}

inline void from_json (const nlohmann::json &json, subscribe_delivery_req_t &value)
{
    value.delivery_id = json.value ("deliveryId", "");
}

inline void to_json (nlohmann::json &json, const subscribe_delivery_accepted_t &value)
{
    json = nlohmann::json{{"deliveryId", value.delivery_id}};
}

inline void from_json (const nlohmann::json &json, subscribe_delivery_accepted_t &value)
{
    value.delivery_id = json.value ("deliveryId", "");
}

inline void to_json (nlohmann::json &json, const assign_delivery_req_t &value)
{
    json = nlohmann::json{{"deliveryId", value.delivery_id}, {"courierId", value.courier_id}};
}

inline void from_json (const nlohmann::json &json, assign_delivery_req_t &value)
{
    value.delivery_id = json.value ("deliveryId", "");
    value.courier_id = json.value ("courierId", "");
}

inline void to_json (nlohmann::json &json, const advance_delivery_req_t &value)
{
    json = nlohmann::json{{"deliveryId", value.delivery_id},
                          {"status", value.status},
                          {"courierId", value.courier_id}};
}

inline void from_json (const nlohmann::json &json, advance_delivery_req_t &value)
{
    value.delivery_id = json.value ("deliveryId", "");
    value.status = json.value ("status", "");
    value.courier_id = json.value ("courierId", "");
}

inline void to_json (nlohmann::json &json, const delivery_status_notify_t &value)
{
    json = nlohmann::json{{"deliveryId", value.delivery_id},
                          {"status", value.status},
                          {"courierId", value.courier_id},
                          {"occurredAt", value.occurred_at}};
}

inline void from_json (const nlohmann::json &json, delivery_status_notify_t &value)
{
    value.delivery_id = json.value ("deliveryId", "");
    value.status = json.value ("status", "");
    value.courier_id = json.value ("courierId", "");
    value.occurred_at = json.value ("occurredAt", "");
}

inline void to_json (nlohmann::json &json, const server_assertion_req_t &value)
{
    json = nlohmann::json{{"successfulDeliveryId", value.successful_delivery_id},
                          {"reassignedDeliveryId", value.reassigned_delivery_id}};
}

inline void from_json (const nlohmann::json &json, server_assertion_req_t &value)
{
    value.successful_delivery_id = json.value ("successfulDeliveryId", "");
    value.reassigned_delivery_id = json.value ("reassignedDeliveryId", "");
}

inline void to_json (nlohmann::json &json, const server_assertion_res_t &value)
{
    json = nlohmann::json{{"passed", value.passed}, {"evidence", value.evidence}};
}

inline void from_json (const nlohmann::json &json, server_assertion_res_t &value)
{
    value.passed = json.value ("passed", false);
    value.evidence = json.value ("evidence", std::vector<std::string>{});
}

} // namespace zlink::samples::deliverydispatch
