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

inline void run_sm_d7_scenario (const std::string &session_stream_endpoint)
{
    if (session_stream_endpoint.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_STREAM_ENDPOINT is required for SM-D7");
    }

    zlink::stream_connector::connector_options_t options;
    options.endpoint = session_stream_endpoint;
    options.connect_timeout = std::chrono::milliseconds (5000);
    options.request_timeout = std::chrono::milliseconds (5000);
    options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;

    {
        auto unauthenticated = zlink::stream_connector::connector_factory_t::create (options);
        unauthenticated.codecs ().add_json ();
        auto connected = unauthenticated.connect ();
        if (!connected) {
            throw std::runtime_error ("SM-D7 unauthenticated stream connect failed");
        }
        auto rejected =
          zlink::stream_connector::codecs::request (
            unauthenticated, actor_ping_req_t{"before-auth"})
            .packet_name ("ActorPingReq")
            .timeout (std::chrono::milliseconds (3000))
            .submit<actor_ping_res_t> ();
        if (rejected) {
            throw std::runtime_error ("SM-D7 unauthenticated dispatch unexpectedly succeeded");
        }
        (void) unauthenticated.close ();
    }

    {
        auto invalid = zlink::stream_connector::connector_factory_t::create (options);
        invalid.codecs ().add_json ();
        auto connected = invalid.connect ();
        if (!connected) {
            throw std::runtime_error ("SM-D7 invalid-auth stream connect failed");
        }
        auto rejected =
          zlink::stream_connector::codecs::request (
            invalid, stream_auth_req_t{"play-a", "actor-sm-d7-invalid", "SM-D7 Invalid", {}})
            .packet_name ("StreamAuthReq")
            .timeout (std::chrono::milliseconds (3000))
            .submit<stream_auth_res_t> ();
        if (rejected) {
            throw std::runtime_error ("SM-D7 invalid auth unexpectedly succeeded");
        }
        (void) invalid.close ();
    }

    constexpr auto actor_id = "actor-sm-d7";
    auto stream = zlink::stream_connector::connector_factory_t::create (options);
    stream.codecs ().add_json ();
    auto connected = stream.connect ();
    if (!connected) {
        throw std::runtime_error ("SM-D7 stream connect failed");
    }

    auto auth =
      zlink::stream_connector::codecs::request (
        stream, stream_ensure_auth_req_t{"play-a", actor_id, "SM-D7 Auth"})
        .packet_name ("StreamEnsureAuthReq")
        .timeout (std::chrono::milliseconds (5000))
        .submit<stream_auth_res_t> ();
    if (!auth || auth.value ().actor.actor_id != actor_id
        || auth.value ().session_node_rid != "session-a") {
        throw std::runtime_error ("SM-D7 stream auth reply mismatch");
    }

    auto reply = zlink::stream_connector::codecs::request (stream, actor_ping_req_t{"auth-ok"})
                   .packet_name ("ActorPingReq")
                   .timeout (std::chrono::milliseconds (5000))
                   .submit<actor_ping_res_t> ();
    if (!reply || reply.value ().actor_id != actor_id || reply.value ().node_rid != "play-a"
        || reply.value ().value != "auth-ok") {
        throw std::runtime_error ("SM-D7 authenticated dispatch mismatch");
    }

    (void) stream.close ();
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
