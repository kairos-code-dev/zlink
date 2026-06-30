/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/framework/codecs/json_stream_connector.hpp>
#include <zlink/stream_connector.hpp>

#include <chrono>
#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

inline void run_sm_d12_scenario (const std::string &session_a_stream_endpoint,
                                 const std::string &session_b_stream_endpoint)
{
    if (session_a_stream_endpoint.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_STREAM_ENDPOINT is required for SM-D12");
    }
    if (session_b_stream_endpoint.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_ALT_STREAM_ENDPOINT is required for SM-D12");
    }

    constexpr auto actor_id = "actor-sm-d12-transfer";

    zlink::stream_connector::connector_options_t first_options;
    first_options.endpoint = session_a_stream_endpoint;
    first_options.connect_timeout = std::chrono::milliseconds (5000);
    first_options.request_timeout = std::chrono::milliseconds (5000);
    first_options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;

    auto first = zlink::stream_connector::connector_factory_t::create (first_options);
    auto first_connected = first.connect ();
    if (!first_connected) {
        throw std::runtime_error ("SM-D12 first stream connect failed");
    }

    auto first_auth =
      first.request (stream_ensure_auth_req_t{"play-a", actor_id, "SM-D12 Transfer"})
        .packet_name ("StreamEnsureAuthReq")
        .timeout (std::chrono::milliseconds (5000))
        .submit<stream_auth_res_t> ();
    if (!first_auth || first_auth.value ().actor.actor_id != actor_id
        || first_auth.value ().session_node_rid != "session-a") {
        throw std::runtime_error ("SM-D12 first stream auth failed");
    }

    auto joined =
      first.request (join_req_t{.key = "sm-d12-transfer",
                          .actor_id = actor_id,
                          .display_name = "SM-D12 Transfer",
                          .level = 12,
                          .tags = {"stream", "SM-D12", "session-a"}})
        .packet_name ("JoinReq")
        .timeout (std::chrono::milliseconds (5000))
        .submit<join_res_t> ();
    if (!joined || joined.value ().owner_node_rid != "play-a"
        || joined.value ().actor.actor_id != actor_id) {
        throw std::runtime_error ("SM-D12 first join failed");
    }

    auto first_state =
      first.request (state_req_t{.op = "add", .amount = 11})
        .packet_name ("StateReq")
        .timeout (std::chrono::milliseconds (5000))
        .submit<state_res_t> ();
    if (!first_state || first_state.value ().owner_node_rid != "play-a"
        || first_state.value ().value != 11) {
        throw std::runtime_error ("SM-D12 first state mismatch");
    }

    (void) first.close ();

    zlink::stream_connector::connector_options_t second_options;
    second_options.endpoint = session_b_stream_endpoint;
    second_options.connect_timeout = std::chrono::milliseconds (5000);
    second_options.request_timeout = std::chrono::milliseconds (5000);
    second_options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;

    auto second = zlink::stream_connector::connector_factory_t::create (second_options);
    auto second_connected = second.connect ();
    if (!second_connected) {
        throw std::runtime_error ("SM-D12 second stream connect failed");
    }

    auto second_auth =
      second.request (stream_auth_req_t{"play-a", actor_id, "SM-D12 Transfer", joined.value ().actor})
        .packet_name ("StreamAuthReq")
        .timeout (std::chrono::milliseconds (5000))
        .submit<stream_auth_res_t> ();
    if (!second_auth || second_auth.value ().actor.actor_id != actor_id
        || second_auth.value ().session_node_rid != "session-b") {
        throw std::runtime_error ("SM-D12 second stream auth failed");
    }

    auto snapshot =
      second.request (state_req_t{.op = "add", .amount = 0})
        .packet_name ("StateReq")
        .timeout (std::chrono::milliseconds (5000))
        .submit<state_res_t> ();
    if (!snapshot || snapshot.value ().owner_node_rid != "play-a"
        || snapshot.value ().value != 11) {
        throw std::runtime_error ("SM-D12 snapshot mismatch");
    }

    auto notify_wait =
      second.wait_for<actor_push_notify_t> (std::chrono::milliseconds (10000))
        .to_future ("SM-D12 push notify missing");
    auto pushed =
      second.request (actor_push_req_t{"after-transfer"})
        .packet_name ("PushReq")
        .timeout (std::chrono::milliseconds (5000))
        .submit<actor_push_res_t> ();
    if (!pushed || !pushed.value ().pushed || pushed.value ().actor_id != actor_id) {
        throw std::runtime_error ("SM-D12 push request failed");
    }
    auto notify = notify_wait.get ();
    if (notify.actor_id != actor_id || notify.value != "after-transfer") {
        throw std::runtime_error ("SM-D12 push notify mismatch");
    }

    auto resumed =
      second.request (state_req_t{.op = "add", .amount = 5})
        .packet_name ("StateReq")
        .timeout (std::chrono::milliseconds (5000))
        .submit<state_res_t> ();
    if (!resumed || resumed.value ().owner_node_rid != "play-a" || resumed.value ().value != 16) {
        throw std::runtime_error ("SM-D12 resumed state mismatch");
    }

    (void) second.close ();
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
