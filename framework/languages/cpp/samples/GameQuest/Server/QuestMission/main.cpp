/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_configuration.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>


#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <map>
#include <optional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace zlink::samples::gamequest
{

using namespace framework;

struct gameplay_fact_t
{
    std::string event_id;
    std::string player_id;
    std::string type;
    std::string value;
    int count = 0;
    long long occurred_at_unix_ms = 0;
};

gameplay_fact_t decode_gameplay (const gameplay_msg_t &message)
{
    const auto payload = gameplay_payload (message);
    return {.event_id = message.event_id,
            .player_id = message.player_id,
            .type = message.type,
            .value = payload.value ("value", std::string{}),
            .count = payload.value ("count", 0),
            .occurred_at_unix_ms = message.occurred_at_unix_ms};
}

/* 공통 sample spec §10: quest event stream이 진실의 원천(append-only)이고, projection은 그
 * stream을 fold해서 만든다. 같은 gameplay event가 다시 와도(재시도) event를 또 append하지
 * 않는다 — source event id로 판정한다. */
class quest_event_store_t
{
  public:
    struct evaluation_t
    {
        std::vector<quest_progress_t> projection;
        std::string completed_quest_id;
        bool reward_granted = false;
    };

    evaluation_t apply (const gameplay_fact_t &event)
    {
        const std::lock_guard lock (_mutex);
        auto &stream = _streams[event.player_id];
        const auto already_applied =
          std::any_of (stream.begin (), stream.end (), [&] (const stored_quest_event_t &stored) {
              return !stored.source_event_id.empty ()
                     && stored.source_event_id == event.event_id;
          });
        if (already_applied) {
            /* reward 멱등: 이미 반영한 gameplay event는 stream을 늘리지 않고 현재 projection만
             * 돌려준다. */
            return {projection_unlocked (event.player_id), {}, false};
        }

        const auto rule = quest_rule_for (event);
        if (!rule) {
            return {projection_unlocked (event.player_id), {}, false};
        }

        auto projection = replay_unlocked (event.player_id);
        const auto current = find_progress (projection, rule->quest_id);
        const auto previous_count = current ? current->current_count : 0;
        const auto previous_status =
          current ? current->status : std::string (quest_status_t::active);
        const auto reconciliation = event.type == "SnapshotKillCount";
        if (!reconciliation && previous_status == quest_status_t::reward_granted) {
            return {projection, {}, false};
        }

        const auto next_count = reconciliation
                                  ? std::max (previous_count, event.count)
                                  : std::min (previous_count + rule->delta, rule->required_count);
        if (next_count == previous_count) {
            return {projection_unlocked (event.player_id), {}, false};
        }
        append_unlocked (stream,
                         reconciliation ? stored_quest_event_t::reconciled
                                        : stored_quest_event_t::progressed,
                         event, *rule, next_count - previous_count, next_count);

        std::string completed_quest_id;
        bool reward_granted = false;
        if (next_count >= rule->required_count && previous_status == quest_status_t::active) {
            append_unlocked (stream, stored_quest_event_t::completed, event, *rule, 0, next_count);
            append_unlocked (stream, stored_quest_event_t::reward_granted, event, *rule, 0,
                             next_count);
            completed_quest_id = rule->quest_id;
            reward_granted = true;
        }

        std::cerr << "gamequest mission processed player=" << event.player_id
                  << " type=" << event.type << " value=" << event.value
                  << " completed=" << completed_quest_id << "\n";
        auto updated = replay_unlocked (event.player_id);
        _projections[event.player_id] = updated;
        return {std::move (updated), completed_quest_id, reward_granted};
    }

    std::vector<quest_progress_t> projection (const std::string &player_id) const
    {
        const std::lock_guard lock (_mutex);
        return projection_unlocked (player_id);
    }

    void delete_projection (const std::string &player_id, const std::string &quest_id)
    {
        const std::lock_guard lock (_mutex);
        auto &projection = _projections[player_id];
        projection.erase (
          std::remove_if (projection.begin (), projection.end (), [&] (const auto &item) {
              return item.quest_id == quest_id;
          }),
          projection.end ());
    }

    std::vector<quest_progress_t> rebuild_projection (const std::string &player_id)
    {
        const std::lock_guard lock (_mutex);
        auto rebuilt = replay_unlocked (player_id);
        _projections[player_id] = rebuilt;
        return rebuilt;
    }

    void rehydrate_owner (const std::string &player_id)
    {
        const std::lock_guard lock (_mutex);
        _projections[player_id] = replay_unlocked (player_id);
        ++_rehydrate_count[player_id];
        std::cerr << "gamequest owner rehydrated player=" << player_id
                  << " count=" << _rehydrate_count[player_id] << "\n";
    }

    int rehydrate_count (const std::string &player_id) const
    {
        const std::lock_guard lock (_mutex);
        const auto found = _rehydrate_count.find (player_id);
        return found == _rehydrate_count.end () ? 0 : found->second;
    }

  private:
    struct quest_rule_t
    {
        std::string quest_id;
        int delta = 0;
        int required_count = 0;
    };

    static std::optional<quest_rule_t> quest_rule_for (const gameplay_fact_t &event)
    {
        if (event.type == "MonsterKilled" && event.value == "wolf") {
            return quest_rule_t{quest_ids_t::first_hunt, event.count, 3};
        }
        if (event.type == "SnapshotKillCount") {
            return quest_rule_t{quest_ids_t::first_hunt, event.count, 3};
        }
        if (event.type == "FeatureUnlocked" && event.value == "auction") {
            return quest_rule_t{quest_ids_t::open_auction, 1, 1};
        }
        if (event.type == "ItemCollected" && event.value == "healing-herb") {
            return quest_rule_t{quest_ids_t::herb_gathering, event.count, 5};
        }
        if (event.type == "MissionCompleted" && event.value == "tutorial") {
            return quest_rule_t{quest_ids_t::clear_tutorial, 1, 1};
        }
        if (event.type == "AreaEntered" && event.value == "ruins") {
            return quest_rule_t{quest_ids_t::visit_ruins, 1, 1};
        }
        return std::nullopt;
    }

    static const quest_progress_t *find_progress (const std::vector<quest_progress_t> &projection,
                                                  const std::string &quest_id)
    {
        const auto found =
          std::find_if (projection.begin (), projection.end (),
                        [&] (const quest_progress_t &item) { return item.quest_id == quest_id; });
        return found == projection.end () ? nullptr : &*found;
    }

    void append_unlocked (std::vector<stored_quest_event_t> &stream,
                          const char *type,
                          const gameplay_fact_t &source,
                          const quest_rule_t &rule,
                          int delta,
                          int current_count)
    {
        stored_quest_event_t stored;
        stored.event_id = source.event_id + ":" + type;
        stored.player_id = source.player_id;
        stored.quest_id = rule.quest_id;
        stored.type = type;
        stored.source_event_id = source.event_id;
        stored.delta = delta;
        stored.current_count = current_count;
        stored.required_count = rule.required_count;
        stored.version = static_cast<long long> (stream.size ()) + 1;
        stored.created_at_unix_ms = source.occurred_at_unix_ms;
        stream.push_back (std::move (stored));
    }

    std::vector<quest_progress_t> replay_unlocked (const std::string &player_id) const
    {
        std::vector<quest_progress_t> projection;
        const auto stream = _streams.find (player_id);
        if (stream == _streams.end ()) {
            return projection;
        }
        for (const auto &stored : stream->second) {
            auto found =
              std::find_if (projection.begin (), projection.end (),
                            [&] (const quest_progress_t &item) {
                                return item.quest_id == stored.quest_id;
                            });
            if (found == projection.end ()) {
                quest_progress_t progress;
                progress.player_id = stored.player_id;
                progress.quest_id = stored.quest_id;
                progress.status = quest_status_t::active;
                projection.push_back (progress);
                found = std::prev (projection.end ());
            }
            if (stored.type == stored_quest_event_t::progressed
                || stored.type == stored_quest_event_t::reconciled) {
                found->current_count = stored.current_count;
                found->required_count = stored.required_count;
            } else if (stored.type == stored_quest_event_t::completed) {
                found->status = quest_status_t::completed;
            } else if (stored.type == stored_quest_event_t::reward_granted) {
                found->status = quest_status_t::reward_granted;
            }
            found->last_source_event_id = stored.source_event_id;
            found->version = stored.version;
            found->updated_at_unix_ms = stored.created_at_unix_ms;
        }
        return projection;
    }

    std::vector<quest_progress_t> projection_unlocked (const std::string &player_id) const
    {
        const auto found = _projections.find (player_id);
        return found == _projections.end () ? std::vector<quest_progress_t>{} : found->second;
    }

    mutable std::mutex _mutex;
    std::map<std::string, std::vector<stored_quest_event_t>> _streams;
    std::map<std::string, std::vector<quest_progress_t>> _projections;
    std::map<std::string, int> _rehydrate_count;
};

class player_quest_spot_t : public spot_t
{
  public:
    player_quest_spot_t (quest_event_store_t &store, service_provider_t services) :
        _store (store), _services (std::move (services))
    {
    }

    void configure (spot_context_t &context)
    {
        _context = context;
        context.handlers ()
          .add_handler<&player_quest_spot_t::apply> (gameplay_msg_t::packet_name)
          .add_handler<&player_quest_spot_t::sync> (sync_quest_progress_req_t::packet_name)
          .add_handler<&player_quest_spot_t::get> (get_quest_progress_req_t::packet_name)
          .add_handler<&player_quest_spot_t::admin> (projection_admin_req_t::packet_name);
    }

    spot_create_response_t on_create (const zlink::framework::message_t &request)
    {
        auto create = request.decode<player_quest_spot_create_req_t> ();
        _player_id = create.player_id;
        _store.rehydrate_owner (_player_id);
        std::cerr << "gamequest player quest spot ready player=" << _player_id
                  << " spot=" << _player_id << "\n";
        return spot_create_response_t::accept ();
    }

    /* 공통 sample spec §11.2: gameplay event는 응답 없는 one-way다. 진행 notify는 player의 현재
     * session binding이 가리키는 노드의 entry spot으로 route한다 — binding이 없으면 생략(§12). */
    task_t<void> apply (const gameplay_msg_t &message)
    {
        auto result = _store.apply (decode_gameplay (message));
        auto scope = _services.create_scope ();
        auto &actor_spots = scope.get_required<actor_spot_handle_resolver_t> ();
        auto &routes = scope.get_required<route_client_t> ();

        auto session_spot =
          co_await actor_spots.resolve_actor_spot_handle (message.player_id);
        if (!session_spot) {
            std::cerr << "gamequest mission kept projection while the player has no session"
                      << " binding. player=" << message.player_id << "\n";
            co_return;
        }
        routes
          .send_to_spot (*session_spot,
                         notify_quest_progress_msg_t{message.player_id, result.projection,
                                                     result.completed_quest_id})
          .submit ();
        std::cerr << "gamequest mission notified player=" << message.player_id
                  << " completed=" << result.completed_quest_id << "\n";
        co_return;
    }

    sync_quest_progress_res_t sync (const sync_quest_progress_req_t &request)
    {
        if (request.snapshot_kill_count > 0) {
            const gameplay_msg_t snapshot{
              request.player_id + "-snapshot-" + std::to_string (request.snapshot_kill_count),
              request.player_id,
              "SnapshotKillCount",
              gameplay_payload ("kills", request.snapshot_kill_count),
              static_cast<long long> (std::time (nullptr)) * 1000LL};
            return {_store.apply (decode_gameplay (snapshot)).projection};
        }
        return {_store.projection (request.player_id)};
    }

    get_quest_progress_res_t get (const get_quest_progress_req_t &request)
    {
        return {_store.projection (request.player_id)};
    }

    projection_admin_res_t admin (const projection_admin_req_t &request)
    {
        if (request.operation == "delete") {
            _store.delete_projection (request.player_id, request.quest_id);
            return {true, _store.projection (request.player_id)};
        }
        if (request.operation == "rebuild") {
            return {true, _store.rebuild_projection (request.player_id)};
        }
        if (request.operation == "deactivate") {
            const auto projection = _store.projection (request.player_id);
            _context.close ();
            return {true, projection};
        }
        return {false, _store.projection (request.player_id)};
    }

  private:
    quest_event_store_t &_store;
    service_provider_t _services;
    std::string _player_id;
    spot_context_t _context;
};

class ensure_player_quest_spot_handler_t
{
  public:
    using dependency_types = dependency_list_t<spot_node_manager_t>;
    using request_type = ensure_player_quest_spot_req_t;
    using reply_type = ensure_player_quest_spot_res_t;
    static constexpr const char *topic_name = ensure_player_quest_spot_req_t::packet_name;

    explicit ensure_player_quest_spot_handler_t (spot_node_manager_t &spots) : _spots (spots) {}

    ensure_player_quest_spot_res_t handle (const ensure_player_quest_spot_req_t &request)
    {
        (void) _spots.get_or_create_spot (
          sample_names_t::player_quest_spot,
          player_spot_rid (request.player_id),
          player_quest_spot_create_req_t{request.player_id});
        return {true};
    }

  private:
    spot_node_manager_t &_spots;
};

} // namespace zlink::samples::gamequest

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::gamequest;

    auto app = app_t::create ();
    const auto configuration = load_sample_configuration (app, argc, argv);
    const auto &topology = configuration.topology;
    auto quest_store = std::make_unique<quest_event_store_t> ();
    auto *quest_store_ptr = quest_store.get ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (configuration.flow_log_path ())
          .trace_label (topology.mission_name);
        options.services ().add_singleton<quest_event_store_t> (std::move (quest_store));
        options.services ().add_singleton<sample_topology_t> (
          std::make_unique<sample_topology_t> (topology));
        add_gamequest_json_codecs (options.codecs ());
        add_gamequest_location_store (options, topology);
        options.add_client_server_channel (sample_names_t::quest_owner_channel)
          .enable_server (topology.selected_mission_route_endpoint ())
          .set_routing_id (topology.selected_mission_rid ())
          .use_handler_group ("quest-owner");
        /* 같은 spot route mesh를 양방향으로 쓴다. API 노드의 entry spot으로 notify를 보내려면 그
         * 노드의 spot mesh 이름에 이 route 채널을 매핑해야 한다. */
        auto quest_spot_route = options.add_route_mesh (sample_names_t::quest_spot_route);
        quest_spot_route.listen (topology.selected_mission_spot_route_endpoint ())
          .use_allocated_routing_id (16, "gamequest-mission-route")
          .channel_name (sample_names_t::quest_spot_route);
        options.configure_locations ().spot_router_channels[api_spot_mesh_for ("api-a")] =
          sample_names_t::quest_spot_route;
        options.configure_locations ().spot_router_channels[api_spot_mesh_for ("api-b")] =
          sample_names_t::quest_spot_route;
        auto spot_services = options.services ().build_provider ();
        auto quest_spot = options.add_route_mesh (sample_names_t::quest_spot_discovery);
        quest_spot.channel_name (sample_names_t::quest_spot_route);
        quest_spot.listen (topology.selected_mission_spot_router_endpoint ())
          .use_allocated_routing_id (16, "gamequest-mission-owner")
          .add_spot<player_quest_spot_t> (
            sample_names_t::player_quest_spot, [quest_store_ptr, spot_services] {
                return std::make_shared<player_quest_spot_t> (*quest_store_ptr, spot_services);
            });
        options.handlers ()
          .group ("quest-owner")
          .add<ensure_player_quest_spot_handler_t> ();
    });
    return app.run (argc, argv);
}
