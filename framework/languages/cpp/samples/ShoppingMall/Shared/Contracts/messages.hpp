/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace zlink::samples::shoppingmall
{

struct order_status_t
{
    static constexpr const char *created = "Created";
    static constexpr const char *inventory_reserved = "InventoryReserved";
    static constexpr const char *payment_authorized = "PaymentAuthorized";
    static constexpr const char *confirmed = "Confirmed";
    static constexpr const char *failed = "Failed";
};

struct order_line_input_t
{
    std::string sku;
    int quantity{};
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
    static constexpr const char *packet_name = "StartOrderRes";
    std::string order_id;
    std::string status;
};

struct get_order_state_req_t
{
    static constexpr const char *packet_name = "GetOrderStateReq";
    std::string order_id;
};

struct order_state_t
{
    std::string order_id;
    std::string status;
    std::string shipping_address_id;
    std::string reservation_id;
    std::string payment_id;
    std::string reason;
    double amount{};
    std::string currency;
    std::int64_t updated_at_unix_ms{};
};

struct get_order_state_res_t
{
    static constexpr const char *packet_name = "GetOrderStateRes";
    order_state_t state;
};

struct start_order_workflow_req_t
{
    static constexpr const char *packet_name = "StartOrderWorkflowReq";
    std::string order_id;
    std::string cart_id;
    std::string shipping_address_id;
    std::string payment_method_id;
    std::string idempotency_key;
    std::vector<order_line_input_t> lines;
    double amount{};
    std::string currency;
};

struct start_order_workflow_res_t
{
    static constexpr const char *packet_name = "StartOrderWorkflowRes";
    order_state_t state;
};

struct continue_order_workflow_req_t
{
    static constexpr const char *packet_name = "ContinueOrderWorkflowReq";
    std::string order_id;
};

struct continue_order_workflow_res_t
{
    static constexpr const char *packet_name = "ContinueOrderWorkflowRes";
    order_state_t state;
};

struct rebuild_order_projection_req_t
{
    static constexpr const char *packet_name = "RebuildOrderProjectionReq";
    std::string order_id;
};

struct rebuild_order_projection_res_t
{
    static constexpr const char *packet_name = "RebuildOrderProjectionRes";
    order_state_t state;
};

struct ensure_order_workflow_spot_req_t
{
    static constexpr const char *packet_name = "EnsureOrderWorkflowSpotReq";
    std::string order_id;
};

struct pending_mapping_req_t
{
    static constexpr const char *packet_name = "PendingMappingReq";
    std::string idempotency_key;
    std::string order_id;
    std::string owner_instance_id;
};

struct delete_projection_req_t
{
    static constexpr const char *packet_name = "DeleteProjectionReq";
    std::string order_id;
};

struct ok_res_t
{
    static constexpr const char *packet_name = "OkRes";
    bool ok{true};
};

struct server_assertion_req_t
{
    static constexpr const char *packet_name = "ServerAssertionReq";
    std::string successful_order_id;
    std::string pending_recovered_order_id;
    std::string concurrent_order_id;
    std::string resumed_order_id;
    std::string inventory_failure_order_id;
    std::string payment_failure_order_id;
    std::string scale_out_order_id;
};

struct server_assertion_res_t
{
    static constexpr const char *packet_name = "ServerAssertionRes";
    bool passed{};
    std::vector<std::string> evidence;
};

inline std::string json_string (const nlohmann::json &json,
                                const char *camel,
                                const char *snake)
{
    return json.contains (camel) ? json.value (camel, std::string{})
                                 : json.value (snake, std::string{});
}

inline std::string json_nullable_string (const nlohmann::json &json,
                                         const char *camel,
                                         const char *snake)
{
    const auto read = [&json] (const char *name) {
        if (!json.contains (name) || json[name].is_null ()) return std::string{};
        return json.value (name, std::string{});
    };
    return json.contains (camel) ? read (camel) : read (snake);
}

inline void to_json (nlohmann::json &json, const order_line_input_t &value)
{
    json = {{"sku", value.sku}, {"quantity", value.quantity}};
}

inline void from_json (const nlohmann::json &json, order_line_input_t &value)
{
    value.sku = json.value ("sku", "");
    value.quantity = json.value ("quantity", 0);
}

inline void to_json (nlohmann::json &json, const start_order_req_t &value)
{
    json = {{"cartId", value.cart_id},
            {"shippingAddressId", value.shipping_address_id},
            {"paymentMethodId", value.payment_method_id},
            {"idempotencyKey", value.idempotency_key}};
}

inline void from_json (const nlohmann::json &json, start_order_req_t &value)
{
    value.cart_id = json_string (json, "cartId", "cart_id");
    value.shipping_address_id = json_string (json, "shippingAddressId", "shipping_address_id");
    value.payment_method_id = json_string (json, "paymentMethodId", "payment_method_id");
    value.idempotency_key = json_string (json, "idempotencyKey", "idempotency_key");
}

inline void to_json (nlohmann::json &json, const start_order_res_t &value)
{
    json = {{"orderId", value.order_id}, {"status", value.status}};
}

inline void from_json (const nlohmann::json &json, start_order_res_t &value)
{
    value.order_id = json_string (json, "orderId", "order_id");
    value.status = json.value ("status", "");
}

inline void to_json (nlohmann::json &json, const get_order_state_req_t &value)
{
    json = {{"orderId", value.order_id}};
}

inline void from_json (const nlohmann::json &json, get_order_state_req_t &value)
{
    value.order_id = json_string (json, "orderId", "order_id");
}

inline void to_json (nlohmann::json &json, const order_state_t &value)
{
    json = {{"orderId", value.order_id},
            {"status", value.status},
            {"shippingAddressId", value.shipping_address_id},
            {"reservationId", value.reservation_id.empty () ? nullptr : nlohmann::json (value.reservation_id)},
            {"paymentId", value.payment_id.empty () ? nullptr : nlohmann::json (value.payment_id)},
            {"reason", value.reason.empty () ? nullptr : nlohmann::json (value.reason)},
            {"amount", value.amount},
            {"currency", value.currency},
            {"updatedAtUnixMs", value.updated_at_unix_ms}};
}

inline void from_json (const nlohmann::json &json, order_state_t &value)
{
    value.order_id = json_string (json, "orderId", "order_id");
    value.status = json.value ("status", "");
    value.shipping_address_id = json_string (json, "shippingAddressId", "shipping_address_id");
    value.reservation_id = json_nullable_string (json, "reservationId", "reservation_id");
    value.payment_id = json_nullable_string (json, "paymentId", "payment_id");
    value.reason = json_nullable_string (json, "reason", "reason");
    value.amount = json.value ("amount", 0.0);
    value.currency = json.value ("currency", "");
    value.updated_at_unix_ms = json.value ("updatedAtUnixMs", std::int64_t{0});
}

inline void to_json (nlohmann::json &json, const get_order_state_res_t &value)
{
    json = {{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, get_order_state_res_t &value)
{
    value.state = json.value ("state", order_state_t{});
}

inline void to_json (nlohmann::json &json, const start_order_workflow_req_t &value)
{
    json = {{"orderId", value.order_id},
            {"cartId", value.cart_id},
            {"shippingAddressId", value.shipping_address_id},
            {"paymentMethodId", value.payment_method_id},
            {"idempotencyKey", value.idempotency_key},
            {"lines", value.lines},
            {"amount", value.amount},
            {"currency", value.currency}};
}

inline void from_json (const nlohmann::json &json, start_order_workflow_req_t &value)
{
    value.order_id = json_string (json, "orderId", "order_id");
    value.cart_id = json_string (json, "cartId", "cart_id");
    value.shipping_address_id = json_string (json, "shippingAddressId", "shipping_address_id");
    value.payment_method_id = json_string (json, "paymentMethodId", "payment_method_id");
    value.idempotency_key = json_string (json, "idempotencyKey", "idempotency_key");
    value.lines = json.value ("lines", std::vector<order_line_input_t>{});
    value.amount = json.value ("amount", 0.0);
    value.currency = json.value ("currency", "");
}

inline void to_json (nlohmann::json &json, const start_order_workflow_res_t &value)
{
    json = {{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, start_order_workflow_res_t &value)
{
    value.state = json.value ("state", order_state_t{});
}

inline void to_json (nlohmann::json &json, const continue_order_workflow_req_t &value)
{
    json = {{"orderId", value.order_id}};
}

inline void from_json (const nlohmann::json &json, continue_order_workflow_req_t &value)
{
    value.order_id = json_string (json, "orderId", "order_id");
}

inline void to_json (nlohmann::json &json, const continue_order_workflow_res_t &value)
{
    json = {{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, continue_order_workflow_res_t &value)
{
    value.state = json.value ("state", order_state_t{});
}

inline void to_json (nlohmann::json &json, const rebuild_order_projection_req_t &value)
{
    json = {{"orderId", value.order_id}};
}

inline void from_json (const nlohmann::json &json, rebuild_order_projection_req_t &value)
{
    value.order_id = json_string (json, "orderId", "order_id");
}

inline void to_json (nlohmann::json &json, const rebuild_order_projection_res_t &value)
{
    json = {{"state", value.state}};
}

inline void from_json (const nlohmann::json &json, rebuild_order_projection_res_t &value)
{
    value.state = json.value ("state", order_state_t{});
}

inline void to_json (nlohmann::json &json, const ensure_order_workflow_spot_req_t &value)
{
    json = {{"orderId", value.order_id}};
}

inline void from_json (const nlohmann::json &json, ensure_order_workflow_spot_req_t &value)
{
    value.order_id = json_string (json, "orderId", "order_id");
}

inline void to_json (nlohmann::json &json, const pending_mapping_req_t &value)
{
    json = {{"idempotencyKey", value.idempotency_key},
            {"orderId", value.order_id},
            {"ownerInstanceId", value.owner_instance_id}};
}

inline void from_json (const nlohmann::json &json, pending_mapping_req_t &value)
{
    value.idempotency_key = json_string (json, "idempotencyKey", "idempotency_key");
    value.order_id = json_string (json, "orderId", "order_id");
    value.owner_instance_id = json_string (json, "ownerInstanceId", "owner_instance_id");
}

inline void to_json (nlohmann::json &json, const delete_projection_req_t &value)
{
    json = {{"orderId", value.order_id}};
}

inline void from_json (const nlohmann::json &json, delete_projection_req_t &value)
{
    value.order_id = json_string (json, "orderId", "order_id");
}

inline void to_json (nlohmann::json &json, const ok_res_t &value)
{
    json = {{"ok", value.ok}};
}

inline void from_json (const nlohmann::json &json, ok_res_t &value)
{
    value.ok = json.value ("ok", false);
}

inline void to_json (nlohmann::json &json, const server_assertion_req_t &value)
{
    json = {{"successfulOrderId", value.successful_order_id},
            {"pendingRecoveredOrderId", value.pending_recovered_order_id},
            {"concurrentOrderId", value.concurrent_order_id},
            {"resumedOrderId", value.resumed_order_id},
            {"inventoryFailureOrderId", value.inventory_failure_order_id},
            {"paymentFailureOrderId", value.payment_failure_order_id},
            {"scaleOutOrderId", value.scale_out_order_id}};
}

inline void from_json (const nlohmann::json &json, server_assertion_req_t &value)
{
    value.successful_order_id = json_string (json, "successfulOrderId", "successful_order_id");
    value.pending_recovered_order_id =
      json_string (json, "pendingRecoveredOrderId", "pending_recovered_order_id");
    value.concurrent_order_id = json_string (json, "concurrentOrderId", "concurrent_order_id");
    value.resumed_order_id = json_string (json, "resumedOrderId", "resumed_order_id");
    value.inventory_failure_order_id =
      json_string (json, "inventoryFailureOrderId", "inventory_failure_order_id");
    value.payment_failure_order_id =
      json_string (json, "paymentFailureOrderId", "payment_failure_order_id");
    value.scale_out_order_id = json_string (json, "scaleOutOrderId", "scale_out_order_id");
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

} // namespace zlink::samples::shoppingmall
