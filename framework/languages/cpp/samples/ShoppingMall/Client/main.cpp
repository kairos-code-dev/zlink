/* SPDX-License-Identifier: MPL-2.0 */

#include "shopping_mall_client_scenario.hpp"

#include <zlink/framework.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace
{

std::string registry_router_endpoint ()
{
    if (const char *value = std::getenv ("SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT");
        value != nullptr && *value != '\0') {
        return value;
    }
    return "tcp://127.0.0.1:32086";
}

class client_scenario_service_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit client_scenario_service_t (zlink::framework::app_t &app) : _app (app) {}

    void start (zlink::framework::service_provider_t &services) override
    {
        auto &channels = services.get_required<zlink::framework::channel_client_t> ();
        passed = zlink::samples::shoppingmall::shopping_mall_client_scenario_t{}.run (channels);
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
          .add_json<zlink::samples::shoppingmall::start_order_req_t> ()
          .add_json<zlink::samples::shoppingmall::start_order_res_t> ()
          .add_json<zlink::samples::shoppingmall::get_order_state_req_t> ()
          .add_json<zlink::samples::shoppingmall::delete_order_projection_req_t> ()
          .add_json<zlink::samples::shoppingmall::get_order_state_res_t> ()
          .add_json<zlink::samples::shoppingmall::continue_order_workflow_req_t> ()
          .add_json<zlink::samples::shoppingmall::continue_order_workflow_res_t> ()
          .add_json<zlink::samples::shoppingmall::rebuild_order_projection_req_t> ()
          .add_json<zlink::samples::shoppingmall::rebuild_order_projection_res_t> ()
          .add_json<zlink::samples::shoppingmall::seed_pending_idempotency_req_t> ()
          .add_json<zlink::samples::shoppingmall::server_assertion_req_t> ()
          .add_json<zlink::samples::shoppingmall::server_assertion_res_t> ()
          .add_json<zlink::samples::shoppingmall::order_state_t> ();
        options.use_discovery ().add_registry_endpoint (registry_router_endpoint ());
        options.add_client_server_channel ("shoppingmall.workflow")
          .enable_client ();
    });
    app.add_hosted_service (std::move (scenario));
    const auto exit_code = app.run (argc, argv);
    if (exit_code != 0 || !scenario_result->passed) {
        std::cerr << "shoppingmall=failed\n";
        return 1;
    }
    std::cout << "shoppingmall=completed\n";
    return 0;
}
