/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <zlink/http_client.hpp>
#include <zlink/stream_connector.hpp>
#include <zlink/stream_e2e_client.hpp>
#include <zlink/stream_e2e_client/codecs/auto_codec.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace zlink::samples::deliverydispatch
{

class delivery_dispatch_client_scenario_t
{
  public:
    bool run (const std::string &api_http_url, const std::string &stream_endpoint)
    {
        try {
            zlink::stream_connector::connector_options_t connector_options;
            connector_options.endpoint = stream_endpoint;
            connector_options.connect_timeout = std::chrono::seconds (5);
            connector_options.request_timeout = std::chrono::seconds (12);
            connector_options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;
            auto core_customer =
              zlink::stream_connector::connector_factory_t::create (connector_options);
            auto customer = zlink::stream_e2e_client::use (core_customer);
            auto connected = customer.connect ().submit ();
            ensure (static_cast<bool> (connected), "customer stream connect failed");

            auto http = zlink::http_client::client_t::create (api_http_url)
                          .timeout (std::chrono::seconds (12))
                          .build ();
            run_successful_delivery (http, customer);
            run_reassigned_delivery (http, customer);
            assert_server_evidence (http);
            return true;
        }
        catch (const std::exception &error) {
            std::cerr << "deliverydispatch scenario failed: " << error.what () << "\n";
            return false;
        }
    }

  private:
    using customer_t = zlink::stream_e2e_client::coroutine_connector_t;

    static void run_successful_delivery (zlink::http_client::client_t &http,
                                         customer_t &customer)
    {
        const std::string delivery_id = "delivery-success";
        auto assigned = wait_status (customer, delivery_id, delivery_status_t::assigned);
        auto accepted = wait_status (customer, delivery_id, delivery_status_t::accepted);
        auto picked_up = wait_status (customer, delivery_id, delivery_status_t::picked_up);
        auto delivered = wait_status (customer, delivery_id, delivery_status_t::delivered);

        const auto subscribed =
          customer.request (subscribe_delivery_req_t{delivery_id})
            .async<subscribe_delivery_res_t> ()
            .result ();
        if (!subscribed) {
            throw std::runtime_error (subscribed.error () ? subscribed.error ()->message
                                                          : "delivery-success subscription failed");
        }
        ensure (subscribed && subscribed.value ().delivery_id == delivery_id,
                "delivery-success subscription failed");
        std::this_thread::sleep_for (std::chrono::milliseconds (200));

        auto created = http.post ("/deliveries")
                         .body (create_delivery_req_t{
                           delivery_id, "customer-1", "Kitchen 12", "Customer Lobby"})
                         .fetch<create_delivery_res_t> ();
        ensure (created.delivery_id == delivery_id, "delivery-success create failed");
        ensure (assigned.get ().courier_id == "courier-a", "assigned courier mismatch");
        ensure (accepted.get ().courier_id == "courier-a", "accepted courier mismatch");
        ensure (picked_up.get ().courier_id == "courier-a", "picked-up courier mismatch");
        ensure (delivered.get ().courier_id == "courier-a", "delivered courier mismatch");
    }

    static void run_reassigned_delivery (zlink::http_client::client_t &http,
                                         customer_t &customer)
    {
        const std::string delivery_id = "delivery-reassign";
        auto assigned = wait_status (customer, delivery_id, delivery_status_t::assigned);
        auto reassigned = wait_status (customer, delivery_id, delivery_status_t::reassigned);
        auto accepted = wait_status (customer, delivery_id, delivery_status_t::accepted);
        auto delivered = wait_status (customer, delivery_id, delivery_status_t::delivered);

        const auto subscribed =
          customer.request (subscribe_delivery_req_t{delivery_id})
            .async<subscribe_delivery_res_t> ()
            .result ();
        if (!subscribed) {
            throw std::runtime_error (subscribed.error () ? subscribed.error ()->message
                                                          : "delivery-reassign subscription failed");
        }
        ensure (subscribed && subscribed.value ().delivery_id == delivery_id,
                "delivery-reassign subscription failed");
        std::this_thread::sleep_for (std::chrono::milliseconds (200));

        auto created = http.post ("/deliveries")
                         .body (create_delivery_req_t{
                           delivery_id, "customer-1", "Kitchen 12", "Customer Lobby"})
                         .fetch<create_delivery_res_t> ();
        ensure (created.delivery_id == delivery_id, "delivery-reassign create failed");
        ensure (assigned.get ().courier_id == "courier-a", "assigned courier mismatch");
        ensure (reassigned.get ().courier_id == "courier-b", "reassigned courier mismatch");
        ensure (accepted.get ().courier_id == "courier-b", "accepted courier mismatch");
        ensure (delivered.get ().courier_id == "courier-b", "delivered courier mismatch");
        std::cout << "deliverydispatch-reassignment=completed\n";
    }

    static void assert_server_evidence (zlink::http_client::client_t &http)
    {
        auto assertion = http.post ("/self-check/assert")
                           .body (server_assertion_req_t{"delivery-success", "delivery-reassign"})
                           .fetch<server_assertion_res_t> ();
        ensure (assertion.passed, "server evidence assertion failed");
        std::cout << "deliverydispatch-server-evidence=completed\n";
    }

    static std::future<delivery_status_notify_t>
    wait_status (customer_t &customer, const std::string &delivery_id, const std::string &status)
    {
        return customer.wait_for<delivery_status_notify_t> ()
          .where ([delivery_id, status] (const delivery_status_notify_t &message) {
              return message.delivery_id == delivery_id && message.status == status;
          })
          .timeout (std::chrono::seconds (12))
          .to_future ("delivery status wait failed");
    }

    static void ensure (bool condition, const char *message)
    {
        if (!condition) {
            throw std::runtime_error (message);
        }
    }
};

} // namespace zlink::samples::deliverydispatch
