/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <zlink/framework.hpp>

#include <iostream>
#include <stdexcept>

namespace zlink::samples::deliverydispatch
{

using namespace framework;

class delivery_dispatch_client_scenario_t
{
  public:
    bool run (channel_client_t &channels)
    {
        if (!run_delivery (channels, "delivery-success")) {
            return false;
        }
        if (!run_delivery (channels, "delivery-reassign")) {
            return false;
        }
        const auto assertion = request<server_assertion_req_t, server_assertion_res_t> (
          channels, {"delivery-success", "delivery-reassign"});
        if (!assertion.passed) {
            return false;
        }
        std::cout << "deliverydispatch-reassignment=completed\n";
        std::cout << "deliverydispatch-server-evidence=completed\n";
        return true;
    }

  private:
    static bool run_delivery (channel_client_t &channels, const std::string &delivery_id)
    {
        const auto subscribed = request<subscribe_delivery_req_t, subscribe_delivery_accepted_t> (
          channels, {delivery_id});
        if (subscribed.delivery_id != delivery_id) {
            return false;
        }
        const auto created = request<create_delivery_req_t, delivery_created_t> (
          channels, {delivery_id, "customer-1", "Kitchen 12", "Customer Lobby"});
        return created.delivery_id == delivery_id;
    }

    template <typename TRequest, typename TResponse>
    static TResponse request (channel_client_t &channels, TRequest request)
    {
        auto result = channels.request_to_channel ("deliverydispatch.dispatch", std::move (request))
                        .template async<TResponse> ()
                        .result ();
        if (!result) {
            const auto *error = result.error ();
            throw std::runtime_error (error == nullptr ? "DeliveryDispatch request failed"
                                                       : error->what ());
        }
        return result.value ();
    }
};

} // namespace zlink::samples::deliverydispatch
