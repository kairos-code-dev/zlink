/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_configuration.hpp"
#include "../common_codecs.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <exception>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace zlink::samples::gamequest
{

using namespace framework;

static constexpr const char *gamequest_player_actor_type = "gamequest-player";

class game_api_store_t
{
  public:
    void bind (const std::string &player_id, const std::string &api_name, const stream_t &stream)
    {
        const std::lock_guard lock (_mutex);
        _bindings[player_id] = api_name;
        _session_ids[player_id] = stream.session_id ();
    }

    void unbind (const std::string &player_id)
    {
        const std::lock_guard lock (_mutex);
        _bindings.erase (player_id);
        _session_ids.erase (player_id);
    }

    void merge_projection (const std::string &player_id,
                           const std::vector<quest_progress_t> &projection)
    {
        const std::lock_guard lock (_mutex);
        _projections[player_id] = projection;
    }

    std::vector<quest_progress_t> projection (const std::string &player_id) const
    {
        const std::lock_guard lock (_mutex);
        const auto found = _projections.find (player_id);
        return found == _projections.end () ? std::vector<quest_progress_t>{} : found->second;
    }

    void record_event (const gameplay_event_envelope_t &event)
    {
        const std::lock_guard lock (_mutex);
        _events.push_back (event);
    }

    /* notify는 actor가 자기 bound session으로 push한다. store는 projection 기록만 맡는다. */
    void push_notify (session_actor_manager_t &actors, const notify_quest_progress_msg_t &notify)
    {
        std::string session_id;
        {
            const std::lock_guard lock (_mutex);
            _projections[notify.player_id] = notify.projection;
            const auto found = _session_ids.find (notify.player_id);
            if (found == _session_ids.end ()) {
                std::cerr << "gamequest api: no bound session for player=" << notify.player_id
                          << "\n";
                return;
            }
            session_id = found->second;
        }

        auto actor = actors.find (notify.player_id);
        if (!actor) {
            return;
        }
        for (const auto &progress : notify.projection) {
            actor->bound_session ()
              .send (quest_progress_notify_t{notify.player_id, session_id, progress})
              .submit ();
        }
        if (!notify.completed_quest_id.empty ()) {
            const auto completed =
              std::find_if (notify.projection.begin (), notify.projection.end (),
                            [&] (const quest_progress_t &progress) {
                                return progress.quest_id == notify.completed_quest_id;
                            });
            if (completed != notify.projection.end ()) {
                actor->bound_session ()
                  .send (quest_completed_notify_t{notify.player_id, session_id, *completed, true})
                  .submit ();
            }
        }
    }

    server_assertion_res_t assert_state () const
    {
        const std::lock_guard lock (_mutex);
        std::vector<std::string> evidence;
        bool alice_first_hunt = false;
        bool alice_auction = false;
        bool bob_herb = false;
        for (const auto &[player_id, projection] : _projections) {
            for (const auto &progress : projection) {
                evidence.push_back (player_id + ":" + progress.quest_id + ":" + progress.status
                                    + ":" + std::to_string (progress.current_count) + "/"
                                    + std::to_string (progress.required_count));
                alice_first_hunt = alice_first_hunt
                                   || (progress.player_id == "player-alice"
                                       && progress.quest_id == quest_ids_t::first_hunt
                                       && progress.status == quest_status_t::reward_granted);
                alice_auction = alice_auction
                                || (progress.player_id == "player-alice"
                                    && progress.quest_id == quest_ids_t::open_auction
                                    && progress.status == quest_status_t::reward_granted);
                bob_herb = bob_herb
                           || (progress.player_id == "player-bob"
                               && progress.quest_id == quest_ids_t::herb_gathering
                               && progress.status == quest_status_t::reward_granted);
            }
        }
        for (const auto &[player, api] : _bindings) {
            evidence.push_back ("binding:" + player + ":" + api);
        }
        for (const auto &event : _events) {
            evidence.push_back ("event:" + event.player_id + ":" + event.event_type + ":"
                                + event.idempotency_key);
        }
        return {alice_first_hunt || alice_auction || bob_herb, evidence};
    }

  private:
    mutable std::mutex _mutex;
    std::map<std::string, std::string> _bindings;
    std::map<std::string, std::string> _session_ids;
    std::map<std::string, std::vector<quest_progress_t>> _projections;
    std::vector<gameplay_event_envelope_t> _events;
};

class player_actor_t
{
  public:
    explicit player_actor_t (std::string actor_id) : actor_id (std::move (actor_id)) {}

    void set_actor_ref (const zlink::framework::actor_ref_t &value)
    {
        actor_ref = value;
        actor_id = std::string (value.actor_id ());
    }

    void set_actor_context (actor_context_t value) { context = std::move (value); }

    std::string actor_id;
    zlink::framework::actor_ref_t actor_ref;
    actor_context_t context;
};

struct player_actor_factory_t
{
    player_actor_t create (std::string actor_id) const
    {
        return player_actor_t (std::move (actor_id));
    }
};

/* owner spot이 보낸 진행 notify가 이 노드의 entry spot으로 route돼 들어온다. 어느 노드로 갈지는
 * location store의 session binding이 정하므로, API는 자기 노드의 actor만 보면 된다. */
class player_entry_spot_t : public entry_spot_t
{
  public:
    player_entry_spot_t (game_api_store_t &store, service_provider_t services) :
        _store (store), _services (std::move (services))
    {
    }

    void configure (entry_spot_context_t &context)
    {
        _context = context;
        context.handlers ()
          .add_handler<&player_entry_spot_t::quest_progress_notified> (
            notify_quest_progress_msg_t::packet_name)
          .add_actor_request<&player_entry_spot_t::join_session> (join_session_req_t::packet_name);
    }

    void configure (spot_context_t &context)
    {
        entry_spot_context_t entry_context (context);
        configure (entry_context);
    }

    spot_actor_join_response_t on_actor_join (std::string_view, const zlink::message_t &)
    {
        return spot_actor_join_response_t::accept ();
    }

    /* session이 join을 actor로 relay한다. actor가 이 노드의 entry spot에 붙어 있어야 owner spot이
     * session binding으로 이 노드를 찾을 수 있다. */
    join_session_res_t join_session (player_actor_t &,
                                     spot_actor_request_context_t &,
                                     const join_session_req_t &request)
    {
        return {_store.projection (request.player_id)};
    }

    void quest_progress_notified (const notify_quest_progress_msg_t &notify)
    {
        auto scope = _services.create_scope ();
        _store.push_notify (scope.get_required<session_actor_manager_t> (), notify);
    }

  private:
    game_api_store_t &_store;
    service_provider_t _services;
    entry_spot_context_t _context;
};

class gamequest_session_t final : public packet_stream_session_t
{
  public:
    using dependency_types = dependency_list_t<channel_client_t,
                                               route_client_t,
                                               game_api_store_t,
                                               sample_topology_t,
                                               session_actor_manager_t,
                                               actor_gateway_t,
                                               spot_handle_resolver_t>;

    gamequest_session_t (channel_client_t &channels,
                         route_client_t &routes,
                         game_api_store_t &store,
                         sample_topology_t &topology,
                         session_actor_manager_t &actors,
                         actor_gateway_t &gateway,
                         spot_handle_resolver_t &spot_handles) :
        _channels (channels),
        _routes (routes),
        _store (store),
        _topology (topology),
        _actors (actors),
        _gateway (gateway),
        _spot_handles (spot_handles)
    {
    }

    task_t<void> on_connected (stream_t &) override { co_return; }

    task_t<void> on_disconnected (stream_t &) override
    {
        if (_player_id) {
            _gateway.unbind_session_stream (*_player_id);
            _actors.unbind_session (*_player_id);
            _store.unbind (*_player_id);
            _player_id.reset ();
        }
        co_return;
    }

    task_t<void> on_error (stream_t &, const stream_error_t &) override { co_return; }

    task_t<void> on_packet (stream_t &stream,
                            const stream_dispatch_context_t &dispatch,
                            const zlink::message_t &payload) override
    {
        const auto packet = std::string (dispatch.packet_name ());
        if (packet == join_session_req_t::packet_name) {
            const auto request = payload.parse_json<join_session_req_t> ();
            auto actor = _actors.get_or_create (gamequest_player_actor_type, request.player_id);
            if (!actor) {
                throw framework_exception_t (
                  actor.error_kind (),
                  actor.error () ? actor.error ()->what () : "gamequest session actor bind failed");
            }
            auto bound = co_await _actors.bind_or_get (actor.value ().ref ()).async ();
            (void) co_await bound.context ()
              .join_entry_spot (node_rid_t::from_string (_topology.selected_api_node_rid ()),
                                request)
              .async ();
            _gateway.bind_session_stream (std::string (bound.actor_id ()), stream,
                                          stream_codec_t::json);
            _player_id = request.player_id;
            _store.bind (request.player_id, _topology.api_name, stream);
            auto synced = co_await sync_projection (request.player_id);
            _store.merge_projection (request.player_id, synced.updated_quests);
            auto current = _actors.find (std::string (bound.actor_id ()));
            if (!current) {
                throw framework_exception_t (framework_error_kind_t::actor_route_not_found,
                                             "joined player actor route is not found");
            }
            auto reply = co_await current
                           ->relay_request (join_session_req_t::packet_name,
                                            zlink::message_t::from_json (request))
                           .async ();
            stream.reply_packet (reply).submit ();
            co_return;
        }
        if (packet == get_quest_progress_req_t::packet_name) {
            const auto request = payload.parse_json<get_quest_progress_req_t> ();
            auto synced = co_await sync_projection (request.player_id);
            _store.merge_projection (request.player_id, synced.updated_quests);
            stream
              .reply_packet (
                zlink::message_t::from_json (get_quest_progress_res_t{synced.updated_quests}))
              .submit ();
            co_return;
        }
        if (packet == sync_quest_progress_req_t::packet_name) {
            const auto request = payload.parse_json<sync_quest_progress_req_t> ();
            auto synced = co_await sync_projection (request.player_id);
            _store.merge_projection (request.player_id, synced.updated_quests);
            stream.reply_packet (zlink::message_t::from_json (synced))
              .submit ();
            co_return;
        }
        if (packet == kill_monster_req_t::packet_name) {
            const auto request = payload.parse_json<kill_monster_req_t> ();
            const auto event = event_for (request.player_id, request.idempotency_key,
                                          "MonsterKilled", request.monster_id, 1);
            co_await apply_event (event);
            stream.reply_packet (zlink::message_t::from_json (kill_monster_res_t{event.event_id}))
              .submit ();
            co_return;
        }
        if (packet == collect_item_req_t::packet_name) {
            const auto request = payload.parse_json<collect_item_req_t> ();
            const auto event = event_for (request.player_id, request.idempotency_key,
                                          "ItemCollected", request.item_id, request.count);
            co_await apply_event (event);
            stream.reply_packet (zlink::message_t::from_json (collect_item_res_t{event.event_id}))
              .submit ();
            co_return;
        }
        if (packet == complete_mission_req_t::packet_name) {
            const auto request = payload.parse_json<complete_mission_req_t> ();
            const auto event = event_for (request.player_id, request.idempotency_key,
                                          "MissionCompleted", request.mission_id, 1);
            co_await apply_event (event);
            stream
              .reply_packet (zlink::message_t::from_json (complete_mission_res_t{event.event_id}))
              .submit ();
            co_return;
        }
        if (packet == enter_area_req_t::packet_name) {
            const auto request = payload.parse_json<enter_area_req_t> ();
            const auto event = event_for (request.player_id, request.idempotency_key, "AreaEntered",
                                          request.area_id, 1);
            co_await apply_event (event);
            stream.reply_packet (zlink::message_t::from_json (enter_area_res_t{event.event_id}))
              .submit ();
            co_return;
        }
        if (packet == unlock_feature_req_t::packet_name) {
            const auto request = payload.parse_json<unlock_feature_req_t> ();
            const auto event = event_for (request.player_id, request.idempotency_key,
                                          "FeatureUnlocked", request.feature_id, 1);
            co_await apply_event (event);
            stream.reply_packet (zlink::message_t::from_json (unlock_feature_res_t{event.event_id}))
              .submit ();
            co_return;
        }
        throw framework_exception_t (framework_error_kind_t::request_failed,
                                     "Unsupported GameQuest packet: " + packet);
    }

  private:
    gameplay_event_envelope_t event_for (std::string player_id,
                                         std::string idempotency_key,
                                         std::string event_type,
                                         std::string value,
                                         int count) const
    {
        return {player_id + "-" + idempotency_key,
                std::move (player_id),
                std::move (idempotency_key),
                std::move (event_type),
                std::move (value),
                count,
                _topology.api_name,
                static_cast<long long> (std::time (nullptr)) * 1000LL};
    }

    task_t<spot_handle_t> resolve_player_spot (const std::string &player_id)
    {
        auto handle = co_await _spot_handles.resolve_spot_handle (player_spot_rid (player_id));
        if (!handle) {
            throw framework_exception_t (framework_error_kind_t::spot_route_not_found,
                                         "GameQuest player quest spot for '" + player_id
                                           + "' has no live location row");
        }
        co_return *handle;
    }

    task_t<sync_quest_progress_res_t> sync_projection (const std::string &player_id)
    {
        co_await ensure_player_spot (player_id);
        auto target = co_await resolve_player_spot (player_id);
        auto synced = co_await _routes
                        .request_to_spot (std::move (target), sync_quest_progress_req_t{player_id})
                        .template async<sync_quest_progress_res_t> ();
        co_return synced;
    }

    /* 공통 sample spec §11.2: gameplay event는 owner spot으로 보내는 응답 없는 one-way다.
     * client에는 event id만 즉시 돌려주고, 진행은 notify로 돌아온다. */
    task_t<void> apply_event (const gameplay_event_envelope_t &event)
    {
        co_await ensure_player_spot (event.player_id);
        auto target = co_await resolve_player_spot (event.player_id);
        _routes.send_to_spot (std::move (target), gameplay_msg_t{event}).submit ();
        _store.record_event (event);
        std::cerr << "gamequest api event routed player=" << event.player_id
                  << " owner=" << owner_index (event.player_id) << " type=" << event.event_type
                  << "\n";
        co_return;
    }

    task_t<void> ensure_player_spot (const std::string &player_id)
    {
        auto ensured =
          co_await _channels
            .request (quest_owner_channel_for (owner_mission_id (player_id)),
                      ensure_player_quest_spot_req_t{player_id})
            .template async<ensure_player_quest_spot_res_t> ();
        if (!ensured.ok) {
            throw framework_exception_t (framework_error_kind_t::request_failed,
                                         "GameQuest player quest spot ensure failed");
        }
        co_return;
    }

    channel_client_t &_channels;
    route_client_t &_routes;
    game_api_store_t &_store;
    sample_topology_t &_topology;
    session_actor_manager_t &_actors;
    actor_gateway_t &_gateway;
    spot_handle_resolver_t &_spot_handles;
    std::optional<std::string> _player_id;
};

class server_assertion_http_handler_t
{
  public:
    using dependency_types = dependency_list_t<game_api_store_t>;
    using request_type = server_assertion_req_t;
    using reply_type = server_assertion_res_t;
    static constexpr const char *topic_name = server_assertion_req_t::packet_name;

    explicit server_assertion_http_handler_t (game_api_store_t &store) : _store (store) {}

    server_assertion_res_t handle (const server_assertion_req_t &)
    {
        return _store.assert_state ();
    }

  private:
    game_api_store_t &_store;
};

} // namespace zlink::samples::gamequest

int main (int argc, char **argv)
{
    using namespace zlink::framework;
    using namespace zlink::samples::gamequest;

    auto app = app_t::create ();
    const auto configuration = load_sample_configuration (app, argc, argv);
    const auto &topology = configuration.topology;
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (configuration.flow_log_path ())
          .trace_label (topology.api_name);
        options.services ().add_singleton<sample_topology_t> (
          std::make_unique<sample_topology_t> (topology));
        auto api_store = std::make_unique<game_api_store_t> ();
        auto *store_ptr = api_store.get ();
        options.services ().add_singleton<game_api_store_t> (std::move (api_store));
        auto spot_services = options.services ().build_provider ();
        add_gamequest_json_codecs (options.codecs ());
        add_gamequest_location_store (options, topology);
        options.add_client_server_channel (quest_owner_channel_for ("mission-a")).enable_client ();
        options.add_client_server_channel (quest_owner_channel_for ("mission-b")).enable_client ();
        /* 같은 spot route mesh를 양방향으로 쓴다: API는 owner spot으로 gameplay를 보내고, owner
         * spot은 같은 mesh로 이 노드의 entry spot에 notify를 보낸다. */
        auto quest_spot_route = options.add_route_mesh (sample_names_t::quest_spot_route);
        quest_spot_route.enable_server (topology.selected_api_spot_route_endpoint ());
        quest_spot_route.set_routing_id (topology.selected_api_rid ());
        quest_spot_route.enable_client ();
        options.configure_locations ().spot_router_channels[sample_names_t::quest_spot_discovery] =
          sample_names_t::quest_spot_route;
        options.add_spot_mesh (api_spot_mesh_for (topology.api_name))
          .set_routing_id (topology.selected_api_rid ())
          .enable_router (topology.selected_api_spot_router_endpoint ())
          .accept_route_mesh (sample_names_t::quest_spot_route)
          .add_entry_spot<player_entry_spot_t> ([store_ptr, spot_services] {
              return std::make_shared<player_entry_spot_t> (*store_ptr, spot_services);
          })
          .add_actor_factory<player_actor_factory_t> (gamequest_player_actor_type);
        options.add_stream_node (sample_names_t::stream_node)
          .bind (topology.selected_api_stream_endpoint ())
          .register_session<gamequest_session_t> ();
        options.http ()
          .listen (topology.selected_api_http_url ())
          .map_health ("/health")
          .map_post<server_assertion_http_handler_t> ("/self-check/assert");
    });
    return app.run (argc, argv);
}
