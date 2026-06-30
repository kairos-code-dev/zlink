/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../../Shared/evidence_store.hpp"
#include "../../../Shared/runtime_monitoring_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <cstdint>
#include <csignal>
#include <stdexcept>
#include <string>
#include <thread>

namespace zlink::framework::e2e::runtime_monitoring::service
{

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

inline std::string socket_kind_name (zlink::framework::socket_event_kind_t kind)
{
    switch (kind) {
        case zlink::framework::socket_event_kind_t::connected:
            return "Connected";
        case zlink::framework::socket_event_kind_t::connection_ready:
            return "ConnectionReady";
        case zlink::framework::socket_event_kind_t::disconnected:
            return "Disconnected";
        case zlink::framework::socket_event_kind_t::closed:
            return "Closed";
        case zlink::framework::socket_event_kind_t::handshake_failed:
            return "HandshakeFailed";
        case zlink::framework::socket_event_kind_t::peer_admission_changed:
            return "PeerAdmissionChanged";
        case zlink::framework::socket_event_kind_t::internal:
            return "Internal";
    }
    return "Unknown";
}

inline std::string spot_kind_name (zlink::framework::spot_event_kind_t kind)
{
    switch (kind) {
        case zlink::framework::spot_event_kind_t::status_changed:
            return "StatusChanged";
        case zlink::framework::spot_event_kind_t::peers_changed:
            return "PeersChanged";
        case zlink::framework::spot_event_kind_t::subjects_changed:
            return "SubjectsChanged";
        case zlink::framework::spot_event_kind_t::timer_handler_failed:
            return "TimerHandlerFailed";
        case zlink::framework::spot_event_kind_t::timer_stopped_after_unhandled_exception:
            return "TimerStoppedAfterUnhandledException";
    }
    return "Unknown";
}

} // namespace zlink::framework::e2e::runtime_monitoring::service
