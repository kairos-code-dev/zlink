/* SPDX-License-Identifier: MPL-2.0 */

#include "../Shared/registration_codec_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace e2e = zlink::framework::e2e::registration_codec;

namespace
{

std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

void ensure (bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error (message);
    }
}

zlink::framework::encoded_payload_t encode_text (const std::string &prefix,
                                                 const std::string &value)
{
    return zlink::framework::encoded_payload_t::from_string (prefix + ":" + value);
}

std::string decode_text (const zlink::framework::encoded_payload_t &payload,
                         const std::string &prefix)
{
    const auto text = payload.to_string ();
    const auto expected = prefix + ":";
    if (text.rfind (expected, 0) != 0) {
        throw zlink::framework::framework_exception_t (
          zlink::framework::framework_error_kind_t::payload_decode_failed,
          "custom payload prefix mismatch");
    }
    return text.substr (expected.size ());
}

void add_json_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.add_json ()
      .add_json<e2e::echo_auto_t> ()
      .add_json<e2e::echo_auto_reply_t> ()
      .add_json<e2e::echo_auto_send_t> ()
      .add_json<e2e::echo_manual_t> ()
      .add_json<e2e::echo_manual_reply_t> ()
      .add_json<e2e::json_roundtrip_t> ()
      .add_json<e2e::json_roundtrip_reply_t> ()
      .add_json<e2e::scoped_lifecycle_req_t> ()
      .add_json<e2e::scoped_lifecycle_reply_t> ()
      .add_json<e2e::scoped_lifecycle_stats_req_t> ()
      .add_json<e2e::scoped_lifecycle_stats_reply_t> ()
      .add_json<e2e::filter_order_req_t> ()
      .add_json<e2e::filter_order_reply_t> ();
}

void add_custom_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs
      .add_serializer<e2e::custom_roundtrip_t> (
        [] (const e2e::custom_roundtrip_t &value) {
            return encode_text ("custom-request", value.value);
        },
        [] (const zlink::framework::encoded_payload_t &payload) {
            return e2e::custom_roundtrip_t{.value = decode_text (payload, "custom-request")};
        })
      .add_serializer<e2e::custom_roundtrip_reply_t> (
        [] (const e2e::custom_roundtrip_reply_t &value) {
            return encode_text ("custom-reply", value.value);
        },
        [] (const zlink::framework::encoded_payload_t &payload) {
            return e2e::custom_roundtrip_reply_t{.value = decode_text (payload, "custom-reply")};
        })
      .add_serializer<e2e::mismatch_roundtrip_t> (
        [] (const e2e::mismatch_roundtrip_t &value) {
            return encode_text ("mismatch-request", value.value);
        },
        [] (const zlink::framework::encoded_payload_t &payload) {
            return e2e::mismatch_roundtrip_t{.value = decode_text (payload, "mismatch-request")};
        })
      .add_serializer<e2e::mismatch_roundtrip_reply_t> (
        [] (const e2e::mismatch_roundtrip_reply_t &value) {
            return encode_text ("client-wrong-reply", value.value);
        },
        [] (const zlink::framework::encoded_payload_t &payload) {
            return e2e::mismatch_roundtrip_reply_t{
              .value = decode_text (payload, "client-wrong-reply")};
        });
}

class scenario_service_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit scenario_service_t (zlink::framework::app_t &app) : _app (app) {}

    void start (zlink::framework::service_provider_t &services) override
    {
        try {
            auto &channels = services.get_required<zlink::framework::channel_client_t> ();
            auto &routes = services.get_required<zlink::framework::route_client_t> ();
            run_auto_registration (channels);
            run_manual_registration (routes);
            run_scoped_lifecycle (channels);
            run_filter_order (channels);
            run_json_roundtrip (channels);
            run_custom_coexistence (channels);
            run_mismatch_negative (channels);
            passed = true;
        }
        catch (const std::exception &error) {
            std::cerr << "registration-codec scenario failed: " << error.what () << "\n";
        }
        _app.stop ();
    }

    void stop () noexcept override {}

    bool passed = false;

  private:
    void run_auto_registration (zlink::framework::channel_client_t &channels)
    {
        auto request =
          channels.request (e2e::api_channel, e2e::echo_auto_t{.value = "a1"})
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::echo_auto_reply_t> ();
        ensure (request.result ().has_value (), "RC-A1 request failed");
        ensure (request.result ().value ().value == "auto:a1", "RC-A1 reply mismatch");

        auto send = channels.send (e2e::api_channel, e2e::echo_auto_send_t{.value = "send-a1"})
                      .timeout (std::chrono::milliseconds (2000))
                      .async ();
        ensure (send.result ().has_value (), "RC-A1 send failed");
        std::cout << "scenario RC-A1 passed\n";
    }

    void run_manual_registration (zlink::framework::route_client_t &routes)
    {
        auto request =
          routes
            .request (e2e::route_channel, zlink::routing_id_t::from (std::string ("rc-server")),
                      e2e::echo_manual_t{.value = "manual"})
            .packet_name ("EchoManual")
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::echo_manual_reply_t> ();
        ensure (request.result ().has_value (), "RC-A3 route request failed");
        ensure (request.result ().value ().value == "manual:manual", "RC-A3 reply mismatch");
        ensure (request.result ().value ().packet_name == "EchoManual",
                "RC-A3 packet name mismatch");
        ensure (request.result ().value ().content_type == "application/json",
                "RC-A3 content type mismatch");
        std::cout << "scenario RC-A3 passed\n";
    }

    void run_scoped_lifecycle (zlink::framework::channel_client_t &channels)
    {
        auto first =
          channels.request (e2e::api_channel, e2e::scoped_lifecycle_req_t{.value = "first"})
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::scoped_lifecycle_reply_t> ();
        ensure (first.result ().has_value (), "RC-A4 first request failed");

        auto second =
          channels.request (e2e::api_channel, e2e::scoped_lifecycle_req_t{.value = "second"})
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::scoped_lifecycle_reply_t> ();
        ensure (second.result ().has_value (), "RC-A4 second request failed");

        const auto first_reply = first.result ().value ();
        const auto second_reply = second.result ().value ();
        ensure (first_reply.scoped_id != second_reply.scoped_id,
                "RC-A4 scoped dependency was reused");
        ensure (first_reply.singleton_id == second_reply.singleton_id,
                "RC-A4 singleton dependency was recreated");
        ensure (second_reply.destroyed_before >= first_reply.destroyed_before + 1,
                "RC-A4 scoped dependency was not destroyed after request");

        auto stats =
          channels.request (e2e::api_channel, e2e::scoped_lifecycle_stats_req_t{.value = "stats"})
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::scoped_lifecycle_stats_reply_t> ();
        ensure (stats.result ().has_value (), "RC-A4 stats request failed");
        ensure (stats.result ().value ().destroyed_count >= 2,
                "RC-A4 scoped dependency destruction count mismatch");
        std::cout << "scenario RC-A4 passed\n";
    }

    void run_filter_order (zlink::framework::channel_client_t &channels)
    {
        auto request =
          channels.request (e2e::api_channel, e2e::filter_order_req_t{.value = "filter-order"})
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::filter_order_reply_t> ();
        ensure (request.result ().has_value (), "RC-A5 request failed");
        const auto reply = request.result ().value ();
        const std::vector<std::string> expected{
          "first-before", "second-before", "handler", "second-after", "first-after"};
        ensure (reply.value == "filter-order", "RC-A5 reply value mismatch");
        ensure (reply.order == expected, "RC-A5 filter order mismatch");
        std::cout << "scenario RC-A5 passed\n";
    }

    void run_json_roundtrip (zlink::framework::channel_client_t &channels)
    {
        auto request =
          channels.request (e2e::api_channel, e2e::json_roundtrip_t{.value = "b1"})
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::json_roundtrip_reply_t> ();
        ensure (request.result ().has_value (), "RC-B1 request failed");
        ensure (request.result ().value ().value == "json:b1", "RC-B1 reply mismatch");
        std::cout << "scenario RC-B1 passed\n";
    }

    void run_custom_coexistence (zlink::framework::channel_client_t &channels)
    {
        auto json =
          channels.request (e2e::api_channel, e2e::json_roundtrip_t{.value = "coexist-json"})
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::json_roundtrip_reply_t> ();
        ensure (json.result ().has_value (), "RC-B4 JSON request failed");

        auto custom =
          channels.request (e2e::api_channel, e2e::custom_roundtrip_t{.value = "coexist-custom"})
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::custom_roundtrip_reply_t> ();
        ensure (custom.result ().has_value (), "RC-B4 custom request failed");
        ensure (custom.result ().value ().value == "custom:coexist-custom",
                "RC-B4 custom reply mismatch");
        std::cout << "scenario RC-B4 passed\n";
    }

    void run_mismatch_negative (zlink::framework::channel_client_t &channels)
    {
        auto request =
          channels.request (e2e::api_channel, e2e::mismatch_roundtrip_t{.value = "mismatch"})
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::mismatch_roundtrip_reply_t> ();
        ensure (!request.result ().has_value (), "RC-B5 mismatch unexpectedly succeeded");
        std::cout << "scenario RC-B5 passed\n";
    }

    zlink::framework::app_t &_app;
};

} // namespace

int main (int argc, char **argv)
{
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto api_endpoint = env_or ("ZLINK_CPP_E2E_API_ENDPOINT");
    const auto route_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_ENDPOINT");
    const auto client_route_endpoint = env_or ("ZLINK_CPP_E2E_CLIENT_ROUTE_ENDPOINT");

    auto app = zlink::framework::app_t::create ();
    auto scenario = std::make_unique<scenario_service_t> (app);
    auto *scenario_result = scenario.get ();
    app.logging ().use_file (log_dir + "/client.log").set_min_level (
      zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/client-flow.log")
          .trace_label ("cpp-rc-client");
        add_json_codecs (options.codecs ());
        add_custom_codecs (options.codecs ());
        options.add_client_server_channel (e2e::api_channel).enable_client (api_endpoint);
        options.add_route_mesh (e2e::route_channel)
          .enable_server (client_route_endpoint)
          .set_routing_id (zlink::routing_id_t::from (std::string ("rc-client")))
          .enable_client (route_endpoint);
    });
    app.add_hosted_service (std::move (scenario));
    const auto exit_code = app.run (argc, argv);
    if (exit_code != 0 || !scenario_result->passed) {
        return 1;
    }
    std::cout << "registration-codec e2e result=passed\n";
    return 0;
}
