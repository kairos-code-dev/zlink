/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../../Shared/evidence_store.hpp"
#include "../../../Shared/runtime_monitoring_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <csignal>
#include <stdexcept>
#include <string>
#include <thread>
#include <mutex>

namespace zlink::framework::e2e::runtime_monitoring::service
{

class application_gate_t
{
  public:
    void arm ()
    {
        std::lock_guard lock (_mutex);
        _armed = true;
        _entered = false;
    }

    void wait_if_armed ()
    {
        std::unique_lock lock (_mutex);
        if (!_armed)
            return;
        _entered = true;
        _changed.notify_all ();
        _changed.wait (lock, [&] { return !_armed; });
    }

    bool wait_until_entered (std::chrono::milliseconds timeout)
    {
        std::unique_lock lock (_mutex);
        return _changed.wait_for (lock, timeout, [&] { return _entered; });
    }

    void release ()
    {
        std::lock_guard lock (_mutex);
        _armed = false;
        _changed.notify_all ();
    }

  private:
    std::mutex _mutex;
    std::condition_variable _changed;
    bool _armed = false;
    bool _entered = false;
};

class monitoring_spot_t;

struct failing_timer_handler_t
{
    void handle (monitoring_spot_t &, const zlink::framework::timer_tick_t &) const;
};

class monitoring_spot_t : public zlink::framework::spot_t
{
  public:
    ~monitoring_spot_t () override = default;

    void configure (zlink::framework::spot_context_t &context)
    {
        using namespace std::chrono_literals;
        context.add_timer<failing_timer_handler_t> ("failing", 50ms);
        context.add_timer<failing_timer_handler_t> (
          "stopping", 50ms, {.stop_on_unhandled_exception = true});
    }
};

class monitoring_subject_spot_t : public zlink::framework::spot_t
{
  public:
    explicit monitoring_subject_spot_t (server::evidence_store_t &evidence) :
        _evidence (evidence)
    {
    }

    void configure (zlink::framework::spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_subscribe<
          &monitoring_subject_spot_t::on_multicast> ("monitor.multicast");
    }

    void on_multicast (const multicast_probe_t &message)
    {
        _evidence.add (
          "multicast-received|rid=" + _evidence.rid () + "|spot="
          + std::string (_context.spot_rid ().value ()) + "|marker="
          + message.marker + "|sequence=" + std::to_string (message.sequence));
    }

  private:
    server::evidence_store_t &_evidence;
    zlink::framework::spot_context_t _context;
};

class monitoring_slow_spot_t : public zlink::framework::spot_t
{
  public:
    monitoring_slow_spot_t (server::evidence_store_t &evidence,
                            application_gate_t &gate) :
        _evidence (evidence), _gate (gate)
    {
    }

    void configure (zlink::framework::spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_subscribe<
          &monitoring_slow_spot_t::on_multicast> ("monitor.prefill");
        context.handlers ().add_subscribe<
          &monitoring_slow_spot_t::on_multicast> ("monitor.multicast");
    }

    void on_multicast (const multicast_probe_t &message)
    {
        if (message.marker == "mon-c1-application-gate") {
            _evidence.add ("application-gate|state=entered");
            _gate.wait_if_armed ();
            _evidence.add ("application-gate|state=released");
        } else {
            std::this_thread::sleep_for (std::chrono::milliseconds (25));
        }
        _evidence.add (
          "multicast-received|rid=" + _evidence.rid () + "|spot="
          + std::string (_context.spot_rid ().value ()) + "|marker="
          + message.marker + "|sequence=" + std::to_string (message.sequence));
    }

  private:
    server::evidence_store_t &_evidence;
    application_gate_t &_gate;
    zlink::framework::spot_context_t _context;
};

inline void failing_timer_handler_t::handle (monitoring_spot_t &,
                                             const zlink::framework::timer_tick_t &) const
{
    throw std::runtime_error ("monitoring timer failure");
}

class profile_request_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<server::evidence_store_t>;
    using request_type = profile_req_t;
    using reply_type = profile_res_t;

    explicit profile_request_handler_t (server::evidence_store_t &evidence) :
        _evidence (evidence)
    {
    }

    profile_res_t handle (const profile_req_t &request)
    {
        _evidence.add ("profile-request|rid=" + _evidence.rid () + "|marker=" + request.marker
                       + "|value=" + request.value);
        return {.value = "profile:" + request.value,
                .provider_rid = _evidence.rid (),
                .marker = request.marker};
    }

  private:
    server::evidence_store_t &_evidence;
};

class mesh_profile_request_dispatch_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<server::evidence_store_t>;
    using request_type = profile_req_t;
    using reply_type = profile_res_t;

    explicit mesh_profile_request_dispatch_handler_t (
      server::evidence_store_t &evidence) :
        _evidence (evidence)
    {
    }

    profile_res_t
    handle (const profile_req_t &request,
            const zlink::framework::route_handler_context_t &)
    {
        _evidence.add (
          "mesh-profile-request|rid=" + _evidence.rid ()
          + "|marker=" + request.marker);
        return {.value = "profile:" + request.value,
                .provider_rid = _evidence.rid (),
                .marker = request.marker};
    }

  private:
    server::evidence_store_t &_evidence;
};

class mesh_application_gate_dispatch_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<
      application_gate_t,
      server::evidence_store_t>;
    using request_type = application_gate_req_t;
    using reply_type = application_gate_res_t;

    mesh_application_gate_dispatch_handler_t (
      application_gate_t &gate,
      server::evidence_store_t &evidence) :
        _gate (gate), _evidence (evidence)
    {
    }

    application_gate_res_t
    handle (const application_gate_req_t &request,
            const zlink::framework::route_handler_context_t &)
    {
        _evidence.add ("application-gate|state=entered");
        _gate.wait_if_armed ();
        _evidence.add ("application-gate|state=released");
        return {.marker = request.marker, .provider_rid = _evidence.rid ()};
    }

  private:
    application_gate_t &_gate;
    server::evidence_store_t &_evidence;
};

class application_gate_arm_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<application_gate_t>;

    explicit application_gate_arm_handler_t (application_gate_t &gate) :
        _gate (gate)
    {
    }

    zlink::framework::http_response_t
    handle (const zlink::framework::http_request_t &)
    {
        _gate.arm ();
        return {.body = R"({"status":"armed"})"};
    }

  private:
    application_gate_t &_gate;
};

class application_gate_wait_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<application_gate_t>;

    explicit application_gate_wait_handler_t (application_gate_t &gate) :
        _gate (gate)
    {
    }

    zlink::framework::http_response_t
    handle (const zlink::framework::http_request_t &)
    {
        if (!_gate.wait_until_entered (std::chrono::seconds (10)))
            return {.status = 504, .body = R"({"status":"timeout"})"};
        return {.body = R"({"status":"entered"})"};
    }

  private:
    application_gate_t &_gate;
};

class application_gate_release_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<application_gate_t>;

    explicit application_gate_release_handler_t (application_gate_t &gate) :
        _gate (gate)
    {
    }

    zlink::framework::http_response_t
    handle (const zlink::framework::http_request_t &)
    {
        _gate.release ();
        return {.body = R"({"status":"released"})"};
    }

  private:
    application_gate_t &_gate;
};

class mesh_profile_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<
      zlink::framework::route_client_t,
      server::evidence_store_t>;

    explicit mesh_profile_request_handler_t (
      zlink::framework::route_client_t &routes,
      server::evidence_store_t &evidence) :
        _routes (routes), _evidence (evidence)
    {
    }

    zlink::framework::http_response_t
    handle (const zlink::framework::http_request_t &request)
    {
        const auto target = request.query_values.at ("targetRid");
        const auto payload =
          nlohmann::json::parse (request.body).get<profile_req_t> ();
        const auto reply =
          _routes
            .request_to_node (
              route_mesh_channel, zlink::routing_id_t::from (target),
              payload)
            .timeout (std::chrono::seconds (5))
            .async<profile_res_t> ()
            .result ()
            .value ();
        _evidence.add (
          "mesh-request-completed|target=" + target
          + "|marker=" + payload.marker);
        return {.body = nlohmann::json (reply).dump ()};
    }

  private:
    zlink::framework::route_client_t &_routes;
    server::evidence_store_t &_evidence;
};

class mesh_application_gate_request_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_client_t>;

    explicit mesh_application_gate_request_handler_t (
      zlink::framework::route_client_t &routes) :
        _routes (routes)
    {
    }

    zlink::framework::http_response_t
    handle (const zlink::framework::http_request_t &request)
    {
        const auto target = request.query_values.at ("targetRid");
        const auto payload =
          nlohmann::json::parse (request.body).get<application_gate_req_t> ();
        const auto reply =
          _routes
            .request_to_node (
              route_mesh_channel, zlink::routing_id_t::from (target),
              payload)
            .timeout (std::chrono::seconds (15))
            .async<application_gate_res_t> ()
            .result ()
            .value ();
        return {.body = nlohmann::json (reply).dump ()};
    }

  private:
    zlink::framework::route_client_t &_routes;
};

class server_weight_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<
      zlink::framework::channel_runtime_options_t, server::evidence_store_t>;

    server_weight_handler_t (zlink::framework::channel_runtime_options_t &options,
                             server::evidence_store_t &evidence) :
        _options (options), _evidence (evidence)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &request)
    {
        const auto found = request.query_values.find ("weight");
        if (found == request.query_values.end ()) {
            zlink::framework::http_response_t response;
            response.status = 400;
            response.body = R"({"error":"weight is required"})";
            return response;
        }
        const auto weight = static_cast<std::uint32_t> (std::stoul (found->second));
        _options.client_server_channel (profile_channel)
          .configure_server_socket ()
          .peer_weight (zlink::peer_weight_t::value (weight));
        _evidence.add ("admin|rid=" + _evidence.rid () + "|action=server-weight|weight="
                       + std::to_string (weight));
        zlink::framework::http_response_t response;
        response.body = nlohmann::json{{"weight", weight}}.dump ();
        return response;
    }

  private:
    zlink::framework::channel_runtime_options_t &_options;
    server::evidence_store_t &_evidence;
};

class create_spot_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<
      zlink::framework::spot_node_manager_t, server::evidence_store_t>;

    create_spot_handler_t (zlink::framework::spot_node_manager_t &spots,
                           server::evidence_store_t &evidence) :
        _spots (spots), _evidence (evidence)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        const auto created = _spots.create_spot (spot_channel);
        const auto spot_rid = std::string (created.spot_rid.value ());
        _evidence.add ("spot-create|rid=" + _evidence.rid () + "|spot_rid="
                       + spot_rid);
        zlink::framework::http_response_t response;
        response.body = nlohmann::json{{"spotRid", spot_rid}, {"state", "created"}}.dump ();
        return response;
    }

  private:
    zlink::framework::spot_node_manager_t &_spots;
    server::evidence_store_t &_evidence;
};

class create_subject_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<
      zlink::framework::spot_node_manager_t>;

    explicit create_subject_handler_t (
      zlink::framework::spot_node_manager_t &spots) :
        _spots (spots)
    {
    }

    zlink::framework::http_response_t
    handle (const zlink::framework::http_request_t &request)
    {
        const auto spot_rid = request.query_values.at ("spotRid");
        (void) _spots.get_or_create_spot (
          monitoring_subject_spot,
          zlink::framework::spot_rid_t::from_string (spot_rid));
        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json{{"status", "created"}, {"spotRid", spot_rid}}.dump ();
        return response;
    }

  private:
    zlink::framework::spot_node_manager_t &_spots;
};

class create_slow_subject_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<
      zlink::framework::spot_node_manager_t>;

    explicit create_slow_subject_handler_t (
      zlink::framework::spot_node_manager_t &spots) :
        _spots (spots)
    {
    }

    zlink::framework::http_response_t
    handle (const zlink::framework::http_request_t &request)
    {
        const auto spot_rid = request.query_values.at ("spotRid");
        (void) _spots.get_or_create_spot (
          monitoring_slow_spot,
          zlink::framework::spot_rid_t::from_string (spot_rid));
        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json{{"status", "created"}, {"spotRid", spot_rid}}.dump ();
        return response;
    }

  private:
    zlink::framework::spot_node_manager_t &_spots;
};

class close_subject_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<
      zlink::framework::spot_node_manager_t>;

    explicit close_subject_handler_t (
      zlink::framework::spot_node_manager_t &spots) :
        _spots (spots)
    {
    }

    zlink::framework::http_response_t
    handle (const zlink::framework::http_request_t &request)
    {
        const auto spot_rid = request.query_values.at ("spotRid");
        const auto closed =
          _spots.close_spot (
            zlink::framework::spot_rid_t::from_string (spot_rid))
            .result ();
        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json{{"status", closed ? "closed" : "not-found"},
                         {"spotRid", spot_rid}}
            .dump ();
        return response;
    }

  private:
    zlink::framework::spot_node_manager_t &_spots;
};

inline std::string submit_status_name (zlink::framework::submit_status_t status)
{
    switch (status) {
        case zlink::framework::submit_status_t::submitted:
            return "Submitted";
        case zlink::framework::submit_status_t::backpressured:
            return "Backpressured";
        case zlink::framework::submit_status_t::timed_out:
            return "TimedOut";
        case zlink::framework::submit_status_t::target_not_found:
            return "TargetNotFound";
        case zlink::framework::submit_status_t::route_not_connected:
            return "RouteNotConnected";
        case zlink::framework::submit_status_t::shutdown:
            return "Shutdown";
    }
    return "Shutdown";
}

inline multicast_publish_res_t multicast_response (
  const zlink::framework::publish_result_t &result,
  int sequence,
  const zlink::framework::mesh_node_snapshot_t &snapshot)
{
    return multicast_publish_res_t{
      .status = submit_status_name (result.status),
      .sequence = sequence,
      .snapshot_remote = result.detail.snapshot_remote_node_count,
      .admitted_remote = result.detail.admitted_remote_node_count,
      .dropped_remote = result.detail.dropped_remote_node_count,
      .snapshot_local = result.detail.snapshot_local_spot_count,
      .admitted_local = result.detail.admitted_local_spot_count,
      .dropped_local = result.detail.dropped_local_spot_count,
      .submitted_total = snapshot.multicast.submitted,
      .backpressured_total = snapshot.multicast.backpressured,
      .dropped_total = snapshot.multicast.dropped};
}

class publish_until_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<
      zlink::framework::spot_publisher_client_t,
      zlink::framework::route_mesh_runtime_t>;

    publish_until_handler_t (
      zlink::framework::spot_publisher_client_t &publisher,
      zlink::framework::route_mesh_runtime_t &runtime) :
        _publisher (publisher), _runtime (runtime)
    {
    }

    zlink::framework::http_response_t
    handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<multicast_publish_req_t> ();
        const auto payload_size =
          std::clamp (request.payload_bytes, 1, 1024 * 1024);
        const auto attempts = std::clamp (request.max_attempts, 1, 50000);
        const std::string payload (static_cast<std::size_t> (payload_size), 'x');
        zlink::framework::publish_result_t last{
          .status = zlink::framework::submit_status_t::shutdown};
        for (int sequence = 1; sequence <= attempts; ++sequence) {
            auto call = _publisher.publish (
              route_mesh_channel, "monitor.multicast",
              multicast_probe_t{request.marker, sequence, payload});
            const auto result = call.submit ().result ().value ();
            last = result;
            const bool remote_expected =
              !request.expected_remote_dropped
              || (result.detail.dropped_remote_node_count
                    == *request.expected_remote_dropped
                  && result.detail.admitted_remote_node_count
                       + result.detail.dropped_remote_node_count
                     == result.detail.snapshot_remote_node_count);
            const bool local_expected =
              !request.expected_local_dropped
              || (result.detail.dropped_local_spot_count
                    == *request.expected_local_dropped
                  && result.detail.admitted_local_spot_count
                       + result.detail.dropped_local_spot_count
                     == result.detail.snapshot_local_spot_count);
            if (remote_expected && local_expected) {
                zlink::framework::http_response_t response;
                response.body = nlohmann::json (
                  multicast_response (
                    result, sequence, _runtime.snapshot (route_mesh_name)))
                                  .dump ();
                return response;
            }
            if (!request.blocking)
                std::this_thread::yield ();
        }
        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json (
            multicast_response (
              last, attempts, _runtime.snapshot (route_mesh_name)))
            .dump ();
        return response;
    }

  private:
    zlink::framework::spot_publisher_client_t &_publisher;
    zlink::framework::route_mesh_runtime_t &_runtime;
};

class local_drop_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<
      zlink::framework::spot_publisher_client_t,
      zlink::framework::route_mesh_runtime_t>;

    local_drop_handler_t (
      zlink::framework::spot_publisher_client_t &publisher,
      zlink::framework::route_mesh_runtime_t &runtime) :
        _publisher (publisher), _runtime (runtime)
    {
    }

    zlink::framework::http_response_t
    handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<multicast_publish_req_t> ();
        const auto payload_size =
          std::clamp (request.payload_bytes, 1, 1024 * 1024);
        const auto attempts = std::clamp (request.max_attempts, 1, 50000);
        const std::string payload (static_cast<std::size_t> (payload_size), 'x');
        zlink::framework::publish_result_t last{
          .status = zlink::framework::submit_status_t::shutdown};
        for (int sequence = 1; sequence <= attempts; ++sequence) {
            (void) _publisher
              .publish (
                route_mesh_channel, "monitor.prefill",
                multicast_probe_t{request.marker, sequence, payload})
              .submit ()
              .result ();
            const auto result =
              _publisher
                .publish (
                  route_mesh_channel, "monitor.multicast",
                  multicast_probe_t{request.marker, sequence, payload})
                .submit ()
                .result ()
                .value ();
            last = result;
            if (result.detail.snapshot_local_spot_count == 2
                && result.detail.admitted_local_spot_count == 1
                && result.detail.dropped_local_spot_count == 1) {
                zlink::framework::http_response_t response;
                response.body = nlohmann::json (
                  multicast_response (
                    result, sequence, _runtime.snapshot (route_mesh_name)))
                                  .dump ();
                return response;
            }
        }
        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json (
            multicast_response (
              last, attempts, _runtime.snapshot (route_mesh_name)))
            .dump ();
        return response;
    }

  private:
    zlink::framework::spot_publisher_client_t &_publisher;
    zlink::framework::route_mesh_runtime_t &_runtime;
};

class shutdown_handler_t
{
  public:
    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        std::thread ([] {
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
            std::raise (SIGTERM);
        }).detach ();
        zlink::framework::http_response_t response;
        response.body = R"({"status":"stopping"})";
        return response;
    }
};

class runtime_observation_store_t
{
  public:
    void start (zlink::framework::route_mesh_runtime_t &runtime,
                server::evidence_store_t &evidence)
    {
        std::lock_guard lock (_mutex);
        if (_observation)
            return;
        _observation = runtime.observe (
          route_mesh_name, 32,
          [&evidence] (const zlink::framework::mesh_runtime_event_t &event) {
              auto line = "mesh-runtime-event|mesh=" + event.mesh_name
                          + "|identifier=" + event.identifier
                          + "|sequence=" + std::to_string (event.sequence);
              if (event.peer_rid)
                  line += "|peer-rid=" + event.peer_rid->to_string ();
              if (event.lifecycle_generation)
                  line += "|generation="
                          + std::to_string (*event.lifecycle_generation);
              if (event.channel_name)
                  line += "|channel=" + *event.channel_name;
              if (event.remote_snapshot_count)
                  line += "|remote-snapshot="
                          + std::to_string (*event.remote_snapshot_count);
              if (event.remote_admitted_count)
                  line += "|remote-admitted="
                          + std::to_string (*event.remote_admitted_count);
              if (event.remote_dropped_count)
                  line += "|remote-dropped="
                          + std::to_string (*event.remote_dropped_count);
              if (event.local_snapshot_count)
                  line += "|local-snapshot="
                          + std::to_string (*event.local_snapshot_count);
              if (event.local_admitted_count)
                  line += "|local-admitted="
                          + std::to_string (*event.local_admitted_count);
              if (event.local_dropped_count)
                  line += "|local-dropped="
                          + std::to_string (*event.local_dropped_count);
              if (event.claim_domain)
                  line += "|claim-domain=" + *event.claim_domain;
              if (event.reason)
                  line += "|reason=" + *event.reason;
              evidence.add (std::move (line));
          });
    }

    void start_isolation (zlink::framework::route_mesh_runtime_t &runtime,
                          server::evidence_store_t &evidence)
    {
        std::lock_guard lock (_mutex);
        if (_slow_observation || _throwing_observation)
            return;
        _slow_observation = runtime.observe (
          route_mesh_name, 1,
          [&evidence] (const zlink::framework::mesh_runtime_event_t &event) {
              evidence.add (
                "mesh-runtime-slow|identifier=" + event.identifier
                + "|sequence=" + std::to_string (event.sequence));
              std::this_thread::sleep_for (std::chrono::milliseconds (200));
          });
        _throwing_observation = runtime.observe (
          route_mesh_name, 1,
          [&evidence] (const zlink::framework::mesh_runtime_event_t &event) {
              evidence.add (
                "mesh-runtime-throwing|identifier=" + event.identifier
                + "|sequence=" + std::to_string (event.sequence));
              throw std::runtime_error ("MON-C1 observer failure");
          });
    }

  private:
    std::mutex _mutex;
    std::unique_ptr<zlink::framework::mesh_runtime_observation_t> _observation;
    std::unique_ptr<zlink::framework::mesh_runtime_observation_t> _slow_observation;
    std::unique_ptr<zlink::framework::mesh_runtime_observation_t>
      _throwing_observation;
};

class runtime_observe_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<
      zlink::framework::route_mesh_runtime_t,
      runtime_observation_store_t,
      server::evidence_store_t>;

    runtime_observe_handler_t (
      zlink::framework::route_mesh_runtime_t &runtime,
      runtime_observation_store_t &observations,
      server::evidence_store_t &evidence) :
        _runtime (runtime), _observations (observations), _evidence (evidence)
    {
    }

    zlink::framework::http_response_t
    handle (const zlink::framework::http_request_t &)
    {
        _observations.start (_runtime, _evidence);
        zlink::framework::http_response_t response;
        response.body = R"({"status":"observing"})";
        return response;
    }

  private:
    zlink::framework::route_mesh_runtime_t &_runtime;
    runtime_observation_store_t &_observations;
    server::evidence_store_t &_evidence;
};

class runtime_observe_isolation_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<
      zlink::framework::route_mesh_runtime_t,
      runtime_observation_store_t,
      server::evidence_store_t>;

    runtime_observe_isolation_handler_t (
      zlink::framework::route_mesh_runtime_t &runtime,
      runtime_observation_store_t &observations,
      server::evidence_store_t &evidence) :
        _runtime (runtime), _observations (observations), _evidence (evidence)
    {
    }

    zlink::framework::http_response_t
    handle (const zlink::framework::http_request_t &)
    {
        _observations.start_isolation (_runtime, _evidence);
        return {.body = R"({"status":"observing"})"};
    }

  private:
    zlink::framework::route_mesh_runtime_t &_runtime;
    runtime_observation_store_t &_observations;
    server::evidence_store_t &_evidence;
};

class runtime_snapshot_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_mesh_runtime_t>;

    explicit runtime_snapshot_handler_t (
      zlink::framework::route_mesh_runtime_t &runtime) :
        _runtime (runtime)
    {
    }

    zlink::framework::http_response_t
    handle (const zlink::framework::http_request_t &)
    {
        const auto snapshot = _runtime.snapshot (route_mesh_name);
        nlohmann::json peers = nlohmann::json::array ();
        for (const auto &peer : snapshot.peers) {
            peers.push_back ({{"rid", peer.rid.to_string ()},
                              {"generation", peer.lifecycle_generation},
                              {"revision", peer.descriptor_revision},
                              {"endpoint", peer.endpoint},
                              {"admissionState", peer.admission_state},
                              {"ready", peer.ready},
                              {"channels", peer.channel_names}});
        }
        nlohmann::json channels = nlohmann::json::array ();
        for (const auto &channel : snapshot.channels) {
            channels.push_back ({{"name", channel.channel_name},
                                 {"localWeight", channel.local_weight},
                                 {"readyMemberCount", channel.ready_member_count},
                                 {"selectable", channel.selectable}});
        }
        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json{{"meshName", snapshot.mesh_name},
                         {"rid", snapshot.rid.to_string ()},
                         {"generation", snapshot.lifecycle_generation},
                         {"revision", snapshot.descriptor_revision},
                         {"endpoint", snapshot.endpoint},
                         {"sequence", snapshot.sequence},
                         {"descriptorSources", snapshot.descriptor_sources},
                         {"peers", std::move (peers)},
                         {"channels", std::move (channels)},
                         {"multicast",
                          {{"submitted", snapshot.multicast.submitted},
                           {"backpressured", snapshot.multicast.backpressured},
                           {"dropped", snapshot.multicast.dropped},
                           {"remoteSnapshotCount",
                            snapshot.multicast.remote_snapshot_count},
                           {"remoteAdmittedCount",
                            snapshot.multicast.remote_admitted_count},
                           {"remoteDroppedCount",
                            snapshot.multicast.remote_dropped_count},
                           {"localSnapshotCount",
                            snapshot.multicast.local_snapshot_count},
                           {"localAdmittedCount",
                            snapshot.multicast.local_admitted_count},
                           {"localDroppedCount",
                            snapshot.multicast.local_dropped_count}}},
                         {"claims",
                          {{"applicationActive", snapshot.claims.application_active},
                           {"applicationPending",
                            snapshot.claims.pending_application_work},
                           {"infrastructureActive",
                            snapshot.claims.infrastructure_active},
                           {"infrastructurePending",
                            snapshot.claims.pending_infrastructure_work}}},
                         {"location",
                          {{"state", snapshot.location.state},
                           {"lastSuccessPresent",
                            snapshot.location.last_success_at.has_value ()},
                           {"lastFailurePresent",
                            snapshot.location.last_failure_at.has_value ()}}},
                         {"drain", {{"workSealed", snapshot.drain.work_sealed}}}}
            .dump ();
        return response;
    }

  private:
    zlink::framework::route_mesh_runtime_t &_runtime;
};

class mesh_weight_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<
      zlink::framework::route_mesh_runtime_options_t>;

    explicit mesh_weight_handler_t (
      zlink::framework::route_mesh_runtime_options_t &options) :
        _options (options)
    {
    }

    zlink::framework::http_response_t
    handle (const zlink::framework::http_request_t &request)
    {
        const auto found = request.query_values.find ("weight");
        if (found == request.query_values.end ()) {
            zlink::framework::http_response_t response;
            response.status = 400;
            response.body = R"({"error":"weight is required"})";
            return response;
        }
        const auto value = std::stoi (found->second);
        auto &channel = _options.channel (route_mesh_name, route_mesh_channel);
        channel.weight (value);
        zlink::framework::http_response_t response;
        response.body = nlohmann::json{{"weight", channel.weight ()}}.dump ();
        return response;
    }

  private:
    zlink::framework::route_mesh_runtime_options_t &_options;
};

class runtime_validation_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::route_mesh_runtime_t>;

    explicit runtime_validation_handler_t (
      zlink::framework::route_mesh_runtime_t &runtime) :
        _runtime (runtime)
    {
    }

    zlink::framework::http_response_t
    handle (const zlink::framework::http_request_t &)
    {
        bool missing_mesh_rejected = false;
        bool missing_observer_rejected = false;
        bool zero_capacity_rejected = false;
        try {
            (void) _runtime.snapshot ("missing");
        }
        catch (const zlink::framework::framework_exception_t &) {
            missing_mesh_rejected = true;
        }
        try {
            (void) _runtime.observe (
              "missing", 1,
              [] (const zlink::framework::mesh_runtime_event_t &) {});
        }
        catch (const zlink::framework::framework_exception_t &) {
            missing_observer_rejected = true;
        }
        try {
            (void) _runtime.observe (
              route_mesh_name, 0,
              [] (const zlink::framework::mesh_runtime_event_t &) {});
        }
        catch (const zlink::framework::framework_exception_t &) {
            zero_capacity_rejected = true;
        }
        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json{{"missingMeshRejected", missing_mesh_rejected},
                         {"missingObserverRejected",
                          missing_observer_rejected},
                         {"zeroCapacityRejected", zero_capacity_rejected}}
            .dump ();
        return response;
    }

  private:
    zlink::framework::route_mesh_runtime_t &_runtime;
};

} // namespace zlink::framework::e2e::runtime_monitoring::service
