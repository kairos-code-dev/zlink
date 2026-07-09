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
    bool run (const std::string &api_http_url,
              const std::string &customer_stream_endpoint,
              const std::string &courier_stream_endpoint)
    {
        try {
            zlink::stream_connector::connector_options_t connector_options;
            connector_options.endpoint = customer_stream_endpoint;
            connector_options.connect_timeout = std::chrono::milliseconds (3000);
            connector_options.request_timeout = std::chrono::seconds (12);
            connector_options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;
            auto core_customer =
              zlink::stream_connector::connector_factory_t::create (connector_options);
            auto customer = zlink::stream_e2e_client::use (core_customer);
            auto customer_connected = customer.connect ().submit ();
            ensure (static_cast<bool> (customer_connected), "customer stream connect failed");

            connector_options.endpoint = courier_stream_endpoint;
            auto core_courier_a =
              zlink::stream_connector::connector_factory_t::create (connector_options);
            auto courier_a = zlink::stream_e2e_client::use (core_courier_a);
            auto courier_a_connected = courier_a.connect ().submit ();
            ensure (static_cast<bool> (courier_a_connected), "courier-a stream connect failed");

            auto core_courier_b =
              zlink::stream_connector::connector_factory_t::create (connector_options);
            auto courier_b = zlink::stream_e2e_client::use (core_courier_b);
            auto courier_b_connected = courier_b.connect ().submit ();
            ensure (static_cast<bool> (courier_b_connected), "courier-b stream connect failed");

            bind_courier (courier_a, "courier-a");
            bind_courier (courier_b, "courier-b");

            auto http = zlink::http_client::client_t::create (api_http_url)
                          .timeout (std::chrono::seconds (12))
                          .build ();
            run_successful_delivery (http, customer, courier_a);
            run_reassigned_delivery (http, customer, courier_a, courier_b);
            assert_server_evidence (http);
            return true;
        }
        catch (const std::exception &error) {
            std::cerr << "deliverydispatch scenario failed: " << error.what () << "\n";
            return false;
        }
    }

  private:
    using connector_t = zlink::stream_e2e_client::coroutine_connector_t;

    static void bind_courier (connector_t &courier, const std::string &courier_id)
    {
        const auto bound = courier.request (bind_courier_session_req_t{courier_id})
                             .async<bind_courier_session_res_t> ()
                             .result ();
        if (!bound) {
            throw std::runtime_error (bound.error () ? bound.error ()->message
                                                     : "courier bind failed");
        }
        ensure (bound.value ().courier_id == courier_id, "courier bind id mismatch");
    }

    static void run_successful_delivery (zlink::http_client::client_t &http,
                                         connector_t &customer,
                                         connector_t &courier)
    {
        const std::string delivery_id = "delivery-success";
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
        auto offer = wait_offer (courier, delivery_id, "courier-a");
        auto assigned = wait_status (customer, delivery_id, delivery_status_t::assigned);
        auto accepted = wait_status (customer, delivery_id, delivery_status_t::accepted);
        auto picked_up = wait_status (customer, delivery_id, delivery_status_t::picked_up);
        auto delivered = wait_status (customer, delivery_id, delivery_status_t::delivered);
        std::this_thread::sleep_for (std::chrono::milliseconds (200));

        auto created_future = std::async (std::launch::async, [&http, delivery_id] {
            return http.post ("/deliveries")
              .body (create_delivery_req_t{
                delivery_id, "customer-1", "Kitchen 12", "Customer Lobby"})
              .fetch<create_delivery_res_t> ();
        });
        const auto courier_offer = offer.get ();
        send_decision (courier, courier_offer.delivery_id, courier_offer.courier_id, true);
        auto created = created_future.get ();
        ensure (created.delivery_id == delivery_id, "delivery-success create failed");
        ensure (assigned.get ().courier_id == "courier-a", "assigned courier mismatch");
        ensure (accepted.get ().courier_id == "courier-a", "accepted courier mismatch");
        ensure (picked_up.get ().courier_id == "courier-a", "picked-up courier mismatch");
        ensure (delivered.get ().courier_id == "courier-a", "delivered courier mismatch");
    }

    static void run_reassigned_delivery (zlink::http_client::client_t &http,
                                         connector_t &customer,
                                         connector_t &courier_a,
                                         connector_t &courier_b)
    {
        const std::string delivery_id = "delivery-reassign";
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
        auto first_offer = wait_offer (courier_a, delivery_id, "courier-a");
        auto second_offer = wait_offer (courier_b, delivery_id, "courier-b");
        auto assigned = wait_status (customer, delivery_id, delivery_status_t::assigned);
        auto reassigned = wait_status (customer, delivery_id, delivery_status_t::reassigned);
        auto accepted = wait_status (customer, delivery_id, delivery_status_t::accepted);
        auto delivered = wait_status (customer, delivery_id, delivery_status_t::delivered);
        std::this_thread::sleep_for (std::chrono::milliseconds (200));

        auto created_future = std::async (std::launch::async, [&http, delivery_id] {
            return http.post ("/deliveries")
              .body (create_delivery_req_t{
                delivery_id, "customer-1", "Kitchen 12", "Customer Lobby"})
              .fetch<create_delivery_res_t> ();
        });
        (void) first_offer.get ();
        const auto accepted_offer = second_offer.get ();
        send_decision (courier_b, accepted_offer.delivery_id, accepted_offer.courier_id, true);
        auto created = created_future.get ();
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
    wait_status (connector_t &customer, const std::string &delivery_id, const std::string &status)
    {
        return customer.wait_for<delivery_status_notify_t> ()
          .where ([delivery_id, status] (const delivery_status_notify_t &message) {
              return message.delivery_id == delivery_id && message.status == status;
          })
          .timeout (std::chrono::seconds (12))
          .to_future ("delivery status wait failed");
    }

    static std::future<offer_delivery_notify_t>
    wait_offer (connector_t &courier, const std::string &delivery_id, const std::string &courier_id)
    {
        return courier.wait_for<offer_delivery_notify_t> ()
          .where ([delivery_id, courier_id] (const offer_delivery_notify_t &message) {
              return message.delivery_id == delivery_id && message.courier_id == courier_id;
          })
          .timeout (std::chrono::seconds (12))
          .to_future ("courier offer wait failed");
    }

    static void send_decision (connector_t &courier,
                               const std::string &delivery_id,
                               const std::string &courier_id,
                               bool accepted)
    {
        courier.send (courier_decision_msg_t{delivery_id, courier_id, accepted, ""}).submit ();
    }

    static void ensure (bool condition, const char *message)
    {
        if (!condition) {
            throw std::runtime_error (message);
        }
    }
};

} // namespace zlink::samples::deliverydispatch
