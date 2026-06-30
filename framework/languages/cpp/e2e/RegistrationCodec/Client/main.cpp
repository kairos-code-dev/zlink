/* SPDX-License-Identifier: MPL-2.0 */

#include "Scenarios/auto_registration_scenario.hpp"
#include "Scenarios/codec_mismatch_scenario.hpp"
#include "Scenarios/manual_registration_scenario.hpp"
#include "Scenarios/rc_a4_di_lifecycle_scenario.hpp"
#include "Scenarios/rc_a5_filter_ordering_scenario.hpp"
#include "Scenarios/rc_b1_json_codec_scenario.hpp"
#include "Scenarios/rc_b2_protobuf_codec_scenario.hpp"
#include "Scenarios/rc_b3_messagepack_codec_scenario.hpp"
#include "Scenarios/rc_b4_codec_coexistence_scenario.hpp"

#include <iostream>
#include <memory>

namespace rc = zlink::framework::e2e::registration_codec;
namespace rc_client = zlink::framework::e2e::registration_codec::client;

namespace
{

class scenario_service_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit scenario_service_t (zlink::framework::app_t &app, std::string http_endpoint) :
        _app (app), _http_endpoint (std::move (http_endpoint))
    {
    }

    void start (zlink::framework::service_provider_t &services) override
    {
        try {
            auto &channels = services.get_required<zlink::framework::channel_client_t> ();
            auto &routes = services.get_required<zlink::framework::route_client_t> ();
            const auto scenario = rc_client::env_or ("ZLINK_CPP_E2E_SCENARIO", "all");
            if (scenario == "b5") {
                rc_client::run_codec_mismatch_scenario (channels);
            } else {
                rc_client::run_auto_registration_scenario (channels);
                rc_client::run_manual_registration_scenario (routes);
                rc_client::run_di_lifecycle_scenario (channels);
                rc_client::run_filter_ordering_scenario (channels);
                rc_client::run_json_codec_scenario (channels, _http_endpoint);
                rc_client::run_protobuf_codec_scenario (channels, _http_endpoint);
                rc_client::run_messagepack_codec_scenario (channels, _http_endpoint);
                rc_client::run_codec_coexistence_scenario (channels);
            }
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
    zlink::framework::app_t &_app;
    std::string _http_endpoint;
};

} // namespace

int main (int argc, char **argv)
{
    const auto log_dir = rc_client::env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto api_endpoint = rc_client::env_or ("ZLINK_CPP_E2E_API_ENDPOINT");
    const auto route_endpoint = rc_client::env_or ("ZLINK_CPP_E2E_ROUTE_ENDPOINT");
    const auto client_route_endpoint = rc_client::env_or ("ZLINK_CPP_E2E_CLIENT_ROUTE_ENDPOINT");
    const auto http_endpoint = rc_client::env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");

    auto app = zlink::framework::app_t::create ();
    auto scenario = std::make_unique<scenario_service_t> (app, http_endpoint);
    auto *scenario_result = scenario.get ();
    app.logging ().use_file (log_dir + "/client.log").set_min_level (
      zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/client-flow.log")
          .trace_label ("cpp-rc-client");
        rc_client::add_custom_codecs (options.codecs ());
        rc_client::add_binary_codecs (options.codecs ());
        options.add_client_server_channel (rc::api_channel).enable_client (api_endpoint);
        options.add_route_mesh (rc::route_channel)
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
