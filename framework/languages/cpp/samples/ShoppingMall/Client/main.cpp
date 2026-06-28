/* SPDX-License-Identifier: MPL-2.0 */

#include "shopping_mall_client_scenario.hpp"
#include "../sample_log_dir.hpp"

#include <zlink/framework.hpp>

using namespace zlink;

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace
{

using namespace framework;

std::string registry_router_endpoint ()
{
    if (const char *value = std::getenv ("SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT");
        value != nullptr && *value != '\0') {
        return value;
    }
    return "tcp://127.0.0.1:32086";
}

class client_scenario_service_t final : public hosted_service_t
{
  public:
    explicit client_scenario_service_t (app_t &app) : _app (app) {}

    void start (service_provider_t &services) override
    {
        auto &channels = services.get_required<channel_client_t> ();
        passed = zlink::samples::shoppingmall::shopping_mall_client_scenario_t{}.run (channels);
        _app.stop ();
    }

    void stop () noexcept override {}

    bool passed = false;

  private:
    app_t &_app;
};

} // namespace

int main (int argc, char **argv)
{
    auto app = app_t::create ();
    auto scenario = std::make_unique<client_scenario_service_t> (app);
    auto *scenario_result = scenario.get ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (zlink::samples::shoppingmall::flow_log_path ("client"))
          .trace_label ("shoppingmall-client");
        auto codecs = options.codecs ();
        codecs.add_json ();
        codecs.add_json<zlink::samples::shoppingmall::start_order_req_t,
                        zlink::samples::shoppingmall::start_order_res_t,
                        zlink::samples::shoppingmall::get_order_state_req_t,
                        zlink::samples::shoppingmall::delete_order_projection_req_t,
                        zlink::samples::shoppingmall::get_order_state_res_t,
                        zlink::samples::shoppingmall::continue_order_workflow_req_t,
                        zlink::samples::shoppingmall::continue_order_workflow_res_t,
                        zlink::samples::shoppingmall::rebuild_order_projection_req_t,
                        zlink::samples::shoppingmall::rebuild_order_projection_res_t,
                        zlink::samples::shoppingmall::seed_pending_idempotency_req_t,
                        zlink::samples::shoppingmall::server_assertion_req_t,
                        zlink::samples::shoppingmall::server_assertion_res_t,
                        zlink::samples::shoppingmall::order_state_t> ();
        options.use_discovery ().add_registry_endpoint (registry_router_endpoint ());
        options.add_client_server_channel ("shoppingmall.workflow").enable_client ();
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
