/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <zlink/http_client.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

namespace zlink::samples::shoppingmall
{

class shopping_mall_client_scenario_t
{
  public:
    bool run (const std::string &api_url)
    {
        try {
            auto api = zlink::http_client::client_t::create (api_url)
                         .timeout (std::chrono::seconds (12))
                         .build ();
            const auto success =
              start (api, {"cart-success", "addr-home", "pm-ok", "order-success-001"});
            ensure (success.status == order_status_t::created, "success order start failed");
            const auto confirmed = get_state (api, success.order_id);
            ensure (confirmed.state.status == order_status_t::confirmed, "confirm failed");
            ensure (!confirmed.state.reservation_id.empty (), "reservation missing");
            ensure (!confirmed.state.payment_id.empty (), "payment missing");
            ensure (confirmed.state.amount == 120.0, "amount mismatch");
            ensure (confirmed.state.currency == "USD", "currency mismatch");

            const auto duplicate =
              start (api, {"cart-success", "addr-home", "pm-ok", "order-success-001"});
            ensure (duplicate.order_id == success.order_id, "idempotency duplicate failed");

            (void) api.post ("/self-check/idempotency/pending")
              .body (seed_pending_idempotency_req_t{
                "order-pending-001", "order-pending-0001", "api-a"})
              .fetch<start_order_res_t> ();
            const auto pending =
              start (api, {"cart-success", "addr-office", "pm-ok", "order-pending-001"});
            const auto pending_state = get_state (api, pending.order_id);
            ensure (pending.order_id == "order-pending-0001", "pending id mismatch");
            ensure (pending_state.state.status == order_status_t::confirmed,
                    "pending recovery failed");

            const auto inventory =
              start (api, {"cart-inventory-fail", "addr-home", "pm-ok", "order-inventory-001"});
            const auto inventory_failed = get_state (api, inventory.order_id);
            ensure (inventory_failed.state.status == order_status_t::failed,
                    "inventory failure status mismatch");
            ensure (inventory_failed.state.reason.find ("inventory") != std::string::npos,
                    "inventory failure reason missing");

            const auto payment =
              start (api, {"cart-payment-fail", "addr-home", "pm-decline", "order-payment-001"});
            const auto payment_failed = get_state (api, payment.order_id);
            ensure (payment_failed.state.status == order_status_t::failed,
                    "payment failure status mismatch");
            ensure (!payment_failed.state.reservation_id.empty (), "payment reservation missing");
            ensure (payment_failed.state.reason.find ("payment") != std::string::npos,
                    "payment failure reason missing");

            (void) api.post ("/self-check/projection/delete")
              .body (delete_order_projection_req_t{success.order_id})
              .fetch<get_order_state_res_t> ();
            const auto rebuilt = api.post ("/self-check/projection/rebuild")
                                   .body (rebuild_order_projection_req_t{success.order_id})
                                   .fetch<rebuild_order_projection_res_t> ();
            ensure (rebuilt.state.status == order_status_t::confirmed,
                    "projection rebuild failed");

            const auto scale =
              start (api, {"cart-success", "addr-office", "pm-ok", "order-scale-001"});
            const auto scale_confirmed = get_state (api, scale.order_id);
            ensure (scale_confirmed.state.status == order_status_t::confirmed,
                    "scale order confirm failed");

            const auto assertion = api.post ("/self-check/assert")
                                     .body (server_assertion_req_t{
                                       success.order_id, pending.order_id, inventory.order_id,
                                       payment.order_id, scale.order_id})
                                     .fetch<server_assertion_res_t> ();
            ensure (assertion.passed, "server evidence failed");
            std::cout << "shoppingmall-server-evidence=completed\n";
            return true;
        }
        catch (const std::exception &error) {
            std::cerr << "shoppingmall scenario failed: " << error.what () << "\n";
            return false;
        }
    }

  private:
    static start_order_res_t start (zlink::http_client::client_t &api, start_order_req_t request)
    {
        return api.post ("/orders/start").body (request).fetch<start_order_res_t> ();
    }

    static get_order_state_res_t get_state (zlink::http_client::client_t &api,
                                            const std::string &order_id)
    {
        return api.post ("/orders/get")
          .body (get_order_state_req_t{order_id})
          .fetch<get_order_state_res_t> ();
    }

    static void ensure (bool condition, const char *message)
    {
        if (!condition) {
            throw std::runtime_error (message);
        }
    }
};

} // namespace zlink::samples::shoppingmall
