/* SPDX-License-Identifier: MPL-2.0 */

#include "delivery_dispatch_client_scenario.hpp"

#include <zlink/framework.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace
{

std::string dispatch_endpoint ()
{
    if (const char *value = std::getenv ("DELIVERYDISPATCH_DISPATCH_ENDPOINT");
        value != nullptr && *value != '\0') {
        return value;
    }
    return "tcp://127.0.0.1:32091";
}

class client_scenario_service_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit client_scenario_service_t (zlink::framework::app_t &app) : _app (app) {}

    void start (zlink::framework::service_provider_t &services) override
    {
        auto &channels = services.get_required<zlink::framework::channel_client_t> ();
        passed = zlink::samples::deliverydispatch::delivery_dispatch_client_scenario_t{}.run (
          channels);
        _app.stop ();
    }

    void stop () noexcept override {}

    bool passed = false;

  private:
    zlink::framework::app_t &_app;
};

} // namespace

int main (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    auto scenario = std::make_unique<client_scenario_service_t> (app);
    auto *scenario_result = scenario.get ();
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.codecs ()
          .add_json ()
          .add_json<zlink::samples::deliverydispatch::create_delivery_req_t> ()
          .add_json<zlink::samples::deliverydispatch::delivery_created_t> ()
          .add_json<zlink::samples::deliverydispatch::subscribe_delivery_req_t> ()
          .add_json<zlink::samples::deliverydispatch::subscribe_delivery_accepted_t> ()
          .add_json<zlink::samples::deliverydispatch::assign_delivery_req_t> ()
          .add_json<zlink::samples::deliverydispatch::advance_delivery_req_t> ()
          .add_json<zlink::samples::deliverydispatch::delivery_state_t> ()
          .add_json<zlink::samples::deliverydispatch::server_assertion_req_t> ()
          .add_json<zlink::samples::deliverydispatch::server_assertion_res_t> ();
        options.add_client_server_channel ("deliverydispatch.dispatch")
          .enable_client (dispatch_endpoint ());
    });
    app.add_hosted_service (std::move (scenario));
    const auto exit_code = app.run (argc, argv);
    if (exit_code != 0 || !scenario_result->passed) {
        std::cerr << "deliverydispatch=failed\n";
        return 1;
    }
    std::cout << "deliverydispatch=completed\n";
    return 0;
}
