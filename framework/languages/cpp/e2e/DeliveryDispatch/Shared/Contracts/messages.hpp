/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/framework/contracts/actors/actor.hpp>

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#ifndef ZLINK_CPP_FRAMEWORK_ACTOR_REF_SNAPSHOT_JSON_HPP
#define ZLINK_CPP_FRAMEWORK_ACTOR_REF_SNAPSHOT_JSON_HPP
namespace zlink::framework
{
inline void to_json (nlohmann::json &json, const actor_ref_snapshot_t &value)
{
    json = {{"nodeRid", std::string (value.node_rid.value ())},
            {"actorId", value.actor_id},
            {"generation", value.generation}};
}
inline void from_json (const nlohmann::json &json, actor_ref_snapshot_t &value)
{
    const auto node_rid = json.contains ("nodeRid") ? json.value ("nodeRid", std::string{})
                                                    : json.value ("node_rid", std::string{});
    value.node_rid = node_rid_t::from_string (node_rid);
    value.actor_id = json.contains ("actorId") ? json.value ("actorId", std::string{})
                                               : json.value ("actor_id", std::string{});
    value.generation = json.value ("generation", std::uint64_t{0});
}
} // namespace zlink::framework
#endif

namespace zlink::samples::deliverydispatch
{

using actor_ref_snapshot_t = zlink::framework::actor_ref_snapshot_t;

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

struct create_delivery_req_t
{
    static constexpr const char *packet_name = "CreateDeliveryReq";
    std::string delivery_id;
    std::string customer_id;
    std::string pickup_address;
    std::string dropoff_address;
};

struct create_delivery_res_t
{
    static constexpr const char *packet_name = "CreateDeliveryRes";
    std::string delivery_id;
};

struct ensure_customer_actor_req_t
{
    static constexpr const char *packet_name = "EnsureCustomerActorReq";
    std::string customer_id;
};

struct ensure_customer_actor_res_t
{
    static constexpr const char *packet_name = "EnsureCustomerActorReqRes";
    std::string customer_id;
    actor_ref_snapshot_t actor;
};

struct bind_courier_req_t
{
    static constexpr const char *packet_name = "BindCourierReq";
    std::string courier_id;
    std::string session_route;
};

struct bind_courier_res_t
{
    static constexpr const char *packet_name = "BindCourierRes";
    std::string courier_id;
    actor_ref_snapshot_t actor;
    std::string session_route;
};

struct bind_courier_session_req_t
{
    static constexpr const char *packet_name = "BindCourierSessionReq";
    std::string courier_id;
    actor_ref_snapshot_t actor;
    std::string session_route;
};

struct bind_courier_session_res_t
{
    static constexpr const char *packet_name = "BindCourierSessionRes";
    std::string courier_id;
    actor_ref_snapshot_t actor;
    std::string session_route;
};

struct ensure_courier_actor_req_t
{
    static constexpr const char *packet_name = "EnsureCourierActorReq";
    std::string courier_id;
};

struct ensure_courier_actor_res_t
{
    static constexpr const char *packet_name = "EnsureCourierActorRes";
    std::string courier_id;
    actor_ref_snapshot_t actor;
};

struct subscribe_delivery_req_t
{
    static constexpr const char *packet_name = "SubscribeDelivery";
    std::string delivery_id;
};

struct subscribe_delivery_res_t
{
    static constexpr const char *packet_name = "SubscribeDeliveryRes";
    std::string delivery_id;
};

struct subscribe_customer_to_delivery_req_t
{
    static constexpr const char *packet_name = "SubscribeCustomerToDeliveryReq";
    std::string customer_id;
    std::string delivery_id;
};

struct subscribe_customer_to_delivery_res_t
{
    static constexpr const char *packet_name = "SubscribeCustomerToDeliveryRes";
    std::string customer_id;
    std::string delivery_id;
};

struct assign_delivery_req_t
{
    static constexpr const char *packet_name = "AssignDelivery";
    std::string delivery_id;
    std::string customer_id;
    std::string pickup_address;
    std::string dropoff_address;
};

struct assign_delivery_res_t
{
    static constexpr const char *packet_name = "AssignDeliveryRes";
    std::string delivery_id;
    std::string courier_id;
};

struct offer_delivery_req_t
{
    static constexpr const char *packet_name = "OfferDelivery";
    std::string courier_id;
    std::string delivery_id;
    std::string pickup_address;
    std::string dropoff_address;
};

struct offer_delivery_notify_t
{
    static constexpr const char *packet_name = "OfferDeliveryNotify";
    std::string courier_id;
    std::string delivery_id;
    std::string pickup_address;
    std::string dropoff_address;
};

struct offer_delivery_res_t
{
    static constexpr const char *packet_name = "OfferDeliveryRes";
    std::string delivery_id;
    std::string courier_id;
    bool accepted{false};
    std::string reason;
};

struct courier_decision_msg_t
{
    static constexpr const char *packet_name = "CourierDecisionMsg";
    std::string delivery_id;
    std::string courier_id;
    bool accepted{false};
    std::string reason;
};

struct reassign_delivery_msg_t
{
    static constexpr const char *packet_name = "ReassignDeliveryMsg";
    std::string delivery_id;
    std::string previous_courier_id;
    std::string next_courier_id;
    std::string reason;
};

struct delivery_status_req_t
{
    static constexpr const char *packet_name = "DeliveryStatusReq";
    std::string delivery_id;
    std::string customer_id;
    std::string status;
    std::string courier_id;
    std::string occurred_at;
};

struct delivery_status_updated_msg_t
{
    static constexpr const char *packet_name = "DeliveryStatusUpdatedMsg";
    std::string delivery_id;
    std::string customer_id;
    std::string status;
    std::string courier_id;
    std::string occurred_at;
};

struct delivery_status_res_t
{
    static constexpr const char *packet_name = "DeliveryStatusRes";
    std::string delivery_id;
    std::string status;
};

struct delivery_spot_create_req_t
{
    static constexpr const char *packet_name = "DeliverySpotCreateReq";
    std::string delivery_id;
};

struct delivery_spot_create_res_t
{
    static constexpr const char *packet_name = "DeliverySpotCreateReqRes";
    std::string delivery_id;
};

struct delivery_spot_join_req_t
{
    static constexpr const char *packet_name = "DeliverySpotJoinReq";
    std::string delivery_id;
    std::string customer_id;
};

struct delivery_spot_join_res_t
{
    static constexpr const char *packet_name = "DeliverySpotJoinReqRes";
    std::string delivery_id;
    std::string customer_id;
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

inline std::string json_string (const nlohmann::json &json,
                                const char *camel,
                                const char *snake,
                                std::string fallback = {})
{
    if (json.contains (camel)) {
        return json.value (camel, fallback);
    }
    return json.value (snake, fallback);
}

inline void to_json (nlohmann::json &json, const create_delivery_req_t &value)
{
    json = {{"deliveryId", value.delivery_id},
            {"customerId", value.customer_id},
            {"pickupAddress", value.pickup_address},
            {"dropoffAddress", value.dropoff_address}};
}

inline void from_json (const nlohmann::json &json, create_delivery_req_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.customer_id = json_string (json, "customerId", "customer_id");
    value.pickup_address = json_string (json, "pickupAddress", "pickup_address");
    value.dropoff_address = json_string (json, "dropoffAddress", "dropoff_address");
}

inline void to_json (nlohmann::json &json, const create_delivery_res_t &value)
{
    json = {{"deliveryId", value.delivery_id}};
}

inline void from_json (const nlohmann::json &json, create_delivery_res_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
}

inline void to_json (nlohmann::json &json, const ensure_customer_actor_req_t &value)
{
    json = {{"customerId", value.customer_id}};
}

inline void from_json (const nlohmann::json &json, ensure_customer_actor_req_t &value)
{
    value.customer_id = json_string (json, "customerId", "customer_id");
}

inline void to_json (nlohmann::json &json, const ensure_customer_actor_res_t &value)
{
    json = {{"customerId", value.customer_id}, {"actor", value.actor}};
}

inline void from_json (const nlohmann::json &json, ensure_customer_actor_res_t &value)
{
    value.customer_id = json_string (json, "customerId", "customer_id");
    value.actor = json.value ("actor", actor_ref_snapshot_t{});
}

inline void to_json (nlohmann::json &json, const bind_courier_req_t &value)
{
    json = {{"courierId", value.courier_id}, {"sessionRoute", value.session_route}};
}

inline void from_json (const nlohmann::json &json, bind_courier_req_t &value)
{
    value.courier_id = json_string (json, "courierId", "courier_id");
    value.session_route = json_string (json, "sessionRoute", "session_route");
}

inline void to_json (nlohmann::json &json, const bind_courier_res_t &value)
{
    json = {{"courierId", value.courier_id},
            {"actor", value.actor},
            {"sessionRoute", value.session_route}};
}

inline void from_json (const nlohmann::json &json, bind_courier_res_t &value)
{
    value.courier_id = json_string (json, "courierId", "courier_id");
    value.actor = json.value ("actor", actor_ref_snapshot_t{});
    value.session_route = json_string (json, "sessionRoute", "session_route");
}

inline void to_json (nlohmann::json &json, const bind_courier_session_req_t &value)
{
    json = {{"courierId", value.courier_id},
            {"actor", value.actor},
            {"sessionRoute", value.session_route}};
}

inline void from_json (const nlohmann::json &json, bind_courier_session_req_t &value)
{
    value.courier_id = json_string (json, "courierId", "courier_id");
    value.actor = json.value ("actor", actor_ref_snapshot_t{});
    value.session_route = json_string (json, "sessionRoute", "session_route");
}

inline void to_json (nlohmann::json &json, const bind_courier_session_res_t &value)
{
    json = {{"courierId", value.courier_id},
            {"actor", value.actor},
            {"sessionRoute", value.session_route}};
}

inline void from_json (const nlohmann::json &json, bind_courier_session_res_t &value)
{
    value.courier_id = json_string (json, "courierId", "courier_id");
    value.actor = json.value ("actor", actor_ref_snapshot_t{});
    value.session_route = json_string (json, "sessionRoute", "session_route");
}

inline void to_json (nlohmann::json &json, const ensure_courier_actor_req_t &value)
{
    json = {{"courierId", value.courier_id}};
}

inline void from_json (const nlohmann::json &json, ensure_courier_actor_req_t &value)
{
    value.courier_id = json_string (json, "courierId", "courier_id");
}

inline void to_json (nlohmann::json &json, const ensure_courier_actor_res_t &value)
{
    json = {{"courierId", value.courier_id}, {"actor", value.actor}};
}

inline void from_json (const nlohmann::json &json, ensure_courier_actor_res_t &value)
{
    value.courier_id = json_string (json, "courierId", "courier_id");
    value.actor = json.value ("actor", actor_ref_snapshot_t{});
}

inline void to_json (nlohmann::json &json, const subscribe_delivery_req_t &value)
{
    json = {{"deliveryId", value.delivery_id}};
}

inline void from_json (const nlohmann::json &json, subscribe_delivery_req_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
}

inline void to_json (nlohmann::json &json, const subscribe_delivery_res_t &value)
{
    json = {{"deliveryId", value.delivery_id}};
}

inline void from_json (const nlohmann::json &json, subscribe_delivery_res_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
}

inline void to_json (nlohmann::json &json, const subscribe_customer_to_delivery_req_t &value)
{
    json = {{"customerId", value.customer_id}, {"deliveryId", value.delivery_id}};
}

inline void from_json (const nlohmann::json &json, subscribe_customer_to_delivery_req_t &value)
{
    value.customer_id = json_string (json, "customerId", "customer_id");
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
}

inline void to_json (nlohmann::json &json, const subscribe_customer_to_delivery_res_t &value)
{
    json = {{"customerId", value.customer_id}, {"deliveryId", value.delivery_id}};
}

inline void from_json (const nlohmann::json &json, subscribe_customer_to_delivery_res_t &value)
{
    value.customer_id = json_string (json, "customerId", "customer_id");
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
}

inline void to_json (nlohmann::json &json, const assign_delivery_req_t &value)
{
    json = {{"deliveryId", value.delivery_id},
            {"customerId", value.customer_id},
            {"pickupAddress", value.pickup_address},
            {"dropoffAddress", value.dropoff_address}};
}

inline void from_json (const nlohmann::json &json, assign_delivery_req_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.customer_id = json_string (json, "customerId", "customer_id");
    value.pickup_address = json_string (json, "pickupAddress", "pickup_address");
    value.dropoff_address = json_string (json, "dropoffAddress", "dropoff_address");
}

inline void to_json (nlohmann::json &json, const assign_delivery_res_t &value)
{
    json = {{"deliveryId", value.delivery_id}, {"courierId", value.courier_id}};
}

inline void from_json (const nlohmann::json &json, assign_delivery_res_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.courier_id = json_string (json, "courierId", "courier_id");
}

inline void to_json (nlohmann::json &json, const offer_delivery_req_t &value)
{
    json = {{"courierId", value.courier_id},
            {"deliveryId", value.delivery_id},
            {"pickupAddress", value.pickup_address},
            {"dropoffAddress", value.dropoff_address}};
}

inline void from_json (const nlohmann::json &json, offer_delivery_req_t &value)
{
    value.courier_id = json_string (json, "courierId", "courier_id");
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.pickup_address = json_string (json, "pickupAddress", "pickup_address");
    value.dropoff_address = json_string (json, "dropoffAddress", "dropoff_address");
}

inline void to_json (nlohmann::json &json, const offer_delivery_notify_t &value)
{
    json = {{"courierId", value.courier_id},
            {"deliveryId", value.delivery_id},
            {"pickupAddress", value.pickup_address},
            {"dropoffAddress", value.dropoff_address}};
}

inline void from_json (const nlohmann::json &json, offer_delivery_notify_t &value)
{
    value.courier_id = json_string (json, "courierId", "courier_id");
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.pickup_address = json_string (json, "pickupAddress", "pickup_address");
    value.dropoff_address = json_string (json, "dropoffAddress", "dropoff_address");
}

inline void to_json (nlohmann::json &json, const offer_delivery_res_t &value)
{
    json = {{"deliveryId", value.delivery_id},
            {"courierId", value.courier_id},
            {"accepted", value.accepted},
            {"reason", value.reason}};
}

inline void from_json (const nlohmann::json &json, offer_delivery_res_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.courier_id = json_string (json, "courierId", "courier_id");
    value.accepted = json.value ("accepted", false);
    value.reason = json.value ("reason", "");
}

inline void to_json (nlohmann::json &json, const courier_decision_msg_t &value)
{
    json = {{"deliveryId", value.delivery_id},
            {"courierId", value.courier_id},
            {"accepted", value.accepted},
            {"reason", value.reason}};
}

inline void from_json (const nlohmann::json &json, courier_decision_msg_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.courier_id = json_string (json, "courierId", "courier_id");
    value.accepted = json.value ("accepted", false);
    value.reason = json.value ("reason", "");
}

inline void to_json (nlohmann::json &json, const reassign_delivery_msg_t &value)
{
    json = {{"deliveryId", value.delivery_id},
            {"previousCourierId", value.previous_courier_id},
            {"nextCourierId", value.next_courier_id},
            {"reason", value.reason}};
}

inline void from_json (const nlohmann::json &json, reassign_delivery_msg_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.previous_courier_id = json_string (json, "previousCourierId", "previous_courier_id");
    value.next_courier_id = json_string (json, "nextCourierId", "next_courier_id");
    value.reason = json.value ("reason", "");
}

inline void to_json (nlohmann::json &json, const delivery_status_req_t &value)
{
    json = {{"deliveryId", value.delivery_id},
            {"customerId", value.customer_id},
            {"status", value.status},
            {"courierId", value.courier_id},
            {"occurredAt", value.occurred_at}};
}

inline void from_json (const nlohmann::json &json, delivery_status_req_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.customer_id = json_string (json, "customerId", "customer_id");
    value.status = json.value ("status", "");
    value.courier_id = json_string (json, "courierId", "courier_id");
    value.occurred_at = json_string (json, "occurredAt", "occurred_at");
}

inline void to_json (nlohmann::json &json, const delivery_status_updated_msg_t &value)
{
    json = {{"deliveryId", value.delivery_id},
            {"customerId", value.customer_id},
            {"status", value.status},
            {"courierId", value.courier_id},
            {"occurredAt", value.occurred_at}};
}

inline void from_json (const nlohmann::json &json, delivery_status_updated_msg_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.customer_id = json_string (json, "customerId", "customer_id");
    value.status = json.value ("status", "");
    value.courier_id = json_string (json, "courierId", "courier_id");
    value.occurred_at = json_string (json, "occurredAt", "occurred_at");
}

inline void to_json (nlohmann::json &json, const delivery_status_res_t &value)
{
    json = {{"deliveryId", value.delivery_id}, {"status", value.status}};
}

inline void from_json (const nlohmann::json &json, delivery_status_res_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.status = json.value ("status", "");
}

inline void to_json (nlohmann::json &json, const delivery_spot_create_req_t &value)
{
    json = {{"deliveryId", value.delivery_id}};
}

inline void from_json (const nlohmann::json &json, delivery_spot_create_req_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
}

inline void to_json (nlohmann::json &json, const delivery_spot_create_res_t &value)
{
    json = {{"deliveryId", value.delivery_id}};
}

inline void from_json (const nlohmann::json &json, delivery_spot_create_res_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
}

inline void to_json (nlohmann::json &json, const delivery_spot_join_req_t &value)
{
    json = {{"deliveryId", value.delivery_id}, {"customerId", value.customer_id}};
}

inline void from_json (const nlohmann::json &json, delivery_spot_join_req_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.customer_id = json_string (json, "customerId", "customer_id");
}

inline void to_json (nlohmann::json &json, const delivery_spot_join_res_t &value)
{
    json = {{"deliveryId", value.delivery_id}, {"customerId", value.customer_id}};
}

inline void from_json (const nlohmann::json &json, delivery_spot_join_res_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.customer_id = json_string (json, "customerId", "customer_id");
}

inline void to_json (nlohmann::json &json, const delivery_status_notify_t &value)
{
    json = {{"deliveryId", value.delivery_id},
            {"status", value.status},
            {"courierId", value.courier_id},
            {"occurredAt", value.occurred_at}};
}

inline void from_json (const nlohmann::json &json, delivery_status_notify_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.status = json.value ("status", "");
    value.courier_id = json_string (json, "courierId", "courier_id");
    value.occurred_at = json_string (json, "occurredAt", "occurred_at");
}

inline void to_json (nlohmann::json &json, const server_assertion_req_t &value)
{
    json = {{"successfulDeliveryId", value.successful_delivery_id},
            {"reassignedDeliveryId", value.reassigned_delivery_id}};
}

inline void from_json (const nlohmann::json &json, server_assertion_req_t &value)
{
    value.successful_delivery_id =
      json_string (json, "successfulDeliveryId", "successful_delivery_id");
    value.reassigned_delivery_id =
      json_string (json, "reassignedDeliveryId", "reassigned_delivery_id");
}

inline void to_json (nlohmann::json &json, const server_assertion_res_t &value)
{
    json = {{"passed", value.passed}, {"evidence", value.evidence}};
}

inline void from_json (const nlohmann::json &json, server_assertion_res_t &value)
{
    value.passed = json.value ("passed", false);
    value.evidence = json.value ("evidence", std::vector<std::string>{});
}

template <typename T> zlink::message_t to_stream_payload (const T &value)
{
    return zlink::message_t::from_json (value);
}

template <typename T> void from_stream_payload (const zlink::message_t &payload, T &value)
{
    value = payload.parse_json<T> ();
}

} // namespace zlink::samples::deliverydispatch
