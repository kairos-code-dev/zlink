/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/framework/contracts/actors/actor.hpp>

#include <nlohmann/json.hpp>
#include <optional>
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

/* courier actor가 bind되는 SpotNode routing id. bind 응답의 actor node rid로 client가
 * 노드 배치를 확인한다(공통 sample spec §16). */
struct courier_actor_nodes_t
{
    static constexpr const char *node_1 = "delivery-courier-node-1";
    static constexpr const char *node_2 = "delivery-courier-node-2";
};

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
    static constexpr const char *packet_name = "EnsureCustomerActorRes";
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

/* 공통 sample spec §7.2/§7.3: actor를 만들기 전에 먼저 찾는다. 기존 actor가 없으면 비어 있는
 * 결과를 돌려준다(생성은 Ensure가 맡는다). */
struct find_courier_actor_req_t
{
    static constexpr const char *packet_name = "FindCourierActorReq";
    std::string courier_id;
};

struct find_courier_actor_res_t
{
    static constexpr const char *packet_name = "FindCourierActorRes";
    std::string courier_id;
    std::optional<actor_ref_snapshot_t> actor;
};

struct find_customer_actor_req_t
{
    static constexpr const char *packet_name = "FindCustomerActorReq";
    std::string customer_id;
};

struct find_customer_actor_res_t
{
    static constexpr const char *packet_name = "FindCustomerActorRes";
    std::string customer_id;
    std::optional<actor_ref_snapshot_t> actor;
};

struct subscribe_delivery_req_t
{
    static constexpr const char *packet_name = "SubscribeDeliveryReq";
    std::string delivery_id;
};

struct subscribe_delivery_res_t
{
    static constexpr const char *packet_name = "SubscribeDeliveryRes";
    std::string delivery_id;
};

/* 공통 sample spec: 배차 투입은 응답 없는 one-way send다(`AssignDeliveryMsg`). */
struct assign_delivery_msg_t
{
    static constexpr const char *packet_name = "AssignDeliveryMsg";
    std::string delivery_id;
    std::string customer_id;
    std::string pickup_address;
    std::string dropoff_address;
};

/* 공통 sample spec §7.4: 제안도 결정 결과도 응답 없는 one-way다. 사람이 버튼을 누르는 시간을
 * 요청의 응답 시간에 묶지 않는다 — 묶으면 그 요청을 처리하던 실행 줄이 결정이 올 때까지 잡힌다.
 * `attempt`는 이 배송의 몇 번째 제안인지이며, 늦게 도착한 결정을 버리는 데 쓴다. */
struct offer_delivery_msg_t
{
    static constexpr const char *packet_name = "OfferDeliveryMsg";
    std::string courier_id;
    std::string delivery_id;
    int attempt{0};
    std::string pickup_address;
    std::string dropoff_address;
};

struct offer_delivery_result_msg_t
{
    static constexpr const char *packet_name = "OfferDeliveryResultMsg";
    std::string delivery_id;
    std::string courier_id;
    int attempt{0};
    bool accepted{false};
    std::string reason;
};

struct offer_delivery_notify_t
{
    static constexpr const char *packet_name = "OfferDeliveryNotify";
    std::string courier_id;
    std::string delivery_id;
    std::string pickup_address;
    std::string dropoff_address;
};


struct courier_decision_msg_t
{
    static constexpr const char *packet_name = "CourierDecisionMsg";
    std::string delivery_id;
    std::string courier_id;
    bool accepted{false};
    std::string reason;
};

/* 공통 sample spec §10: 상태 변경에는 알림 대상 고객을 함께 전달한다. Tracking은 이 값을
 * 사용해 해당 고객 actor를 찾으므로 delivery와 고객의 관계를 별도로 추측하지 않는다. */
struct delivery_status_changed_req_t
{
    static constexpr const char *packet_name = "DeliveryStatusChangedReq";
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

struct delivery_status_changed_res_t
{
    static constexpr const char *packet_name = "DeliveryStatusChangedRes";
    std::string delivery_id;
    std::string status;
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

inline void to_json (nlohmann::json &json, const find_courier_actor_req_t &value)
{
    json = {{"courierId", value.courier_id}};
}

inline void from_json (const nlohmann::json &json, find_courier_actor_req_t &value)
{
    value.courier_id = json_string (json, "courierId", "courier_id");
}

inline void to_json (nlohmann::json &json, const find_courier_actor_res_t &value)
{
    json = {{"courierId", value.courier_id}};
    if (value.actor) {
        json["actor"] = *value.actor;
    } else {
        json["actor"] = nullptr;
    }
}

inline void from_json (const nlohmann::json &json, find_courier_actor_res_t &value)
{
    value.courier_id = json_string (json, "courierId", "courier_id");
    value.actor = json.contains ("actor") && !json.at ("actor").is_null ()
                    ? std::optional<actor_ref_snapshot_t> (
                        json.at ("actor").get<actor_ref_snapshot_t> ())
                    : std::nullopt;
}

inline void to_json (nlohmann::json &json, const find_customer_actor_req_t &value)
{
    json = {{"customerId", value.customer_id}};
}

inline void from_json (const nlohmann::json &json, find_customer_actor_req_t &value)
{
    value.customer_id = json_string (json, "customerId", "customer_id");
}

inline void to_json (nlohmann::json &json, const find_customer_actor_res_t &value)
{
    json = {{"customerId", value.customer_id}};
    if (value.actor) {
        json["actor"] = *value.actor;
    } else {
        json["actor"] = nullptr;
    }
}

inline void from_json (const nlohmann::json &json, find_customer_actor_res_t &value)
{
    value.customer_id = json_string (json, "customerId", "customer_id");
    value.actor = json.contains ("actor") && !json.at ("actor").is_null ()
                    ? std::optional<actor_ref_snapshot_t> (
                        json.at ("actor").get<actor_ref_snapshot_t> ())
                    : std::nullopt;
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

inline void to_json (nlohmann::json &json, const assign_delivery_msg_t &value)
{
    json = {{"deliveryId", value.delivery_id},
            {"customerId", value.customer_id},
            {"pickupAddress", value.pickup_address},
            {"dropoffAddress", value.dropoff_address}};
}

inline void from_json (const nlohmann::json &json, assign_delivery_msg_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.customer_id = json_string (json, "customerId", "customer_id");
    value.pickup_address = json_string (json, "pickupAddress", "pickup_address");
    value.dropoff_address = json_string (json, "dropoffAddress", "dropoff_address");
}

inline void to_json (nlohmann::json &json, const offer_delivery_msg_t &value)
{
    json = {{"courierId", value.courier_id},
            {"deliveryId", value.delivery_id},
            {"attempt", value.attempt},
            {"pickupAddress", value.pickup_address},
            {"dropoffAddress", value.dropoff_address}};
}

inline void from_json (const nlohmann::json &json, offer_delivery_msg_t &value)
{
    value.courier_id = json_string (json, "courierId", "courier_id");
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.attempt = json.value ("attempt", 0);
    value.pickup_address = json_string (json, "pickupAddress", "pickup_address");
    value.dropoff_address = json_string (json, "dropoffAddress", "dropoff_address");
}

inline void to_json (nlohmann::json &json, const offer_delivery_result_msg_t &value)
{
    json = {{"deliveryId", value.delivery_id},
            {"courierId", value.courier_id},
            {"attempt", value.attempt},
            {"accepted", value.accepted},
            {"reason", value.reason}};
}

inline void from_json (const nlohmann::json &json, offer_delivery_result_msg_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.courier_id = json_string (json, "courierId", "courier_id");
    value.attempt = json.value ("attempt", 0);
    value.accepted = json.value ("accepted", false);
    value.reason = json.value ("reason", std::string{});
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

inline void to_json (nlohmann::json &json, const delivery_status_changed_req_t &value)
{
    json = {{"deliveryId", value.delivery_id},
            {"customerId", value.customer_id},
            {"status", value.status},
            {"courierId", value.courier_id},
            {"occurredAt", value.occurred_at}};
}

inline void from_json (const nlohmann::json &json, delivery_status_changed_req_t &value)
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

inline void to_json (nlohmann::json &json, const delivery_status_changed_res_t &value)
{
    json = {{"deliveryId", value.delivery_id}, {"status", value.status}};
}

inline void from_json (const nlohmann::json &json, delivery_status_changed_res_t &value)
{
    value.delivery_id = json_string (json, "deliveryId", "delivery_id");
    value.status = json.value ("status", "");
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

} // namespace zlink::samples::deliverydispatch
