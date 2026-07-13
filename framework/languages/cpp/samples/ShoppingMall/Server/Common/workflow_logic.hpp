/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "store.hpp"

#include <algorithm>

namespace zlink::samples::shoppingmall
{

inline void append_event (nlohmann::json &state,
                          const std::string &order_id,
                          const std::string &type)
{
    state["events"][order_id].push_back ({{"type", type}, {"createdAtUnixMs", now_unix_ms ()}});
}

inline order_state_t save_projection (nlohmann::json &state, order_state_t order)
{
    order.updated_at_unix_ms = now_unix_ms ();
    state["readModels"][order.order_id] = order;
    return order;
}

inline order_state_t require_projection (const nlohmann::json &state, const std::string &order_id)
{
    if (!state["readModels"].contains (order_id)) {
        throw std::runtime_error ("Order projection does not exist: " + order_id);
    }
    return state["readModels"][order_id].get<order_state_t> ();
}

inline order_state_t start_workflow (nlohmann::json &state,
                                     const start_order_workflow_req_t &request)
{
    const auto already_started =
      std::any_of (state["events"][request.order_id].begin (),
                   state["events"][request.order_id].end (),
                   [&] (const nlohmann::json &event) {
                       return event.value ("type", "") == "OrderStartedEvent";
                   });
    if (!already_started) {
        append_event (state, request.order_id, "OrderStartedEvent");
        save_projection (state,
                         order_state_t{request.order_id,
                                       order_status_t::created,
                                       request.shipping_address_id,
                                       "",
                                       "",
                                       "",
                                       request.amount,
                                       request.currency,
                                       now_unix_ms ()});
    }
    state["idempotency"][request.idempotency_key]["started"] = true;
    return require_projection (state, request.order_id);
}

inline order_state_t rebuild_projection (nlohmann::json &state, const std::string &order_id);

inline order_state_t continue_workflow (nlohmann::json &state, const std::string &order_id)
{
    auto current = state["readModels"].contains (order_id) ? require_projection (state, order_id)
                                                           : rebuild_projection (state, order_id);
    if (current.status == order_status_t::confirmed || current.status == order_status_t::failed)
        return current;

    if (current.status == order_status_t::created) {
        const auto inventory_ok = current.amount > 0.0 && current.amount < 1000.0;
        if (!inventory_ok) {
            append_event (state, order_id, "InventoryReservationFailedEvent");
            append_event (state, order_id, "OrderFailedEvent");
            current.status = order_status_t::failed;
            current.reason = "inventory unavailable for sku-rare";
            return save_projection (state, current);
        }
        append_event (state, order_id, "InventoryReservedEvent");
        current.status = order_status_t::inventory_reserved;
        current.reservation_id = "reservation-" + order_id;
        current = save_projection (state, current);
    }

    if (current.status == order_status_t::inventory_reserved) {
        const auto payment_method =
          state["orderPaymentMethods"].value (order_id, std::string ("pm-ok"));
        if (payment_method == "pm-decline") {
            append_event (state, order_id, "PaymentFailedEvent");
            append_event (state, order_id, "InventoryReleasedEvent");
            append_event (state, order_id, "OrderFailedEvent");
            state["paymentAttempts"][order_id] = "payment declined";
            state["releasedReservations"][current.reservation_id] = "payment declined";
            current.status = order_status_t::failed;
            current.reason = "payment declined";
            return save_projection (state, current);
        }
        append_event (state, order_id, "PaymentAuthorizedEvent");
        current.status = order_status_t::payment_authorized;
        current.payment_id = "payment-" + order_id;
        state["payments"][current.payment_id] = "120.00 USD";
        current = save_projection (state, current);
    }

    if (current.status == order_status_t::payment_authorized) {
        append_event (state, order_id, "OrderConfirmedEvent");
        current.status = order_status_t::confirmed;
        current = save_projection (state, current);
    }
    return current;
}

inline order_state_t rebuild_projection (nlohmann::json &state, const std::string &order_id)
{
    if (!state["events"].contains (order_id)) {
        throw std::runtime_error ("Order event stream does not exist: " + order_id);
    }
    auto current = order_state_t{order_id, "", "", "", "", "", 0.0, "", now_unix_ms ()};
    for (const auto &event : state["events"][order_id]) {
        const auto type = event.value ("type", "");
        if (type == "OrderStartedEvent") {
            const auto existing = state["readModels"].contains (order_id)
                                    ? state["readModels"][order_id].get<order_state_t> ()
                                    : current;
            current.status = order_status_t::created;
            current.shipping_address_id = existing.shipping_address_id;
            current.amount = existing.amount == 0.0 ? 120.0 : existing.amount;
            current.currency = existing.currency.empty () ? "USD" : existing.currency;
        } else if (type == "InventoryReservedEvent") {
            current.status = order_status_t::inventory_reserved;
            current.reservation_id = "reservation-" + order_id;
        } else if (type == "PaymentAuthorizedEvent") {
            current.status = order_status_t::payment_authorized;
            current.payment_id = "payment-" + order_id;
        } else if (type == "OrderConfirmedEvent") {
            current.status = order_status_t::confirmed;
        } else if (type == "InventoryReservationFailedEvent") {
            current.status = order_status_t::failed;
            current.reason = "inventory unavailable for sku-rare";
        } else if (type == "PaymentFailedEvent") {
            current.status = order_status_t::failed;
            current.reason = "payment declined";
        } else if (type == "OrderFailedEvent") {
            current.status = order_status_t::failed;
            if (current.reason.empty ()) current.reason = "order failed";
        }
    }
    return save_projection (state, current);
}

} // namespace zlink::samples::shoppingmall
