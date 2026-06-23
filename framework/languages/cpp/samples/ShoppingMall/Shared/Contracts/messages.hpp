/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace zlink::samples::shoppingmall
{

struct order_status_t
{
    static constexpr const char *created = "Created";
    static constexpr const char *confirmed = "Confirmed";
    static constexpr const char *failed = "Failed";
};

struct order_state_t
{
    static constexpr const char *packet_name = "OrderStateNotify";
    std::string order_id;
    std::string status;
    std::string shipping_address_id;
    std::string reservation_id;
    std::string payment_id;
    std::string reason;
    double amount{0};
    std::string currency;
    long long updated_at_unix_ms{0};
};

struct start_order_req_t
{
    static constexpr const char *packet_name = "StartOrderReq";
    std::string cart_id;
    std::string shipping_address_id;
    std::string payment_method_id;
    std::string idempotency_key;
};

struct start_order_res_t
{
    std::string order_id;
    std::string status;
};

struct get_order_state_req_t
{
    static constexpr const char *packet_name = "GetOrderStateReq";
    std::string order_id;
};

struct delete_order_projection_req_t
{
    static constexpr const char *packet_name = "DeleteOrderProjectionReq";
    std::string order_id;
};

struct get_order_state_res_t
{
    order_state_t state;
};

struct continue_order_workflow_req_t
{
    static constexpr const char *packet_name = "ContinueOrderWorkflowReq";
    std::string order_id;
};

struct continue_order_workflow_res_t
{
    order_state_t state;
};

struct rebuild_order_projection_req_t
{
    static constexpr const char *packet_name = "RebuildOrderProjectionReq";
    std::string order_id;
};

struct rebuild_order_projection_res_t
{
    order_state_t state;
};

struct seed_pending_idempotency_req_t
{
    static constexpr const char *packet_name = "SeedPendingIdempotencyReq";
    std::string idempotency_key;
    std::string order_id;
    std::string owner_instance_id;
};

struct server_assertion_req_t
{
    static constexpr const char *packet_name = "ServerAssertionReq";
    std::string successful_order_id;
    std::string pending_recovered_order_id;
    std::string inventory_failure_order_id;
    std::string payment_failure_order_id;
    std::string scale_out_order_id;
};

struct server_assertion_res_t
{
    bool passed{false};
    std::vector<std::string> evidence;
};

inline void to_json (nlohmann::json &json, const order_state_t &value)
{
    json = nlohmann::json{{"orderId", value.order_id},
                          {"status", value.status},
                          {"shippingAddressId", value.shipping_address_id},
                          {"reservationId", value.reservation_id},
                          {"paymentId", value.payment_id},
                          {"reason", value.reason},
                          {"amount", value.amount},
                          {"currency", value.currency},
                          {"updatedAtUnixMs", value.updated_at_unix_ms}};
}

inline void from_json (const nlohmann::json &json, order_state_t &value)
{
    value.order_id = json.value ("orderId", "");
    value.status = json.value ("status", "");
    value.shipping_address_id = json.value ("shippingAddressId", "");
    value.reservation_id = json.value ("reservationId", "");
    value.payment_id = json.value ("paymentId", "");
    value.reason = json.value ("reason", "");
    value.amount = json.value ("amount", 0.0);
    value.currency = json.value ("currency", "");
    value.updated_at_unix_ms = json.value ("updatedAtUnixMs", 0LL);
}

inline void to_json (nlohmann::json &json, const start_order_req_t &value)
{
    json = nlohmann::json{{"cartId", value.cart_id},
                          {"shippingAddressId", value.shipping_address_id},
                          {"paymentMethodId", value.payment_method_id},
                          {"idempotencyKey", value.idempotency_key}};
}

inline void from_json (const nlohmann::json &json, start_order_req_t &value)
{
    value.cart_id = json.value ("cartId", "");
    value.shipping_address_id = json.value ("shippingAddressId", "");
    value.payment_method_id = json.value ("paymentMethodId", "");
    value.idempotency_key = json.value ("idempotencyKey", "");
}

inline void to_json (nlohmann::json &json, const start_order_res_t &value)
{
    json = nlohmann::json{{"orderId", value.order_id}, {"status", value.status}};
}

inline void from_json (const nlohmann::json &json, start_order_res_t &value)
{
    value.order_id = json.value ("orderId", "");
    value.status = json.value ("status", "");
}

inline void to_json (nlohmann::json &json, const get_order_state_req_t &value)
{
    json = nlohmann::json{{"orderId", value.order_id}};
}

inline void from_json (const nlohmann::json &json, get_order_state_req_t &value)
{
    value.order_id = json.value ("orderId", "");
}

inline void to_json (nlohmann::json &json, const delete_order_projection_req_t &value)
{
    json = nlohmann::json{{"orderId", value.order_id}};
}

inline void from_json (const nlohmann::json &json, delete_order_projection_req_t &value)
{
    value.order_id = json.value ("orderId", "");
}

inline void to_json (nlohmann::json &json, const get_order_state_res_t &value)
{
    json = nlohmann::json{{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, get_order_state_res_t &value)
{
    value.state = json.value ("state", order_state_t{});
}

inline void to_json (nlohmann::json &json, const continue_order_workflow_req_t &value)
{
    json = nlohmann::json{{"orderId", value.order_id}};
}

inline void from_json (const nlohmann::json &json, continue_order_workflow_req_t &value)
{
    value.order_id = json.value ("orderId", "");
}

inline void to_json (nlohmann::json &json, const continue_order_workflow_res_t &value)
{
    json = nlohmann::json{{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, continue_order_workflow_res_t &value)
{
    value.state = json.value ("state", order_state_t{});
}

inline void to_json (nlohmann::json &json, const rebuild_order_projection_req_t &value)
{
    json = nlohmann::json{{"orderId", value.order_id}};
}

inline void from_json (const nlohmann::json &json, rebuild_order_projection_req_t &value)
{
    value.order_id = json.value ("orderId", "");
}

inline void to_json (nlohmann::json &json, const rebuild_order_projection_res_t &value)
{
    json = nlohmann::json{{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, rebuild_order_projection_res_t &value)
{
    value.state = json.value ("state", order_state_t{});
}

inline void to_json (nlohmann::json &json, const seed_pending_idempotency_req_t &value)
{
    json = nlohmann::json{{"idempotencyKey", value.idempotency_key},
                          {"orderId", value.order_id},
                          {"ownerInstanceId", value.owner_instance_id}};
}

inline void from_json (const nlohmann::json &json, seed_pending_idempotency_req_t &value)
{
    value.idempotency_key = json.value ("idempotencyKey", "");
    value.order_id = json.value ("orderId", "");
    value.owner_instance_id = json.value ("ownerInstanceId", "");
}

inline void to_json (nlohmann::json &json, const server_assertion_req_t &value)
{
    json = nlohmann::json{{"successfulOrderId", value.successful_order_id},
                          {"pendingRecoveredOrderId", value.pending_recovered_order_id},
                          {"inventoryFailureOrderId", value.inventory_failure_order_id},
                          {"paymentFailureOrderId", value.payment_failure_order_id},
                          {"scaleOutOrderId", value.scale_out_order_id}};
}

inline void from_json (const nlohmann::json &json, server_assertion_req_t &value)
{
    value.successful_order_id = json.value ("successfulOrderId", "");
    value.pending_recovered_order_id = json.value ("pendingRecoveredOrderId", "");
    value.inventory_failure_order_id = json.value ("inventoryFailureOrderId", "");
    value.payment_failure_order_id = json.value ("paymentFailureOrderId", "");
    value.scale_out_order_id = json.value ("scaleOutOrderId", "");
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

} // namespace zlink::samples::shoppingmall
