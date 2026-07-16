/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../../Shared/spot_service_contracts.hpp"
#include "../../Shared/scenario_state.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace e2e = zlink::framework::e2e::spot_service;

/* Resolves the spot rid from an E2E payload once and returns the opaque
 * messaging handle. A missing live location row fails with the typed routing
 * error so the negative scenarios do not wait for a request timeout. */
inline std::optional<zlink::framework::spot_handle_t>
try_resolve_spot_handle (zlink::framework::spot_handle_resolver_t &handles,
                         const std::string &spot_rid)
{
    return handles.resolve_spot_handle (zlink::framework::spot_rid_t::from_string (spot_rid))
      .result ()
      .value ();
}

inline zlink::framework::spot_handle_t
resolve_required_spot_handle (zlink::framework::spot_handle_resolver_t &handles,
                              const std::string &spot_rid)
{
    auto handle = try_resolve_spot_handle (handles, spot_rid);
    if (!handle) {
        throw zlink::framework::framework_exception_t (
          zlink::framework::framework_error_kind_t::spot_route_not_found,
          "Spot '" + spot_rid + "' has no live location row.");
    }
    return *handle;
}

class route_spot_state_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          zlink::framework::spot_handle_resolver_t>;

    route_spot_state_handler_t (zlink::framework::route_client_t &routes,
        zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_state_route_req_t> ();
        const auto spot_rid = request.spot_rid.empty ()
                                ? e2e::user_spot_rid_for_key (request.key)
                                : request.spot_rid;
        auto reply =
          _routes
            .request_to_spot (resolve_required_spot_handle (_handles, spot_rid), request.state)
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
    zlink::framework::spot_handle_resolver_t &_handles;
};

class direct_spot_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          zlink::framework::spot_handle_resolver_t>;

    direct_spot_route_handler_t (zlink::framework::route_client_t &routes,
        zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::direct_spot_route_req_t> ();
        auto reply =
          _routes
            .request_to_spot (resolve_required_spot_handle (_handles, request.spot_rid),
                              e2e::direct_spot_req_t{request.source_actor_id, request.value})
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
    zlink::framework::spot_handle_resolver_t &_handles;
};

class direct_spot_command_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          zlink::framework::spot_handle_resolver_t>;

    direct_spot_command_route_handler_t (zlink::framework::route_client_t &routes,
        zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::direct_spot_route_req_t> ();
        _routes
          .send_to_spot (resolve_required_spot_handle (_handles, request.spot_rid),
                         e2e::direct_spot_msg_t{request.source_actor_id, request.value})
          .submit ();

        zlink::framework::http_response_t response;
        response.body = nlohmann::json (e2e::spot_state_command_route_res_t{.accepted = true})
                          .dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
    zlink::framework::spot_handle_resolver_t &_handles;
};

class spot_stage_probe_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          zlink::framework::spot_handle_resolver_t>;

    spot_stage_probe_route_handler_t (zlink::framework::route_client_t &routes,
        zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_stage_probe_req_t> ();
        auto reply =
          _routes
            .request_to_spot (
              resolve_required_spot_handle (_handles, request.spot_rid),
              e2e::stage_probe_req_t{.marker = request.marker, .delta = request.delta})
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
    zlink::framework::spot_handle_resolver_t &_handles;
};

class spot_stage_timer_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          scenario_state_t,
                                          zlink::framework::spot_handle_resolver_t>;

    spot_stage_timer_route_handler_t (zlink::framework::route_client_t &routes,
                                      scenario_state_t &state,
                                      zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _state (state), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_stage_timer_req_t> ();
        _routes
          .send_to_spot (
            resolve_required_spot_handle (_handles, request.spot_rid),
            e2e::stage_timer_start_msg_t{.name = request.name, .period_ms = request.period_ms})
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
    zlink::framework::spot_handle_resolver_t &_handles;
};

class spot_state_command_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          zlink::framework::spot_handle_resolver_t>;

    spot_state_command_route_handler_t (zlink::framework::route_client_t &routes,
        zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_state_command_route_req_t> ();
        _routes
          .send_to_spot (resolve_required_spot_handle (_handles, request.spot_rid),
                         e2e::direct_spot_msg_t{"sm-c1-client", request.marker})
          .submit ();

        zlink::framework::http_response_t response;
        response.body = nlohmann::json (e2e::spot_state_command_route_res_t{.accepted = true})
                          .dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
    zlink::framework::spot_handle_resolver_t &_handles;
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
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          zlink::framework::spot_handle_resolver_t>;

    spot_worker_start_route_handler_t (zlink::framework::route_client_t &routes,
        zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_worker_start_req_t> ();
        auto reply =
          _routes
            .request_to_spot (resolve_required_spot_handle (_handles, request.spot_rid), request)
            .timeout (std::chrono::seconds (30))
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
    zlink::framework::spot_handle_resolver_t &_handles;
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
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          zlink::framework::spot_handle_resolver_t>;

    spot_slow_route_handler_t (zlink::framework::route_client_t &routes,
        zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_slow_route_req_t> ();
        auto reply =
          _routes
            .request_to_spot (resolve_required_spot_handle (_handles, request.spot_rid),
                              e2e::slow_spot_req_t{request.value})
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
    zlink::framework::spot_handle_resolver_t &_handles;
};

class spot_missing_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          zlink::framework::spot_handle_resolver_t>;

    spot_missing_route_handler_t (zlink::framework::route_client_t &routes,
        zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_missing_route_req_t> ();
        const auto target = try_resolve_spot_handle (_handles, request.spot_rid);
        bool request_failed = true;
        if (target) {
            auto missing_request = _routes
                                     .request_to_spot (*target,
                                                       e2e::unhandled_spot_req_t{request.value})
                                     .timeout (std::chrono::milliseconds (1000))
                                     .async<e2e::direct_spot_res_t> ()
                                     .result ();
            request_failed = !missing_request.has_value ();
            try {
                _routes
                  .send_to_spot (*target, e2e::unhandled_spot_msg_t{e2e::unhandled_spot_req_t{
                                            request.value + ":send"}})
                  .submit ();
            }
            catch (const zlink::framework::framework_exception_t &) {
            }
        }

        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json (e2e::spot_missing_route_res_t{
            .request_failed = request_failed,
            .command_sent = true})
            .dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
    zlink::framework::spot_handle_resolver_t &_handles;
};

class spot_missing_handler_request_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          scenario_state_t,
                                          zlink::framework::spot_handle_resolver_t>;

    spot_missing_handler_request_handler_t (zlink::framework::route_client_t &routes,
                                            scenario_state_t &state,
                                            zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _state (state), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_missing_handler_req_t> ();
        auto reply =
          _routes
            .request_to_spot (resolve_required_spot_handle (_handles, request.spot_rid),
                              e2e::unhandled_spot_req_t{"missing-handler"})
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
    zlink::framework::spot_handle_resolver_t &_handles;
};

class spot_missing_handler_command_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          scenario_state_t,
                                          zlink::framework::spot_handle_resolver_t>;

    spot_missing_handler_command_handler_t (zlink::framework::route_client_t &routes,
                                            scenario_state_t &state,
                                            zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _state (state), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_missing_command_req_t> ();
        _routes
          .send_to_spot (resolve_required_spot_handle (_handles, request.spot_rid),
                         e2e::unhandled_spot_msg_t{e2e::unhandled_spot_req_t{request.marker}})
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
    zlink::framework::spot_handle_resolver_t &_handles;
};

class spot_missing_target_request_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          zlink::framework::spot_handle_resolver_t>;

    spot_missing_target_request_handler_t (zlink::framework::route_client_t &routes,
        zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_missing_target_req_t> ();
        bool failed = true;
        const auto target = try_resolve_spot_handle (_handles, request.spot_rid);
        if (target) {
            auto reply = _routes
                           .request_to_spot (*target,
                                             e2e::direct_spot_req_t{"missing-target", "noop"})
                           .timeout (std::chrono::milliseconds (2000))
                           .async<e2e::direct_spot_res_t> ()
                           .result ();
            failed = !reply.has_value ();
        }

        zlink::framework::http_response_t response;
        response.body = nlohmann::json (e2e::spot_missing_target_res_t{
                          .spot_rid = request.spot_rid, .failed = failed})
                          .dump ();
        return response;
    }

  private:
    zlink::framework::route_client_t &_routes;
    zlink::framework::spot_handle_resolver_t &_handles;
};

class spot_outbound_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          zlink::framework::spot_handle_resolver_t>;

    spot_outbound_route_handler_t (zlink::framework::route_client_t &routes,
        zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_outbound_route_req_t> ();
        auto reply =
          _routes
            .request_to_spot (resolve_required_spot_handle (_handles, request.spot_rid),
                              e2e::outbound_req_t{request.marker})
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
    zlink::framework::spot_handle_resolver_t &_handles;
};

class spot_outbound_negative_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          zlink::framework::spot_handle_resolver_t>;

    spot_outbound_negative_route_handler_t (zlink::framework::route_client_t &routes,
        zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_outbound_route_req_t> ();
        auto reply =
          _routes
            .request_to_spot (resolve_required_spot_handle (_handles, request.spot_rid),
                              e2e::outbound_negative_req_t{e2e::outbound_req_t{request.marker}})
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
    zlink::framework::spot_handle_resolver_t &_handles;
};

class spot_to_spot_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          zlink::framework::spot_handle_resolver_t>;

    spot_to_spot_route_handler_t (zlink::framework::route_client_t &routes,
        zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_to_spot_route_req_t> ();
        const auto source = try_resolve_spot_handle (_handles, request.source_spot_rid);
        if (!source) {
            throw zlink::framework::framework_exception_t (
              zlink::framework::framework_error_kind_t::spot_route_not_found,
              "SpotToSpotDirectReq source spot was not resolved");
        }
        auto reply =
          _routes.request_to_spot (*source, e2e::spot_to_spot_direct_req_t{request})
            .timeout (std::chrono::milliseconds (2000))
            .async<e2e::spot_to_spot_route_res_t> ()
            .result ();
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
    zlink::framework::route_client_t &_routes;
    zlink::framework::spot_handle_resolver_t &_handles;
};

class spot_to_spot_timeout_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          zlink::framework::spot_handle_resolver_t>;

    spot_to_spot_timeout_route_handler_t (zlink::framework::route_client_t &routes,
        zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_to_spot_route_req_t> ();
        auto reply =
          _routes
            .request_to_spot (resolve_required_spot_handle (_handles, request.source_spot_rid),
                              e2e::spot_to_spot_timeout_req_t{request})
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
    zlink::framework::spot_handle_resolver_t &_handles;
};

class spot_to_spot_negative_route_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t,
                                          zlink::framework::spot_handle_resolver_t>;

    spot_to_spot_negative_route_handler_t (zlink::framework::route_client_t &routes,
        zlink::framework::spot_handle_resolver_t &handles) :
        _routes (routes), _handles (handles)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_to_spot_route_req_t> ();
        auto reply =
          _routes
            .request_to_spot (resolve_required_spot_handle (_handles, request.source_spot_rid),
                              e2e::spot_to_spot_negative_req_t{request})
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
    zlink::framework::spot_handle_resolver_t &_handles;
};
