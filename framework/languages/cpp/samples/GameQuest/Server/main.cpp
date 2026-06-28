/* SPDX-License-Identifier: MPL-2.0 */

#include "Handlers/collect_item_handler.hpp"
#include "Handlers/complete_mission_handler.hpp"
#include "Handlers/enter_area_handler.hpp"
#include "Handlers/kill_monster_handler.hpp"
#include "Handlers/query_and_self_check_handlers.hpp"
#include "../sample_log_dir.hpp"

#include <zlink/framework.hpp>

using namespace zlink;

#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

namespace
{

std::string quest_endpoint ()
{
    if (const char *value = std::getenv ("GAMEQUEST_QUEST_ENDPOINT");
        value != nullptr && *value != '\0') {
        return value;
    }
    return "tcp://127.0.0.1:32092";
}

std::string registry_pub_endpoint ()
{
    if (const char *value = std::getenv ("GAMEQUEST_REGISTRY_PUB_ENDPOINT");
        value != nullptr && *value != '\0') {
        return value;
    }
    return "tcp://127.0.0.1:32083";
}

std::string registry_router_endpoint ()
{
    if (const char *value = std::getenv ("GAMEQUEST_REGISTRY_ROUTER_ENDPOINT");
        value != nullptr && *value != '\0') {
        return value;
    }
    return "tcp://127.0.0.1:32084";
}

} // namespace

int main (int argc, char **argv)
{
    using namespace zlink::samples::gamequest;
    using namespace framework;

    auto registry_app = app_t::create ();
    registry_app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (flow_log_path ("registry"))
          .trace_label ("gamequest-registry");
        options.enable_registry (registry_pub_endpoint (), registry_router_endpoint ());
    });
    std::thread registry_thread ([&] { (void) registry_app.run (argc, argv); });

    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (flow_log_path ("server"))
          .trace_label ("gamequest-server");
        options.services ().add_singleton<game_quest_server_role_t> (
          std::make_unique<game_quest_server_role_t> ());
        options.codecs ().add_json ();
        options.use_discovery ().add_registry_endpoint (registry_router_endpoint ());
        options.add_client_server_channel ("gamequest.quest")
          .enable_server (quest_endpoint ())
          .use_handler_group ("quest");
        options.handlers ()
          .group ("quest")
          .add<enter_area_handler_t> ()
          .add<kill_monster_handler_t> ()
          .add<collect_item_handler_t> ()
          .add<complete_mission_handler_t> ()
          .add<unlock_feature_handler_t> ()
          .add<subscribe_quest_handler_t> ()
          .add<get_quest_progress_handler_t> ()
          .add<sync_quest_progress_handler_t> ()
          .add<get_gameplay_snapshot_handler_t> ()
          .add<delete_quest_projection_handler_t> ()
          .add<rebuild_quest_projection_handler_t> ()
          .add<game_quest_server_assert_handler_t> ();
    });
    const auto exit_code = app.run (argc, argv);
    registry_app.stop ();
    if (registry_thread.joinable ()) {
        registry_thread.join ();
    }
    return exit_code;
}
