/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/yield_dispatch_contracts.hpp"
#include "../Support/client_options.hpp"
#include "../Support/scenario_assert.hpp"
#include "yield_actor_scenario_context.hpp"

#include <zlink/stream_connector.hpp>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace zlink::framework::e2e::yield_dispatch::client
{

template <typename TConnector>
std::string run_yd_d4_session_relay_actor_yield_scenario (
  TConnector &connector,
  const std::string &session_b_stream_endpoint,
  const yield_actor_scenario_context_t &actors)
{
    ensure (!session_b_stream_endpoint.empty (),
            "ZLINK_CPP_E2E_SESSION_B_STREAM_ENDPOINT is required for YD-D4");
    auto unbound_options = make_connector_options (session_b_stream_endpoint);
    auto unbound = zlink::stream_connector::connector_factory_t::create (unbound_options);
    auto unbound_connected = unbound.connect ();
    ensure (static_cast<bool> (unbound_connected), "YD-D4 unbound stream connect failed");

    const auto request_id = unique_id ("YD-D4");
    auto rebound =
      connector
        .request (bind_yield_actors_req_t{.spot_rid = actors.spot_rid,
                                          .actor_ids = {actors.actor_a, actors.actor_b}})
        .packet_name (bind_yield_actors_req_t::packet_name)
        .timeout (std::chrono::milliseconds (15000))
        .template submit<bind_yield_actors_res_t> ();
    ensure (static_cast<bool> (rebound), "YD-D4 client actor rebind failed");
    auto bound_push =
      connector.template wait_for<actor_push_notify_t> (std::chrono::milliseconds (30000))
        .to_future ("YD-D4 bound push notify missing");
    auto unbound_push =
      unbound.wait_for<actor_push_notify_t> (std::chrono::milliseconds (500));

    auto reply =
      connector
        .request (actor_push_yield_req_t{.request_id = request_id,
                                         .delay_ms = 250,
                                         .value = "bound-session-push"})
        .packet_name (actor_push_yield_req_t::packet_name)
        .metadata (actor_id_metadata, actors.actor_a)
        .timeout (std::chrono::milliseconds (30000))
        .template submit<actor_yield_res_t> ();
    ensure_result (reply, "YD-D4 ActorPushYieldReq failed");
    ensure (reply.value ().scenario_id == "YD-D4", "YD-D4 reply scenario mismatch");
    ensure (reply.value ().request_id == request_id, "YD-D4 reply request mismatch");
    ensure (reply.value ().actor_id == actors.actor_a, "YD-D4 reply actor mismatch");
    ensure (reply.value ().marker == "actor-push-yield-completed",
            "YD-D4 reply marker mismatch");

    const auto notify = bound_push.get ();
    ensure (notify.actor_id == actors.actor_a, "YD-D4 push actor mismatch");
    ensure (notify.request_id == request_id, "YD-D4 push request mismatch");
    ensure (notify.value == "bound-session-push", "YD-D4 push value mismatch");
    ensure (notify.node_rid == "play-a", "YD-D4 push node mismatch");

    std::this_thread::sleep_for (std::chrono::milliseconds (150));
    auto leaked = unbound_push.submit ();
    ensure (!static_cast<bool> (leaked), "YD-D4 unbound session received actor push");

    auto evidence =
      connector.request (yield_evidence_req_t{.request_id = request_id})
        .packet_name (yield_evidence_req_t::packet_name)
        .metadata (target_node_rid_metadata, "play-a")
        .timeout (std::chrono::milliseconds (30000))
        .template submit<yield_evidence_res_t> ();
    ensure (static_cast<bool> (evidence), "YD-D4 evidence request failed");
    ensure (contains_in_order (evidence.value ().evidence, request_id,
                               {"actor-push-yield-started",
                                "actor-push-yield-released",
                                "actor-push-yield-resumed",
                                "actor-push-yield-completed"}),
            "YD-D4 marker order mismatch");

    (void) unbound.close ();
    std::cout << "scenario YD-D4 passed\n";
    return request_id;
}

} // namespace zlink::framework::e2e::yield_dispatch::client
