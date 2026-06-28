/* SPDX-License-Identifier: MPL-2.0 */

#include "game_quest_client_scenario.hpp"
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
    if (const char *value = std::getenv ("GAMEQUEST_REGISTRY_ROUTER_ENDPOINT");
        value != nullptr && *value != '\0') {
        return value;
    }
    return "tcp://127.0.0.1:32084";
}

class client_scenario_service_t final : public hosted_service_t
{
  public:
    explicit client_scenario_service_t (app_t &app) : _app (app) {}

    void start (service_provider_t &services) override
    {
        auto &channels = services.get_required<channel_client_t> ();
        passed = zlink::samples::gamequest::game_quest_client_scenario_t{}.run (channels);
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
          .trace_log_file (zlink::samples::gamequest::flow_log_path ("client"))
          .trace_label ("gamequest-client");
        auto codecs = options.codecs ();
        codecs.add_json ();
        codecs.add_json<zlink::samples::gamequest::enter_area_req_t,
                        zlink::samples::gamequest::kill_monster_req_t,
                        zlink::samples::gamequest::collect_item_req_t,
                        zlink::samples::gamequest::complete_mission_req_t,
                        zlink::samples::gamequest::unlock_feature_req_t,
                        zlink::samples::gamequest::subscribe_quest_req_t,
                        zlink::samples::gamequest::subscribe_quest_res_t,
                        zlink::samples::gamequest::get_quest_progress_req_t,
                        zlink::samples::gamequest::get_quest_progress_res_t,
                        zlink::samples::gamequest::sync_quest_progress_req_t,
                        zlink::samples::gamequest::sync_quest_progress_res_t,
                        zlink::samples::gamequest::get_gameplay_snapshot_req_t,
                        zlink::samples::gamequest::get_gameplay_snapshot_res_t,
                        zlink::samples::gamequest::delete_quest_projection_req_t,
                        zlink::samples::gamequest::rebuild_quest_projection_req_t,
                        zlink::samples::gamequest::game_quest_server_assert_req_t,
                        zlink::samples::gamequest::game_quest_server_assert_res_t,
                        zlink::samples::gamequest::event_res_t,
                        zlink::samples::gamequest::quest_progress_t> ();
        options.use_discovery ().add_registry_endpoint (registry_router_endpoint ());
        options.add_client_server_channel ("gamequest.quest").enable_client ();
    });
    app.add_hosted_service (std::move (scenario));
    const auto exit_code = app.run (argc, argv);
    if (exit_code != 0 || !scenario_result->passed) {
        std::cerr << "gamequest=failed\n";
        return 1;
    }
    std::cout << "gamequest=completed\n";
    return 0;
}
