/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../common_codecs.hpp"
#include "../../sample_log_dir.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <ctime>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace zlink::samples::gamequest
{

using namespace framework;

class quest_store_t
{
  public:
    apply_gameplay_event_res_t apply (const gameplay_event_envelope_t &event)
    {
        const std::lock_guard lock (_mutex);
        const auto event_key = event.player_id + ":" + event.idempotency_key;
        if (_seen_events.contains (event_key)) {
            return {true, projection_unlocked (event.player_id), ""};
        }
        _seen_events[event_key] = event.event_id;

        std::string completed;
        if (event.event_type == "MonsterKilled" && event.value == "wolf") {
            completed = advance (event.player_id, quest_ids_t::first_hunt, event.count, 3,
                                 event.event_id);
        }
        else if (event.event_type == "FeatureUnlocked" && event.value == "auction") {
            completed = advance (event.player_id, quest_ids_t::open_auction, 1, 1,
                                 event.event_id);
        }
        else if (event.event_type == "ItemCollected" && event.value == "healing-herb") {
            completed = advance (event.player_id, quest_ids_t::herb_gathering, event.count, 5,
                                 event.event_id);
        }
        else if (event.event_type == "MissionCompleted" && event.value == "tutorial") {
            completed = advance (event.player_id, quest_ids_t::clear_tutorial, 1, 1,
                                 event.event_id);
        }
        else if (event.event_type == "AreaEntered" && event.value == "ruins") {
            completed = advance (event.player_id, quest_ids_t::visit_ruins, 1, 1,
                                 event.event_id);
        }

        std::cerr << "gamequest mission processed player=" << event.player_id
                  << " type=" << event.event_type << " value=" << event.value
                  << " completed=" << completed << "\n";
        return {true, projection_unlocked (event.player_id), completed};
    }

    std::vector<quest_progress_t> projection (const std::string &player_id) const
    {
        const std::lock_guard lock (_mutex);
        return projection_unlocked (player_id);
    }

  private:
    std::string advance (const std::string &player_id,
                         const std::string &quest_id,
                         int delta,
                         int required,
                         const std::string &event_id)
    {
        auto &progress = _progress[player_id + ":" + quest_id];
        if (progress.player_id.empty ()) {
            progress.player_id = player_id;
            progress.quest_id = quest_id;
            progress.required_count = required;
            progress.status = quest_status_t::active;
        }
        progress.current_count += delta;
        if (progress.current_count > required) {
            progress.current_count = required;
        }
        progress.last_event_id = event_id;
        progress.updated_at_unix_ms = static_cast<long long> (std::time (nullptr)) * 1000LL;
        const auto newly_completed = progress.status != quest_status_t::reward_granted
                                     && progress.current_count >= progress.required_count;
        if (newly_completed) {
            progress.status = quest_status_t::reward_granted;
            return quest_id;
        }
        return {};
    }

    std::vector<quest_progress_t> projection_unlocked (const std::string &player_id) const
    {
        std::vector<quest_progress_t> result;
        for (const auto &[_, progress] : _progress) {
            if (progress.player_id == player_id) {
                result.push_back (progress);
            }
        }
        return result;
    }

    mutable std::mutex _mutex;
    std::map<std::string, std::string> _seen_events;
    std::map<std::string, quest_progress_t> _progress;
};

class apply_gameplay_event_handler_t
{
  public:
    using dependency_types = dependency_list_t<quest_store_t>;
    using request_type = apply_gameplay_event_req_t;
    using reply_type = apply_gameplay_event_res_t;
    static constexpr const char *topic_name = apply_gameplay_event_req_t::packet_name;

    explicit apply_gameplay_event_handler_t (quest_store_t &store) : _store (store) {}

    apply_gameplay_event_res_t handle (const apply_gameplay_event_req_t &request)
    {
        return _store.apply (request.event);
    }

  private:
    quest_store_t &_store;
};

class sync_quest_progress_handler_t
{
  public:
    using dependency_types = dependency_list_t<quest_store_t>;
    using request_type = sync_quest_progress_req_t;
    using reply_type = sync_quest_progress_res_t;
    static constexpr const char *topic_name = sync_quest_progress_req_t::packet_name;

    explicit sync_quest_progress_handler_t (quest_store_t &store) : _store (store) {}

    sync_quest_progress_res_t handle (const sync_quest_progress_req_t &request)
    {
        return {_store.projection (request.player_id)};
    }

  private:
    quest_store_t &_store;
};

class get_quest_progress_handler_t
{
  public:
    using dependency_types = dependency_list_t<quest_store_t>;
    using request_type = get_quest_progress_req_t;
    using reply_type = get_quest_progress_res_t;
    static constexpr const char *topic_name = get_quest_progress_req_t::packet_name;

    explicit get_quest_progress_handler_t (quest_store_t &store) : _store (store) {}

    get_quest_progress_res_t handle (const get_quest_progress_req_t &request)
    {
        return {_store.projection (request.player_id)};
    }

  private:
    quest_store_t &_store;
};

} // namespace zlink::samples::gamequest

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::gamequest;

    const sample_topology_t topology;
    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (gamequest_flow_log_path (topology.mission_name))
          .trace_label (topology.mission_name);
        options.services ().add_singleton<quest_store_t> ();
        add_gamequest_json_codecs (options.codecs ());
        add_gamequest_location_store (options, topology);
        options.add_route_mesh_channel (sample_names_t::quest_owner_route_channel)
          .enable_server (topology.selected_mission_route_endpoint ())
          .set_routing_id (topology.selected_mission_rid ())
          .use_handler_group ("quest-owner");
        options.handlers ()
          .group ("quest-owner")
          .add<apply_gameplay_event_handler_t> ()
          .add<sync_quest_progress_handler_t> ()
          .add<get_quest_progress_handler_t> ();
    });
    return app.run (argc, argv);
}
