/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../game_quest_server_role.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::gamequest
{

class subscribe_quest_handler_t
{
  public:
    using request_type = subscribe_quest_req_t;
    using reply_type = subscribe_quest_res_t;
    using dependency_types = zlink::framework::dependency_list_t<game_quest_server_role_t>;
    static constexpr const char *topic_name = "SubscribeQuestReq";
    explicit subscribe_quest_handler_t (game_quest_server_role_t &server) : _server (server) {}
    subscribe_quest_res_t handle (const subscribe_quest_req_t &request)
    {
        return _server.subscribe_quest (request.player_id);
    }
  private:
    game_quest_server_role_t &_server;
};

class unlock_feature_handler_t
{
  public:
    using request_type = unlock_feature_req_t;
    using reply_type = event_res_t;
    using dependency_types = zlink::framework::dependency_list_t<game_quest_server_role_t>;
    static constexpr const char *topic_name = "UnlockFeatureReq";
    explicit unlock_feature_handler_t (game_quest_server_role_t &server) : _server (server) {}
    event_res_t handle (const unlock_feature_req_t &request) { return _server.unlock_feature (request); }
  private:
    game_quest_server_role_t &_server;
};

class get_quest_progress_handler_t
{
  public:
    using request_type = get_quest_progress_req_t;
    using reply_type = get_quest_progress_res_t;
    using dependency_types = zlink::framework::dependency_list_t<game_quest_server_role_t>;
    static constexpr const char *topic_name = "GetQuestProgressReq";
    explicit get_quest_progress_handler_t (game_quest_server_role_t &server) : _server (server) {}
    get_quest_progress_res_t handle (const get_quest_progress_req_t &request)
    {
        return _server.get_progress (request.player_id);
    }
  private:
    game_quest_server_role_t &_server;
};

class sync_quest_progress_handler_t
{
  public:
    using request_type = sync_quest_progress_req_t;
    using reply_type = sync_quest_progress_res_t;
    using dependency_types = zlink::framework::dependency_list_t<game_quest_server_role_t>;
    static constexpr const char *topic_name = "SyncQuestProgressReq";
    explicit sync_quest_progress_handler_t (game_quest_server_role_t &server) : _server (server) {}
    sync_quest_progress_res_t handle (const sync_quest_progress_req_t &request)
    {
        return _server.sync_progress (request.player_id);
    }
  private:
    game_quest_server_role_t &_server;
};

class get_gameplay_snapshot_handler_t
{
  public:
    using request_type = get_gameplay_snapshot_req_t;
    using reply_type = get_gameplay_snapshot_res_t;
    using dependency_types = zlink::framework::dependency_list_t<game_quest_server_role_t>;
    static constexpr const char *topic_name = "GetGameplaySnapshotReq";
    explicit get_gameplay_snapshot_handler_t (game_quest_server_role_t &server) : _server (server) {}
    get_gameplay_snapshot_res_t handle (const get_gameplay_snapshot_req_t &request)
    {
        return _server.get_snapshot (request.player_id);
    }
  private:
    game_quest_server_role_t &_server;
};

class delete_quest_projection_handler_t
{
  public:
    using request_type = delete_quest_projection_req_t;
    using reply_type = quest_progress_t;
    using dependency_types = zlink::framework::dependency_list_t<game_quest_server_role_t>;
    static constexpr const char *topic_name = "DeleteQuestProjectionReq";
    explicit delete_quest_projection_handler_t (game_quest_server_role_t &server) : _server (server) {}
    quest_progress_t handle (const delete_quest_projection_req_t &request)
    {
        return _server.delete_projection (request);
    }
  private:
    game_quest_server_role_t &_server;
};

class rebuild_quest_projection_handler_t
{
  public:
    using request_type = rebuild_quest_projection_req_t;
    using reply_type = quest_progress_t;
    using dependency_types = zlink::framework::dependency_list_t<game_quest_server_role_t>;
    static constexpr const char *topic_name = "RebuildQuestProjectionReq";
    explicit rebuild_quest_projection_handler_t (game_quest_server_role_t &server) : _server (server) {}
    quest_progress_t handle (const rebuild_quest_projection_req_t &request)
    {
        return _server.rebuild_projection (request);
    }
  private:
    game_quest_server_role_t &_server;
};

class game_quest_server_assert_handler_t
{
  public:
    using request_type = game_quest_server_assert_req_t;
    using reply_type = game_quest_server_assert_res_t;
    using dependency_types = zlink::framework::dependency_list_t<game_quest_server_role_t>;
    static constexpr const char *topic_name = "GameQuestServerAssertReq";
    explicit game_quest_server_assert_handler_t (game_quest_server_role_t &server) : _server (server) {}
    game_quest_server_assert_res_t handle (const game_quest_server_assert_req_t &)
    {
        return _server.assert_server ();
    }
  private:
    game_quest_server_role_t &_server;
};

} // namespace zlink::samples::gamequest
