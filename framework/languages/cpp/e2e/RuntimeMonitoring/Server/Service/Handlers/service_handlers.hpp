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

class monitoring_spot_t
    : public zlink::framework::spot_t<zlink::framework::actor_t>
{
  public:
    explicit monitoring_spot_t (zlink::framework::spot_context_t context) :
        _context (std::move (context))
    {
    }

    ~monitoring_spot_t () override = default;

    zlink::framework::spot_context_t &context () noexcept override
    {
        return _context;
    }

    const zlink::framework::spot_context_t &context () const noexcept override
    {
        return _context;
    }

    void configure () override
    {
        using namespace std::chrono_literals;
        _context.add_timer<failing_timer_handler_t> ("failing", 50ms);
        _context.add_timer<failing_timer_handler_t> (
          "stopping", 50ms, {.stop_on_unhandled_exception = true});
    }

    zlink::framework::task_t<zlink::framework::spot_actor_join_response_t>
    on_actor_join (std::string_view,
                   const zlink::framework::message_t &) override
    {
        co_return zlink::framework::spot_actor_join_response_t::accept ();
    }

    zlink::framework::task_t<void>
    on_actor_joined (zlink::framework::actor_t &) override
    {
        co_return;
    }

    zlink::framework::task_t<void>
    on_leave_actor (zlink::framework::actor_t &) override
    {
        co_return;
    }

  private:
    zlink::framework::spot_context_t _context;
};

class monitoring_subject_spot_t
    : public zlink::framework::spot_t<zlink::framework::actor_t>
{
  public:
    explicit monitoring_subject_spot_t (
      zlink::framework::spot_context_t context) :
        _context (std::move (context))
    {
    }

    ~monitoring_subject_spot_t () override = default;

    zlink::framework::spot_context_t &context () noexcept override
    {
        return _context;
    }

    const zlink::framework::spot_context_t &context () const noexcept override
    {
        return _context;
    }

    void configure () override {}

    zlink::framework::task_t<zlink::framework::spot_actor_join_response_t>
    on_actor_join (std::string_view,
                   const zlink::framework::message_t &) override
    {
        co_return zlink::framework::spot_actor_join_response_t::accept ();
    }

    zlink::framework::task_t<void>
    on_actor_joined (zlink::framework::actor_t &) override
    {
        co_return;
    }

    zlink::framework::task_t<void>
    on_leave_actor (zlink::framework::actor_t &) override
    {
        co_return;
    }

  private:
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
            .submit<profile_res_t> ()
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
            .submit<application_gate_res_t> ()
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
        const auto spot_id = created.spot_id;
        _evidence.add ("spot-create|rid=" + _evidence.rid () + "|spot_id="
                       + spot_id);
        zlink::framework::http_response_t response;
        response.body = nlohmann::json{{"spotId", spot_id}, {"state", "created"}}.dump ();
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
        const auto spot_id = request.query_values.at ("spotId");
        (void) _spots.get_or_create_spot (
          monitoring_subject_spot,
          (spot_id));
        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json{{"status", "created"}, {"spotId", spot_id}}.dump ();
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
        const auto spot_id = request.query_values.at ("spotId");
        const auto closed =
          _spots.close_spot (
            (spot_id))
            .result ();
        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json{{"status", closed ? "closed" : "not-found"},
                         {"spotId", spot_id}}
            .dump ();
        return response;
    }

  private:
    zlink::framework::spot_node_manager_t &_spots;
};

class publish_probe_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<
      zlink::framework::spot_publisher_client_t>;

    explicit publish_probe_handler_t (
      zlink::framework::spot_publisher_client_t &publisher) :
        _publisher (publisher)
    {
    }

    zlink::framework::http_response_t
    handle (const zlink::framework::http_request_t &request)
    {
        const auto topic = request.query_values.at ("topic");
        _publisher
          .publish (
            route_mesh_channel,
            topic,
            profile_req_t{.value = "publish", .marker = topic})
          .submit ()
          .result ()
          .value ();
        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json{{"status", "published"}, {"topic", topic}}.dump ();
        return response;
    }

  private:
    zlink::framework::spot_publisher_client_t &_publisher;
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
          [&evidence] (const zlink::framework::mesh_node_snapshot_t &event) {
              auto line = "mesh-runtime-snapshot|mesh=" + event.mesh_name
                          + "|sequence=" + std::to_string (event.sequence)
                          + "|ready=" + (event.is_ready ? "true" : "false")
                          + "|ready-peers="
                          + std::to_string (event.ready_peer_count);
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
          [&evidence] (const zlink::framework::mesh_node_snapshot_t &event) {
              evidence.add (
                "mesh-runtime-slow|mesh=" + event.mesh_name
                + "|sequence=" + std::to_string (event.sequence));
              std::this_thread::sleep_for (std::chrono::milliseconds (200));
          });
        _throwing_observation = runtime.observe (
          route_mesh_name, 1,
          [&evidence] (const zlink::framework::mesh_node_snapshot_t &event) {
              evidence.add (
                "mesh-runtime-throwing|mesh=" + event.mesh_name
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
        auto &channel = _options.channel (route_mesh_channel);
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
              [] (const zlink::framework::mesh_node_snapshot_t &) {});
        }
        catch (const zlink::framework::framework_exception_t &) {
            missing_observer_rejected = true;
        }
        try {
            (void) _runtime.observe (
              route_mesh_name, 0,
              [] (const zlink::framework::mesh_node_snapshot_t &) {});
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
