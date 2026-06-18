/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>

#include <nlohmann/json.hpp>
#include <string>

namespace zlink::samples::shoppingmallcheckout
{

struct checkout_status_t
{
    static constexpr const char *cart_created = "CartCreated";
    static constexpr const char *payment_authorized = "PaymentAuthorized";
    static constexpr const char *inventory_reserved = "InventoryReserved";
    static constexpr const char *order_confirmed = "OrderConfirmed";
};

struct checkout_state_t
{
    static constexpr const char *packet_name = "CheckoutStateNotify";
    std::string checkout_id;
    std::string customer_id;
    std::string status;
};

struct start_checkout_req_t
{
    static constexpr const char *packet_name = "StartCheckoutReq";
    std::string customer_id;
};

struct authorize_payment_req_t
{
    static constexpr const char *packet_name = "AuthorizePaymentReq";
    std::string checkout_id;
};

struct reserve_inventory_req_t
{
    static constexpr const char *packet_name = "ReserveInventoryReq";
    std::string checkout_id;
};

struct confirm_order_req_t
{
    static constexpr const char *packet_name = "ConfirmOrderReq";
    std::string checkout_id;
};

inline void to_json (nlohmann::json &json, const checkout_state_t &value)
{
    json = nlohmann::json{{"checkoutId", value.checkout_id},
                          {"customerId", value.customer_id},
                          {"status", value.status}};
}

inline void from_json (const nlohmann::json &json, checkout_state_t &value)
{
    value.checkout_id = json.value ("checkoutId", "");
    value.customer_id = json.value ("customerId", "");
    value.status = json.value ("status", "");
}

inline void to_json (nlohmann::json &json, const start_checkout_req_t &value)
{
    json = nlohmann::json{{"customerId", value.customer_id}};
}

inline void from_json (const nlohmann::json &json, start_checkout_req_t &value)
{
    value.customer_id = json.value ("customerId", "");
}

inline void to_json (nlohmann::json &json, const authorize_payment_req_t &value)
{
    json = nlohmann::json{{"checkoutId", value.checkout_id}};
}

inline void from_json (const nlohmann::json &json, authorize_payment_req_t &value)
{
    value.checkout_id = json.value ("checkoutId", "");
}

inline void to_json (nlohmann::json &json, const reserve_inventory_req_t &value)
{
    json = nlohmann::json{{"checkoutId", value.checkout_id}};
}

inline void from_json (const nlohmann::json &json, reserve_inventory_req_t &value)
{
    value.checkout_id = json.value ("checkoutId", "");
}

inline void to_json (nlohmann::json &json, const confirm_order_req_t &value)
{
    json = nlohmann::json{{"checkoutId", value.checkout_id}};
}

inline void from_json (const nlohmann::json &json, confirm_order_req_t &value)
{
    value.checkout_id = json.value ("checkoutId", "");
}

template <typename T> inline zlink::message_t to_stream_payload (const T &value)
{
    return zlink::message_t::from (nlohmann::json (value).dump ());
}

template <typename T> inline void from_stream_payload (const zlink::message_t &payload, T &value)
{
    value = nlohmann::json::parse (payload.to_string ()).template get<T> ();
}

} // namespace zlink::samples::shoppingmallcheckout
