/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

#include <stdexcept>
#include <utility>

namespace zlink::samples::shoppingmallcheckout
{

class shopping_mall_checkout_client_scenario_t
{
  public:
    bool run (zlink::framework::channel_client_t &channels)
    {
        auto checkout = request<start_checkout_req_t> (channels, {"customer-1"});
        if (checkout.status != checkout_status_t::cart_created) {
            return false;
        }
        checkout = request<authorize_payment_req_t> (channels, {checkout.checkout_id});
        if (checkout.status != checkout_status_t::payment_authorized) {
            return false;
        }
        checkout = request<reserve_inventory_req_t> (channels, {checkout.checkout_id});
        if (checkout.status != checkout_status_t::inventory_reserved) {
            return false;
        }
        checkout = request<confirm_order_req_t> (channels, {checkout.checkout_id});
        return checkout.status == checkout_status_t::order_confirmed;
    }

  private:
    template <typename TRequest>
    static checkout_state_t request (zlink::framework::channel_client_t &channels,
                                     TRequest request)
    {
        auto result =
          channels.request_to_channel ("shoppingmallcheckout.checkout", std::move (request))
            .template async<checkout_state_t> ()
            .result ();
        if (!result) {
            const auto *error = result.error ();
            throw std::runtime_error (error == nullptr ? "ShoppingMallCheckout request failed"
                                                       : error->what ());
        }
        return result.value ();
    }
};

} // namespace zlink::samples::shoppingmallcheckout
