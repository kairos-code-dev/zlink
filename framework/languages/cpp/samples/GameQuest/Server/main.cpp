/* SPDX-License-Identifier: MPL-2.0 */

#include "Handlers/collect_item_handler.hpp"
#include "Handlers/complete_mission_handler.hpp"
#include "Handlers/enter_area_handler.hpp"
#include "Handlers/kill_monster_handler.hpp"
#include "Handlers/query_and_self_check_handlers.hpp"

#include <zlink/framework.hpp>

#include <cstdlib>
#include <memory>
#include <string>

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

} // namespace

int main (int argc, char **argv)
{
    using namespace zlink::samples::gamequest;

    auto app = zlink::framework::app_t::create ();
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.services ().add_singleton<game_quest_server_role_t> (
          std::make_unique<game_quest_server_role_t> ());
        options.handlers ()
          .add<enter_area_handler_t> ("quest")
          .add<kill_monster_handler_t> ("quest")
          .add<collect_item_handler_t> ("quest")
          .add<complete_mission_handler_t> ("quest")
          .add<unlock_feature_handler_t> ("quest")
          .add<subscribe_quest_handler_t> ("quest")
          .add<get_quest_progress_handler_t> ("quest")
          .add<sync_quest_progress_handler_t> ("quest")
          .add<get_gameplay_snapshot_handler_t> ("quest")
          .add<delete_quest_projection_handler_t> ("quest")
          .add<rebuild_quest_projection_handler_t> ("quest")
          .add<game_quest_server_assert_handler_t> ("quest");
        options.codecs ()
          .add_json ()
          .add_json<enter_area_req_t> ()
          .add_json<kill_monster_req_t> ()
          .add_json<collect_item_req_t> ()
          .add_json<complete_mission_req_t> ()
          .add_json<unlock_feature_req_t> ()
          .add_json<subscribe_quest_req_t> ()
          .add_json<subscribe_quest_res_t> ()
          .add_json<get_quest_progress_req_t> ()
          .add_json<get_quest_progress_res_t> ()
          .add_json<sync_quest_progress_req_t> ()
          .add_json<sync_quest_progress_res_t> ()
          .add_json<get_gameplay_snapshot_req_t> ()
          .add_json<get_gameplay_snapshot_res_t> ()
          .add_json<delete_quest_projection_req_t> ()
          .add_json<rebuild_quest_projection_req_t> ()
          .add_json<game_quest_server_assert_req_t> ()
          .add_json<game_quest_server_assert_res_t> ()
          .add_json<event_res_t> ()
          .add_json<quest_progress_t> ();
        options.add_client_server_channel ("gamequest.quest")
          .enable_server (quest_endpoint ())
          .use_handler_group ("quest");
    });
    return app.run (argc, argv);
}
