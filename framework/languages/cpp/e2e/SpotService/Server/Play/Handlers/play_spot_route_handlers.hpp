/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/spot_service_contracts.hpp"
#include "../../Shared/scenario_state.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace e2e = zlink::framework::e2e::spot_service;

inline std::string owner_node_rid_from_spot_rid (const std::string &spot_rid)
{
    constexpr std::string_view prefix = "user:";
    if (spot_rid.rfind (prefix, 0) != 0) {
        return "play-a";
    }
    const auto owner_begin = prefix.size ();
    const auto owner_end = spot_rid.find (':', owner_begin);
    if (owner_end == std::string::npos || owner_end == owner_begin) {
        return "play-a";
    }
    return spot_rid.substr (owner_begin, owner_end - owner_begin);
}

inline zlink::framework::spot_ref_t route_spot_ref (const std::string &node_rid,
                                                    const std::string &spot_rid)
{
    return zlink::framework::spot_ref_t{
      .mesh_name = e2e::spot_mesh,
      .node_rid = zlink::routing_id_t::from (node_rid),
      .spot_rid = zlink::routing_id_t::from (spot_rid)};
}

class route_spot_state_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t>;

    explicit route_spot_state_handler_t (zlink::framework::route_client_t &routes) :
        _routes (routes)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_state_route_req_t> ();
        const auto owner = request.target_node_rid.empty ()
                             ? (request.spot_rid.empty ()
                                  ? e2e::owner_node_rid_for_key (request.key)
                                  : owner_node_rid_from_spot_rid (request.spot_rid))
                             : request.target_node_rid;
        const auto spot_rid = request.spot_rid.empty ()
                                ? e2e::user_spot_rid_for_key (request.key)
                                : request.spot_rid;
        auto reply =
          _routes
            .request_to_node (e2e::route_channel, route_spot_ref (owner, spot_rid),
                              request.state)
            .packet_name ("StateReq")
            .async<e2e::state_res_t> ()
            .result ();
        if (!reply) {
            throw zlink::framework::framework_exception_t (
              reply.error_kind (),
              reply.error () ? reply.error ()->what () : "StateReq route failed");
        }

        zlink::framework::http_response_t response;
        response.body = nlohmann::json (reply.value ()).dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
};

class direct_spot_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t>;

    explicit direct_spot_route_handler_t (zlink::framework::route_client_t &routes) :
        _routes (routes)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::direct_spot_route_req_t> ();
        auto reply =
          _routes
            .request_to_node (
              e2e::route_channel, route_spot_ref (request.target_node_rid, request.spot_rid),
              e2e::direct_spot_req_t{request.source_actor_id, request.value})
            .packet_name ("DirectSpotReq")
            .async<e2e::direct_spot_res_t> ()
            .result ();
        if (!reply) {
            throw zlink::framework::framework_exception_t (
              reply.error_kind (),
              reply.error () ? reply.error ()->what () : "DirectSpotReq failed");
        }

        zlink::framework::http_response_t response;
        response.body = nlohmann::json (reply.value ()).dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
};

class direct_spot_command_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t>;

    explicit direct_spot_command_route_handler_t (zlink::framework::route_client_t &routes) :
        _routes (routes)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::direct_spot_route_req_t> ();
        _routes
          .send_to_node (
            e2e::route_channel, route_spot_ref (request.target_node_rid, request.spot_rid),
            e2e::direct_spot_msg_t{request.source_actor_id, request.value})
          .packet_name ("DirectSpotMsg")
          .submit ();

        zlink::framework::http_response_t response;
        response.body = nlohmann::json (e2e::spot_state_command_route_res_t{.accepted = true})
                          .dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
};

class spot_stage_probe_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t>;

    explicit spot_stage_probe_route_handler_t (zlink::framework::route_client_t &routes) :
        _routes (routes)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_stage_probe_req_t> ();
        auto reply =
          _routes
            .request_to_node (
              e2e::route_channel,
              route_spot_ref (owner_node_rid_from_spot_rid (request.spot_rid), request.spot_rid),
              e2e::stage_probe_req_t{.marker = request.marker, .delta = request.delta})
            .packet_name ("StageProbeReq")
            .timeout (std::chrono::seconds (3))
            .async<e2e::state_res_t> ()
            .result ();
        if (!reply) {
            throw zlink::framework::framework_exception_t (
              reply.error_kind (),
              reply.error () ? reply.error ()->what () : "StageProbeReq route failed");
        }

        zlink::framework::http_response_t response;
        response.body = nlohmann::json (reply.value ()).dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
};

class spot_stage_timer_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          scenario_state_t>;

    spot_stage_timer_route_handler_t (zlink::framework::route_client_t &routes,
                                      scenario_state_t &state) :
        _routes (routes), _state (state)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_stage_timer_req_t> ();
        _routes
          .send_to_node (
            e2e::route_channel,
            route_spot_ref (owner_node_rid_from_spot_rid (request.spot_rid), request.spot_rid),
            e2e::stage_timer_start_msg_t{.name = request.name, .period_ms = request.period_ms})
          .packet_name ("StageTimerStartMsg")
          .submit ();

        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json (e2e::spot_stage_timer_res_t{.spot_rid = request.spot_rid,
                                                      .name = request.name,
                                                      .started = true,
                                                      .evidence = _state.snapshot ()})
            .dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
    scenario_state_t &_state;
};

class spot_state_command_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t>;

    explicit spot_state_command_route_handler_t (zlink::framework::route_client_t &routes) :
        _routes (routes)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_state_command_route_req_t> ();
        _routes
          .send_to_node (
            e2e::route_channel, route_spot_ref (request.target_node_rid, request.spot_rid),
            e2e::direct_spot_msg_t{"sm-c1-client", request.marker})
          .packet_name ("DirectSpotMsg")
          .submit ();

        zlink::framework::http_response_t response;
        response.body = nlohmann::json (e2e::spot_state_command_route_res_t{.accepted = true})
                          .dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
};

class spot_publish_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::spot_publisher_client_t>;

    explicit spot_publish_route_handler_t (
      zlink::framework::spot_publisher_client_t &publisher) :
        _publisher (publisher)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_publish_route_req_t> ();
        auto published =
          _publisher
            .publish (e2e::publisher_channel, e2e::mesh_topic,
                      e2e::mesh_msg_t{"evt-sm-c1", request.marker})
            .result ();
        if (!published) {
            throw zlink::framework::framework_exception_t (
              published.error_kind (),
              published.error () ? published.error ()->what () : "SPOT mesh publish failed");
        }

        zlink::framework::http_response_t response;
        response.body = nlohmann::json (e2e::spot_publish_route_res_t{.accepted = true}).dump ();
        return response;
    }

  private:
    zlink::framework::spot_publisher_client_t &_publisher;
};

class spot_publish_wait_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;

    explicit spot_publish_wait_handler_t (scenario_state_t &state) : _state (state) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_publish_route_req_t> ();
        const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);
        do {
            auto snapshot = _state.snapshot ();
            if (has_publish_evidence (snapshot, request)) {
                zlink::framework::http_response_t response;
                response.body =
                  nlohmann::json (e2e::spot_publish_observe_res_t{
                    .operation = "spot.sm-c4-observe",
                    .spot_rid = request.spot_rid,
                    .marker = request.marker,
                    .received = true,
                    .evidence = std::move (snapshot)})
                    .dump ();
                return response;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        } while (std::chrono::steady_clock::now () < deadline);

        throw std::runtime_error ("timed out waiting for spot publish evidence");
    }

  private:
    static bool has_publish_evidence (const e2e::evidence_snapshot_t &snapshot,
                                      const e2e::spot_publish_route_req_t &request)
    {
        for (const auto &entry : snapshot.entries) {
            if (entry.marker == "MeshMsgReceived" && entry.spot_rid == request.spot_rid
                && entry.value == "evt-sm-c4:" + request.marker) {
                return true;
            }
        }
        return false;
    }

    scenario_state_t &_state;
};

class spot_worker_start_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t>;

    explicit spot_worker_start_route_handler_t (zlink::framework::route_client_t &routes) :
        _routes (routes)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_worker_start_req_t> ();
        auto reply =
          _routes
            .request_to_node (
              e2e::route_channel,
              route_spot_ref (owner_node_rid_from_spot_rid (request.spot_rid), request.spot_rid),
              request)
            .packet_name ("WorkerStartReq")
            .timeout (std::chrono::seconds (3))
            .async<e2e::spot_worker_start_res_t> ()
            .result ();
        if (!reply) {
            throw zlink::framework::framework_exception_t (
              reply.error_kind (),
              reply.error () ? reply.error ()->what () : "WorkerStartReq route failed");
        }

        zlink::framework::http_response_t response;
        response.body = nlohmann::json (reply.value ()).dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
};

class spot_worker_complete_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;

    explicit spot_worker_complete_handler_t (scenario_state_t &state) : _state (state) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_worker_complete_req_t> ();
        const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);
        do {
            auto snapshot = _state.snapshot ();
            if (has_worker_complete_evidence (snapshot, request)) {
                zlink::framework::http_response_t response;
                response.body =
                  nlohmann::json (e2e::spot_worker_complete_res_t{.spot_rid = request.spot_rid,
                                                                   .marker = request.marker,
                                                                   .completed = true,
                                                                   .evidence = std::move (
                                                                     snapshot)})
                    .dump ();
                return response;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        } while (std::chrono::steady_clock::now () < deadline);

        throw std::runtime_error ("timed out waiting for spot worker completion evidence");
    }

  private:
    static bool has_worker_complete_evidence (const e2e::evidence_snapshot_t &snapshot,
                                              const e2e::spot_worker_complete_req_t &request)
    {
        for (const auto &entry : snapshot.entries) {
            if (entry.marker == "WorkerCompleted" && entry.spot_rid == request.spot_rid
                && entry.value == request.marker) {
                return true;
            }
        }
        return false;
    }

    scenario_state_t &_state;
};

class spot_idle_close_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<scenario_state_t,
                                          zlink::framework::spot_node_manager_t>;

    spot_idle_close_route_handler_t (scenario_state_t &state,
                                     zlink::framework::spot_node_manager_t &spots) :
        _state (state), _spots (spots)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_idle_close_req_t> ();
        const auto created = _spots.get_or_create_spot (
          e2e::user_spot, zlink::framework::spot_rid_t::from_string (request.spot_rid),
          e2e::idle_close_msg_t{.name = request.name, .period_ms = request.period_ms});
        if (created.state != zlink::framework::spot_create_state_t::created) {
            throw std::runtime_error ("idle close spot already exists");
        }

        const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);
        do {
            auto snapshot = _state.snapshot ();
            if (has_idle_close_evidence (snapshot, request)) {
                zlink::framework::http_response_t response;
                response.body =
                  nlohmann::json (e2e::spot_idle_close_res_t{
                    .spot_rid = request.spot_rid,
                    .name = request.name,
                    .closed = true,
                    .evidence = std::move (snapshot)})
                    .dump ();
                return response;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
        } while (std::chrono::steady_clock::now () < deadline);

        throw std::runtime_error ("timed out waiting for spot idle close evidence");
    }

  private:
    static bool has_idle_close_evidence (const e2e::evidence_snapshot_t &snapshot,
                                         const e2e::spot_idle_close_req_t &request)
    {
        bool closed = false;
        bool closing = false;
        for (const auto &entry : snapshot.entries) {
            if (entry.spot_rid != request.spot_rid) {
                continue;
            }
            if (entry.marker == "SpotIdleTimerClosed"
                && entry.value == request.name + ":closed=true") {
                closed = true;
            }
            if (entry.marker == "SpotClosing") {
                closing = true;
            }
        }
        return closed && closing;
    }

    scenario_state_t &_state;
    zlink::framework::spot_node_manager_t &_spots;
};

class spot_overrun_start_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<scenario_state_t,
                                          zlink::framework::spot_node_manager_t>;

    spot_overrun_start_route_handler_t (scenario_state_t &state,
                                        zlink::framework::spot_node_manager_t &spots) :
        _state (state), _spots (spots)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_overrun_start_req_t> ();
        const auto created = _spots.get_or_create_spot (
          e2e::user_spot, zlink::framework::spot_rid_t::from_string (request.spot_rid),
          e2e::overrun_timer_msg_t{
            .name = request.name, .policy = request.policy, .period_ms = request.period_ms});
        if (created.state != zlink::framework::spot_create_state_t::created) {
            throw std::runtime_error ("overrun timer spot already exists");
        }

        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json (e2e::spot_overrun_start_res_t{.spot_rid = request.spot_rid,
                                                        .name = request.name,
                                                        .started = true,
                                                        .evidence = _state.snapshot ()})
            .dump ();
        return response;
    }

    scenario_state_t &_state;
    zlink::framework::spot_node_manager_t &_spots;
};

class spot_slow_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t>;

    explicit spot_slow_route_handler_t (zlink::framework::route_client_t &routes) :
        _routes (routes)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_slow_route_req_t> ();
        auto reply =
          _routes
            .request_to_node (
              e2e::route_channel, route_spot_ref (request.target_node_rid, request.spot_rid),
              e2e::slow_spot_req_t{request.value})
            .packet_name ("SlowSpotReq")
            .timeout (std::chrono::milliseconds (request.timeout_ms))
            .async<e2e::direct_spot_res_t> ()
            .result ();

        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json (e2e::spot_slow_route_res_t{.timed_out = !reply.has_value ()}).dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
};

class spot_missing_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t>;

    explicit spot_missing_route_handler_t (zlink::framework::route_client_t &routes) :
        _routes (routes)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_missing_route_req_t> ();
        auto missing_request =
          _routes
            .request_to_node (
              e2e::route_channel, route_spot_ref (request.target_node_rid, request.spot_rid),
              e2e::unhandled_spot_req_t{request.value})
            .packet_name ("MissingSpotReq")
            .timeout (std::chrono::milliseconds (1000))
            .async<e2e::direct_spot_res_t> ()
            .result ();
        _routes
          .send_to_node (
            e2e::route_channel, route_spot_ref (request.target_node_rid, request.spot_rid),
            e2e::unhandled_spot_req_t{request.value + ":send"})
          .packet_name ("MissingSpotMsg")
          .submit ();

        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json (e2e::spot_missing_route_res_t{
            .request_failed = !missing_request.has_value (),
            .command_sent = true})
            .dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
};

class spot_missing_handler_request_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          scenario_state_t>;

    spot_missing_handler_request_handler_t (zlink::framework::route_client_t &routes,
                                            scenario_state_t &state) :
        _routes (routes), _state (state)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_missing_handler_req_t> ();
        auto reply =
          _routes
            .request_to_node (
              e2e::route_channel,
              route_spot_ref (owner_node_rid_from_spot_rid (request.spot_rid), request.spot_rid),
              e2e::unhandled_spot_req_t{"missing-handler"})
            .packet_name ("MissingSpotReq")
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::direct_spot_res_t> ()
            .result ();

        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json (e2e::spot_missing_handler_res_t{
            .spot_rid = request.spot_rid, .failed = !reply.has_value (), .evidence = _state.snapshot ()})
            .dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
    scenario_state_t &_state;
};

class spot_missing_handler_command_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          scenario_state_t>;

    spot_missing_handler_command_handler_t (zlink::framework::route_client_t &routes,
                                            scenario_state_t &state) :
        _routes (routes), _state (state)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_missing_command_req_t> ();
        _routes
          .send_to_node (
            e2e::route_channel,
            route_spot_ref (owner_node_rid_from_spot_rid (request.spot_rid), request.spot_rid),
            e2e::unhandled_spot_req_t{request.marker})
          .packet_name ("MissingSpotMsg")
          .submit ();

        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json (e2e::spot_missing_command_res_t{
            .spot_rid = request.spot_rid,
            .marker = request.marker,
            .sent = true,
            .evidence = _state.snapshot ()})
            .dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
    scenario_state_t &_state;
};

class spot_missing_target_request_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t>;

    explicit spot_missing_target_request_handler_t (zlink::framework::route_client_t &routes) :
        _routes (routes)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_missing_target_req_t> ();
        auto reply =
          _routes
            .request_to_node (
              e2e::route_channel,
              route_spot_ref (owner_node_rid_from_spot_rid (request.spot_rid), request.spot_rid),
              e2e::direct_spot_req_t{"missing-target", "noop"})
            .packet_name ("DirectSpotReq")
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::direct_spot_res_t> ()
            .result ();

        zlink::framework::http_response_t response;
        response.body = nlohmann::json (e2e::spot_missing_target_res_t{
                          .spot_rid = request.spot_rid, .failed = !reply.has_value ()})
                          .dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
};

class spot_outbound_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t>;

    explicit spot_outbound_route_handler_t (zlink::framework::route_client_t &routes) :
        _routes (routes)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_outbound_route_req_t> ();
        auto reply =
          _routes
            .request_to_node (
              e2e::route_channel, route_spot_ref (request.target_node_rid, request.spot_rid),
              e2e::outbound_req_t{request.marker})
            .packet_name ("SpotOutboundReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::outbound_res_t> ()
            .result ();
        if (!reply) {
            throw zlink::framework::framework_exception_t (
              reply.error_kind (),
              reply.error () ? reply.error ()->what () : "SpotOutboundReq failed");
        }

        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json (e2e::spot_outbound_route_res_t{
            .spot_rid = request.spot_rid, .marker = request.marker, .accepted = true})
            .dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
};

class spot_outbound_negative_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t>;

    explicit spot_outbound_negative_route_handler_t (zlink::framework::route_client_t &routes) :
        _routes (routes)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_outbound_route_req_t> ();
        auto reply =
          _routes
            .request_to_node (
              e2e::route_channel, route_spot_ref (request.target_node_rid, request.spot_rid),
              e2e::outbound_req_t{request.marker})
            .packet_name ("SpotOutboundNegativeReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::outbound_res_t> ()
            .result ();
        if (!reply) {
            throw zlink::framework::framework_exception_t (
              reply.error_kind (),
              reply.error () ? reply.error ()->what () : "SpotOutboundNegativeReq failed");
        }

        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json (e2e::spot_outbound_route_res_t{
            .spot_rid = request.spot_rid, .marker = request.marker, .accepted = true})
            .dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
};

class spot_to_spot_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t>;

    explicit spot_to_spot_route_handler_t (zlink::framework::route_client_t &routes) :
        _routes (routes)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_to_spot_route_req_t> ();
        auto reply = request_with_retry (request);
        if (!reply) {
            throw zlink::framework::framework_exception_t (
              reply.error_kind (),
              reply.error () ? reply.error ()->what () : "SpotToSpotDirectReq failed");
        }

        zlink::framework::http_response_t response;
        response.body = nlohmann::json (reply.value ()).dump ();
        return response;
    }

  private:
    zlink::framework::result_t<e2e::spot_to_spot_route_res_t>
    request_with_retry (const e2e::spot_to_spot_route_req_t &request)
    {
        auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);
        zlink::framework::result_t<e2e::spot_to_spot_route_res_t> last =
          zlink::framework::result_t<e2e::spot_to_spot_route_res_t>::failure (
            zlink::framework::framework_error_kind_t::timeout, "SpotToSpotDirectReq timed out");
        while (std::chrono::steady_clock::now () < deadline) {
            auto reply =
              _routes
                .request_to_node (
                  e2e::route_channel,
                  route_spot_ref (request.source_node_rid, request.source_spot_rid), request)
                .packet_name ("SpotToSpotDirectReq")
                .timeout (std::chrono::milliseconds (2000))
                .async<e2e::spot_to_spot_route_res_t> ()
                .result ();
            if (reply) {
                return reply;
            }
            last = std::move (reply);
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
        return last;
    }

    zlink::framework::route_client_t &_routes;
};

class spot_to_spot_timeout_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t>;

    explicit spot_to_spot_timeout_route_handler_t (
      zlink::framework::route_client_t &routes) :
        _routes (routes)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_to_spot_route_req_t> ();
        auto reply =
          _routes
            .request_to_node (
              e2e::route_channel,
              route_spot_ref (request.source_node_rid, request.source_spot_rid), request)
            .packet_name ("SpotToSpotTimeoutReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::spot_to_spot_timeout_route_res_t> ()
            .result ();
        if (!reply) {
            throw zlink::framework::framework_exception_t (
              reply.error_kind (),
              reply.error () ? reply.error ()->what () : "SpotToSpotTimeoutReq failed");
        }

        zlink::framework::http_response_t response;
        response.body = nlohmann::json (reply.value ()).dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
};

class spot_to_spot_negative_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t>;

    explicit spot_to_spot_negative_route_handler_t (
      zlink::framework::route_client_t &routes) :
        _routes (routes)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_to_spot_route_req_t> ();
        auto reply =
          _routes
            .request_to_node (
              e2e::route_channel,
              route_spot_ref (request.source_node_rid, request.source_spot_rid), request)
            .packet_name ("SpotToSpotNegativeReq")
            .timeout (std::chrono::milliseconds (3000))
            .async<e2e::spot_to_spot_negative_route_res_t> ()
            .result ();
        if (!reply) {
            throw zlink::framework::framework_exception_t (
              reply.error_kind (),
              reply.error () ? reply.error ()->what () : "SpotToSpotNegativeReq failed");
        }

        zlink::framework::http_response_t response;
        response.body = nlohmann::json (reply.value ()).dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
};
