/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

#include <iostream>
#include <stdexcept>

namespace zlink::samples::shoppingmall
{

class shopping_mall_client_scenario_t
{
  public:
    bool run (zlink::framework::channel_client_t &channels)
    {
        const auto success = start (channels, {"cart-success", "addr-home", "pm-ok",
                                               "order-success-001"});
        if (success.status != order_status_t::created) {
            return false;
        }
        const auto confirmed = get_state (channels, success.order_id);
        if (confirmed.state.status != order_status_t::confirmed ||
            confirmed.state.reservation_id.empty () ||
            confirmed.state.payment_id.empty () ||
            confirmed.state.amount != 120.0 ||
            confirmed.state.currency != "USD") {
            return false;
        }

        const auto duplicate = start (channels, {"cart-success", "addr-home", "pm-ok",
                                                 "order-success-001"});
        if (duplicate.order_id != success.order_id) {
            return false;
        }

        request<seed_pending_idempotency_req_t, start_order_res_t> (
          channels, {"order-pending-001", "order-pending-0001", "api-a"});
        const auto pending = start (channels, {"cart-success", "addr-office", "pm-ok",
                                               "order-pending-001"});
        const auto pending_recovered = get_state (channels, pending.order_id);
        if (pending.order_id != "order-pending-0001" ||
            pending_recovered.state.status != order_status_t::confirmed ||
            pending_recovered.state.shipping_address_id != "addr-office") {
            return false;
        }

        const auto inventory = start (channels, {"cart-inventory-fail", "addr-home", "pm-ok",
                                                 "order-inventory-001"});
        const auto inventory_failed = get_state (channels, inventory.order_id);
        if (inventory_failed.state.status != order_status_t::failed ||
            inventory_failed.state.reason.find ("inventory") == std::string::npos) {
            return false;
        }

        const auto payment = start (channels, {"cart-payment-fail", "addr-home", "pm-decline",
                                               "order-payment-001"});
        const auto payment_failed = get_state (channels, payment.order_id);
        if (payment_failed.state.status != order_status_t::failed ||
            payment_failed.state.reservation_id.empty () ||
            payment_failed.state.reason.find ("payment") == std::string::npos) {
            return false;
        }

        request<delete_order_projection_req_t, get_order_state_res_t> (channels,
                                                                       {success.order_id});
        const auto rebuilt =
          request<rebuild_order_projection_req_t, rebuild_order_projection_res_t> (
            channels, {success.order_id});
        if (rebuilt.state.status != order_status_t::confirmed) {
            return false;
        }

        const auto scale = start (channels, {"cart-success", "addr-office", "pm-ok",
                                             "order-scale-001"});
        const auto scale_confirmed = get_state (channels, scale.order_id);
        if (scale_confirmed.state.status != order_status_t::confirmed) {
            return false;
        }

        const auto assertion = request<server_assertion_req_t, server_assertion_res_t> (
          channels,
          {success.order_id, pending.order_id, inventory.order_id, payment.order_id,
           scale.order_id});
        if (!assertion.passed) {
            return false;
        }
        std::cout << "shoppingmall-server-evidence=completed\n";
        return true;
    }

  private:
    static start_order_res_t start (zlink::framework::channel_client_t &channels,
                                    start_order_req_t request)
    {
        return shopping_mall_client_scenario_t::request<start_order_req_t, start_order_res_t> (
          channels, std::move (request));
    }

    static get_order_state_res_t get_state (zlink::framework::channel_client_t &channels,
                                            const std::string &order_id)
    {
        return request<get_order_state_req_t, get_order_state_res_t> (channels, {order_id});
    }

    template <typename TRequest, typename TResponse>
    static TResponse request (zlink::framework::channel_client_t &channels, TRequest request)
    {
        auto result = channels.request_to_channel ("shoppingmall.workflow", std::move (request))
                        .template async<TResponse> ()
                        .result ();
        if (!result) {
            const auto *error = result.error ();
            throw std::runtime_error (error == nullptr ? "ShoppingMall request failed"
                                                       : error->what ());
        }
        return result.value ();
    }

};

} // namespace zlink::samples::shoppingmall
