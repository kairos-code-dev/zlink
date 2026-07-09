/* SPDX-License-Identifier: MPL-2.0 */

#include "../Configuration/location_store.hpp"
#include "../Configuration/sample_names.hpp"
#include "../Configuration/sample_topology.hpp"
#include "../common_codecs.hpp"
#include "../../sample_log_dir.hpp"

#include <zlink/framework.hpp>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace zlink::samples::gamequest
{

using namespace framework;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

struct http_endpoint_t
{
    std::string host;
    std::string port;
};

http_endpoint_t parse_http_url (const std::string &url)
{
    const std::string prefix = "http://";
    const auto authority_begin = url.rfind (prefix, 0) == 0 ? prefix.size () : 0;
    const auto path_begin = url.find ('/', authority_begin);
    const auto authority = url.substr (authority_begin, path_begin - authority_begin);
    const auto colon = authority.rfind (':');
    if (colon == std::string::npos) {
        return {authority, "80"};
    }
    return {authority.substr (0, colon), authority.substr (colon + 1)};
}

bool post_notify (const std::string &base_url, const notify_quest_progress_req_t &request)
{
    const auto endpoint = parse_http_url (base_url);
    boost::asio::io_context io;
    tcp::resolver resolver (io);
    beast::tcp_stream stream (io);
    stream.connect (resolver.resolve (endpoint.host, endpoint.port));

    http::request<http::string_body> http_request{http::verb::post, "/internal/notify", 11};
    http_request.set (http::field::host, endpoint.host + ":" + endpoint.port);
    http_request.set (http::field::content_type, "application/json");
    http_request.body () = nlohmann::json (request).dump ();
    http_request.prepare_payload ();
    http::write (stream, http_request);

    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read (stream, buffer, response);
    beast::error_code ignored;
    stream.socket ().shutdown (tcp::socket::shutdown_both, ignored);
    if (response.result_int () < 200 || response.result_int () >= 300) {
        return false;
    }
    std::istringstream body (response.body ());
    nlohmann::json decoded;
    body >> decoded;
    return decoded.get<notify_quest_progress_res_t> ().delivered;
}

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

class player_quest_spot_t : public spot_t
{
  public:
    player_quest_spot_t (quest_store_t &store, sample_topology_t topology) :
        _store (store), _topology (std::move (topology))
    {
    }

    void configure (spot_context_t &context)
    {
        context.handlers ()
          .add_handler<&player_quest_spot_t::apply> (apply_gameplay_event_req_t::packet_name)
          .add_handler<&player_quest_spot_t::sync> (sync_quest_progress_req_t::packet_name)
          .add_handler<&player_quest_spot_t::get> (get_quest_progress_req_t::packet_name);
    }

    spot_create_response_t on_create (const zlink::framework::message_t &request)
    {
        auto create = request.decode<player_quest_spot_create_req_t> ();
        _player_id = create.player_id;
        std::cerr << "gamequest player quest spot ready player=" << _player_id
                  << " spot=" << _player_id << "\n";
        return spot_create_response_t::accept ();
    }

    apply_gameplay_event_res_t apply (const apply_gameplay_event_req_t &request)
    {
        auto result = _store.apply (request.event);
        bool delivered = false;
        try {
            delivered =
              post_notify (_topology.api_http_url_for (request.event.source_api),
                           notify_quest_progress_req_t{request.event.player_id,
                                                       result.projection,
                                                       result.completed_quest_id});
        }
        catch (const std::exception &error) {
            std::cerr << "gamequest mission projection kept while stream notify failed."
                      << " player=" << request.event.player_id
                      << " error=" << error.what () << "\n";
        }
        std::cerr << "gamequest mission notified source=" << request.event.source_api
                  << " player=" << request.event.player_id
                  << " delivered=" << (delivered ? "true" : "false") << "\n";
        return result;
    }

    sync_quest_progress_res_t sync (const sync_quest_progress_req_t &request)
    {
        return {_store.projection (request.player_id)};
    }

    get_quest_progress_res_t get (const get_quest_progress_req_t &request)
    {
        return {_store.projection (request.player_id)};
    }

  private:
    quest_store_t &_store;
    sample_topology_t _topology;
    std::string _player_id;
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

    const sample_topology_t topology;
    auto quest_store = std::make_unique<quest_store_t> ();
    auto *quest_store_ptr = quest_store.get ();
    auto app = app_t::create ();
    app.add_zlink_framework ([&] (zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (message_flow_log_mode_t::key_transitions)
          .trace_log_file (gamequest_flow_log_path (topology.mission_name))
          .trace_label (topology.mission_name);
        options.services ().add_singleton<quest_store_t> (std::move (quest_store));
        options.services ().add_singleton<sample_topology_t> (
          std::make_unique<sample_topology_t> (topology));
        add_gamequest_json_codecs (options.codecs ());
        add_gamequest_location_store (options, topology);
        options.add_client_server_channel (quest_owner_channel_for (topology.mission_name))
          .enable_server (topology.selected_mission_route_endpoint ())
          .set_routing_id (topology.selected_mission_rid ())
          .use_handler_group ("quest-owner");
        options.add_route_mesh_channel (quest_spot_route_channel_for (topology.mission_name))
          .enable_server (topology.selected_mission_spot_route_endpoint ())
          .set_routing_id (topology.selected_mission_rid ());
        options.add_spot_mesh (sample_names_t::quest_spot_discovery)
          .enable_router (topology.selected_mission_spot_router_endpoint ())
          .set_routing_id (topology.selected_mission_rid ())
          .enable_pub_sub (topology.selected_mission_spot_endpoint ())
          .accept_route_mesh (quest_spot_route_channel_for (topology.mission_name))
          .add_spot<player_quest_spot_t> (
            sample_names_t::player_quest_spot,
            [quest_store_ptr, topology] {
                return std::make_shared<player_quest_spot_t> (*quest_store_ptr, topology);
            });
        options.handlers ()
          .group ("quest-owner")
          .add<ensure_player_quest_spot_handler_t> ();
    });
    return app.run (argc, argv);
}
