/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Infrastructure/evidence_store.hpp"
#include "../Infrastructure/fault_state.hpp"

#include "../../../Shared/resilience_lifecycle_messages.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

namespace zlink::framework::e2e::resilience_lifecycle::registry
{

inline const std::string &profile_marker_or_value (const profile_req_t &request)
{
    return request.marker.empty () ? request.value : request.marker;
}

inline const std::string &profile_marker_or_command (const profile_msg_t &command)
{
    return command.marker.empty () ? command.command_id : command.marker;
}

class profile_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<evidence_store_t, fault_state_t>;
    using request_type = profile_req_t;
    using reply_type = profile_res_t;

    profile_request_handler_t (evidence_store_t &evidence, fault_state_t &fault) :
        _evidence (evidence), _fault (fault)
    {
    }

    profile_res_t handle (const profile_req_t &request)
    {
        const auto &marker = profile_marker_or_value (request);
        if (_fault.mode () == "gray" && request.value == "gray") {
            _evidence.add ("profile-fault|rid=" + _evidence.rid () + "|marker=" + marker
                           + "|mode=gray");
            throw std::runtime_error ("gray failure");
        }
        if (request.value == "slow") {
            _evidence.add ("profile-start|rid=" + _evidence.rid () + "|marker=" + marker
                           + "|value=" + request.value);
            std::this_thread::sleep_for (std::chrono::milliseconds (700));
        }
        _evidence.add ("profile-request|rid=" + _evidence.rid () + "|marker=" + marker
                       + "|value=" + request.value);
        return {.value = "profile:" + request.value,
                .provider_rid = _evidence.rid (),
                .instance_id = request.value,
                .marker = marker};
    }

  private:
    evidence_store_t &_evidence;
    fault_state_t &_fault;
};

class profile_command_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<evidence_store_t>;
    using message_type = profile_msg_t;

    explicit profile_command_handler_t (evidence_store_t &evidence) : _evidence (evidence) {}

    void handle (const profile_msg_t &command)
    {
        _evidence.add ("profile-command|rid=" + _evidence.rid ()
                       + "|marker=" + profile_marker_or_command (command));
    }

  private:
    evidence_store_t &_evidence;
};

inline std::string dispatch_reason_name (zlink::framework::dispatch_error_reason_t reason)
{
    switch (reason) {
        case zlink::framework::dispatch_error_reason_t::handler_missing:
            return "handler_missing";
        case zlink::framework::dispatch_error_reason_t::payload_decode_failed:
            return "payload_decode_failed";
        case zlink::framework::dispatch_error_reason_t::handler_exception:
            return "handler_exception";
        case zlink::framework::dispatch_error_reason_t::invalid_frame:
            return "invalid_frame";
        case zlink::framework::dispatch_error_reason_t::reply_path_missing:
            return "reply_path_missing";
        case zlink::framework::dispatch_error_reason_t::unexpected_reply:
            return "unexpected_reply";
    }
    return "unknown";
}

inline std::string dispatch_surface_name (zlink::framework::dispatch_error_surface_t surface)
{
    switch (surface) {
        case zlink::framework::dispatch_error_surface_t::channel:
            return "channel";
        case zlink::framework::dispatch_error_surface_t::route_mesh_channel:
            return "route_mesh_channel";
        case zlink::framework::dispatch_error_surface_t::spot_route:
            return "spot_route";
        case zlink::framework::dispatch_error_surface_t::spot_subscription:
            return "spot_subscription";
        case zlink::framework::dispatch_error_surface_t::spot_actor:
            return "spot_actor";
        case zlink::framework::dispatch_error_surface_t::stream_session:
            return "stream_session";
    }
    return "unknown";
}

inline std::string dispatch_message_kind_name (zlink::framework::dispatch_message_kind_t kind)
{
    switch (kind) {
        case zlink::framework::dispatch_message_kind_t::send:
            return "send";
        case zlink::framework::dispatch_message_kind_t::request:
            return "request";
        case zlink::framework::dispatch_message_kind_t::publish:
            return "publish";
        case zlink::framework::dispatch_message_kind_t::response:
            return "response";
        case zlink::framework::dispatch_message_kind_t::error:
            return "error";
        case zlink::framework::dispatch_message_kind_t::actor_send:
            return "actor_send";
        case zlink::framework::dispatch_message_kind_t::actor_request:
            return "actor_request";
    }
    return "unknown";
}

inline std::string dispatch_action_name (zlink::framework::dispatch_error_action_t action)
{
    switch (action) {
        case zlink::framework::dispatch_error_action_t::drop:
            return "drop";
        case zlink::framework::dispatch_error_action_t::reply_error:
            return "reply_error";
    }
    return "unknown";
}

inline void configure_evidence_dispatch_error_observer (
  zlink::framework::zlink_framework_options_t &framework,
  evidence_store_t *evidence,
  fault_state_t *fault_state)
{
    framework.configure_dispatch ().set_message_flow_observer (
      [evidence, fault_state] (const zlink::framework::message_flow_event_t &event) {
          if (event.outcome != zlink::framework::message_flow_outcome_t::error
              || !event.error_reason || !event.error_action) {
              return;
          }
          evidence->add ("dispatch-error|surface=" + dispatch_surface_name (event.surface)
                         + "|kind=" + dispatch_message_kind_name (event.message_kind)
                         + "|reason=" + dispatch_reason_name (*event.error_reason)
                         + "|action=" + dispatch_action_name (*event.error_action)
                         + "|packet=" + event.packet_name.value_or ("<null>")
                         + "|channel=" + event.channel_name.value_or ("<null>"));
          if (fault_state->mode () == "observer-throws") {
              throw std::runtime_error ("dispatch observer failure");
          }
      });
}

} // namespace zlink::framework::e2e::resilience_lifecycle::registry
