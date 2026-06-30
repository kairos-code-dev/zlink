/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/sample_names.hpp"
#include "../Shared/game_quest_state.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace zlink::samples::gamequest
{

using namespace framework;

class game_quest_session_directory_t
{
  public:
    void subscribe (const std::string &player_id, stream_t stream)
    {
        const std::lock_guard lock (_mutex);
        _streams_by_player[player_id].push_back (std::move (stream));
    }

    task_t<void> notify (const std::string &player_id,
                         const std::vector<quest_progress_t> &progress)
    {
        std::vector<stream_t> streams;
        {
            const std::lock_guard lock (_mutex);
            const auto found = _streams_by_player.find (player_id);
            if (found != _streams_by_player.end ()) {
                streams = found->second;
            }
        }
        for (auto &stream : streams) {
            for (const auto &item : progress) {
                const auto payload = zlink::message_t::from_json (item);
                co_await stream.write_packet (payload)
                  .packet_name (item.status == sample_names_t::reward_granted
                                  ? "QuestCompletedNotify"
                                  : "QuestProgressNotify")
                  .async ();
            }
        }
    }

  private:
    std::mutex _mutex;
    std::map<std::string, std::vector<stream_t>> _streams_by_player;
};

template <typename TReq, typename TRes, typename TCall> class http_body_handler_t
{
  public:
    using request_type = TReq;
    using reply_type = TRes;
    using dependency_types = dependency_list_t<game_quest_state_t, game_quest_session_directory_t>;

    http_body_handler_t (game_quest_state_t &state, game_quest_session_directory_t &sessions) :
        _state (state), _sessions (sessions)
    {
    }

  protected:
    task_t<TRes> complete (const TReq &request, TCall call)
    {
        auto result = call (_state, request);
        auto progress = _state.get_progress (request.player_id);
        co_await _sessions.notify (request.player_id, progress.active_quests);
        co_return result;
    }

    game_quest_state_t &_state;
    game_quest_session_directory_t &_sessions;
};

class kill_http_handler_t
  : public http_body_handler_t<kill_monster_req_t, event_res_t, event_res_t (*) (
                                                             game_quest_state_t &,
                                                             const kill_monster_req_t &)>
{
  public:
    using http_body_handler_t::http_body_handler_t;
    static constexpr const char *topic_name = "KillMonsterReq";
    task_t<event_res_t> handle (const kill_monster_req_t &request)
    {
        return complete (request, [] (game_quest_state_t &state, const kill_monster_req_t &value) {
            return state.kill_monster (value);
        });
    }
};

class collect_http_handler_t
  : public http_body_handler_t<collect_item_req_t, event_res_t, event_res_t (*) (
                                                            game_quest_state_t &,
                                                            const collect_item_req_t &)>
{
  public:
    using http_body_handler_t::http_body_handler_t;
    static constexpr const char *topic_name = "CollectItemReq";
    task_t<event_res_t> handle (const collect_item_req_t &request)
    {
        return complete (request, [] (game_quest_state_t &state, const collect_item_req_t &value) {
            return state.collect_item (value);
        });
    }
};

class mission_http_handler_t
  : public http_body_handler_t<complete_mission_req_t, event_res_t, event_res_t (*) (
                                                                  game_quest_state_t &,
                                                                  const complete_mission_req_t &)>
{
  public:
    using http_body_handler_t::http_body_handler_t;
    static constexpr const char *topic_name = "CompleteMissionReq";
    task_t<event_res_t> handle (const complete_mission_req_t &request)
    {
        return complete (
          request, [] (game_quest_state_t &state, const complete_mission_req_t &value) {
              return state.complete_mission (value);
          });
    }
};

class enter_http_handler_t
  : public http_body_handler_t<enter_area_req_t, event_res_t, event_res_t (*) (
                                                          game_quest_state_t &,
                                                          const enter_area_req_t &)>
{
  public:
    using http_body_handler_t::http_body_handler_t;
    static constexpr const char *topic_name = "EnterAreaReq";
    task_t<event_res_t> handle (const enter_area_req_t &request)
    {
        return complete (request, [] (game_quest_state_t &state, const enter_area_req_t &value) {
            return state.enter_area (value);
        });
    }
};

class unlock_http_handler_t
  : public http_body_handler_t<unlock_feature_req_t, event_res_t, event_res_t (*) (
                                                            game_quest_state_t &,
                                                            const unlock_feature_req_t &)>
{
  public:
    using http_body_handler_t::http_body_handler_t;
    static constexpr const char *topic_name = "UnlockFeatureReq";
    task_t<event_res_t> handle (const unlock_feature_req_t &request)
    {
        return complete (request, [] (game_quest_state_t &state, const unlock_feature_req_t &value) {
            return state.unlock_feature (value);
        });
    }
};

class snapshot_http_handler_t
{
  public:
    using request_type = get_gameplay_snapshot_req_t;
    using reply_type = get_gameplay_snapshot_res_t;
    using dependency_types = dependency_list_t<game_quest_state_t>;
    static constexpr const char *topic_name = "GetGameplaySnapshotReq";

    explicit snapshot_http_handler_t (game_quest_state_t &state) : _state (state) {}
    get_gameplay_snapshot_res_t handle (const get_gameplay_snapshot_req_t &request)
    {
        return _state.get_snapshot (request.player_id);
    }

  private:
    game_quest_state_t &_state;
};

class delete_projection_http_handler_t
{
  public:
    using request_type = delete_quest_projection_req_t;
    using reply_type = quest_progress_t;
    using dependency_types = dependency_list_t<game_quest_state_t>;
    static constexpr const char *topic_name = "DeleteQuestProjectionReq";

    explicit delete_projection_http_handler_t (game_quest_state_t &state) : _state (state) {}
    quest_progress_t handle (const delete_quest_projection_req_t &request)
    {
        return _state.delete_projection (request);
    }

  private:
    game_quest_state_t &_state;
};

class rebuild_projection_http_handler_t
{
  public:
    using request_type = rebuild_quest_projection_req_t;
    using reply_type = quest_progress_t;
    using dependency_types = dependency_list_t<game_quest_state_t>;
    static constexpr const char *topic_name = "RebuildQuestProjectionReq";

    explicit rebuild_projection_http_handler_t (game_quest_state_t &state) : _state (state) {}
    quest_progress_t handle (const rebuild_quest_projection_req_t &request)
    {
        return _state.rebuild_projection (request);
    }

  private:
    game_quest_state_t &_state;
};

class assert_http_handler_t
{
  public:
    using request_type = game_quest_server_assert_req_t;
    using reply_type = game_quest_server_assert_res_t;
    using dependency_types = dependency_list_t<game_quest_state_t>;
    static constexpr const char *topic_name = "GameQuestServerAssertReq";

    explicit assert_http_handler_t (game_quest_state_t &state) : _state (state) {}
    game_quest_server_assert_res_t handle (const game_quest_server_assert_req_t &)
    {
        return _state.assert_server ();
    }

  private:
    game_quest_state_t &_state;
};

class game_quest_session_t final : public packet_stream_session_t
{
  public:
    using dependency_types = dependency_list_t<game_quest_state_t, game_quest_session_directory_t>;

    game_quest_session_t (game_quest_state_t &state, game_quest_session_directory_t &sessions) :
        _state (state), _sessions (sessions)
    {
    }

    task_t<void> on_connected (stream_t &) override
    {
        return task_t<void> (result_t<void>::success ());
    }

    task_t<void> on_disconnected (stream_t &) override
    {
        return task_t<void> (result_t<void>::success ());
    }

    task_t<void> on_error (stream_t &, const stream_error_t &) override
    {
        return task_t<void> (result_t<void>::success ());
    }

    task_t<void> on_packet (stream_t &stream,
                            const stream_dispatch_context_t &dispatch,
                            const zlink::message_t &payload) override
    {
        const auto packet = std::string (dispatch.packet_name ());
        if (packet == subscribe_quest_req_t::packet_name) {
            const auto request = payload.parse_json<subscribe_quest_req_t> ();
            const auto response = _state.subscribe_quest (request.player_id);
            _sessions.subscribe (request.player_id, stream);
            co_await stream.reply_packet (zlink::message_t::from_json (response)).async ();
            co_return;
        }
        if (packet == get_quest_progress_req_t::packet_name) {
            const auto request = payload.parse_json<get_quest_progress_req_t> ();
            co_await stream.reply_packet (zlink::message_t::from_json (
                                            _state.get_progress (request.player_id)))
              .async ();
            co_return;
        }
        if (packet == sync_quest_progress_req_t::packet_name) {
            const auto request = payload.parse_json<sync_quest_progress_req_t> ();
            auto response = _state.sync_progress (request.player_id);
            co_await _sessions.notify (request.player_id, response.updated_quests);
            co_await stream.reply_packet (zlink::message_t::from_json (response)).async ();
        }
    }

  private:
    game_quest_state_t &_state;
    game_quest_session_directory_t &_sessions;
};

} // namespace zlink::samples::gamequest

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::gamequest;

    const sample_topology_t topology;
    const auto api_name = gamequest_env_or ("GAMEQUEST_API_NAME", "api-a");
    const auto http = api_name == "api-b" ? topology.game_api_b_http : topology.game_api_a_http;
    const auto stream =
      api_name == "api-b" ? topology.game_api_b_stream : topology.game_api_a_stream;
    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (gamequest_log_dir () + "/flow-" + api_name + ".log")
          .trace_label ("gamequest-" + api_name);
        options.services ().add_singleton<game_quest_state_t> ();
        options.services ().add_singleton<game_quest_session_directory_t> ();
        add_gamequest_json_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint);
        options.add_fanout_channel (sample_names_t::fanout_channel)
          .enable_publisher (api_name == "api-b" ? topology.fanout_publisher_b_endpoint
                                                  : topology.fanout_publisher_a_endpoint);
        options.add_stream_node (sample_names_t::stream_node)
          .bind (stream)
          .register_session<game_quest_session_t> ();
        options.http ()
          .listen (http)
          .map_health ("/health")
          .map_post<kill_http_handler_t> ("/combat/kill")
          .map_post<collect_http_handler_t> ("/inventory/collect")
          .map_post<mission_http_handler_t> ("/mission/complete")
          .map_post<enter_http_handler_t> ("/world/enter")
          .map_post<unlock_http_handler_t> ("/feature/unlock")
          .map_post<snapshot_http_handler_t> ("/internal/snapshot")
          .map_post<delete_projection_http_handler_t> ("/self-check/projection/delete")
          .map_post<rebuild_projection_http_handler_t> ("/self-check/projection/rebuild")
          .map_post<assert_http_handler_t> ("/self-check/assert");
    });
    return app.run (argc, argv);
}
