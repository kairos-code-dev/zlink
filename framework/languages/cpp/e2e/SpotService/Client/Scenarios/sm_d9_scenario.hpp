/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/framework/codecs/json_stream_connector.hpp>
#include <zlink/stream_connector.hpp>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

inline void run_sm_d9_scenario (const std::string &session_stream_endpoint)
{
    if (session_stream_endpoint.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_STREAM_ENDPOINT is required for SM-D9");
    }

    zlink::stream_connector::connector_options_t options;
    options.endpoint = session_stream_endpoint;
    options.connect_timeout = std::chrono::milliseconds (5000);
    options.request_timeout = std::chrono::milliseconds (5000);
    options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;

    std::mutex observations_mutex;
    std::vector<zlink::stream_connector::inbound_observation_t> observations;

    auto stream = zlink::stream_connector::connector_factory_t::create (options);
    stream.codecs ().add_json ();
    auto observer = stream.observe_inbound (
      [&] (const zlink::stream_connector::inbound_observation_t &observation) {
          std::lock_guard lock (observations_mutex);
          observations.push_back (observation);
      });
    auto connected = stream.connect ();
    if (!connected) {
        throw std::runtime_error ("SM-D9 stream connect failed");
    }

    constexpr auto actor_id = "actor-sm-d9";
    auto auth =
      stream.request (stream_ensure_auth_req_t{"play-a", actor_id, "SM-D9 Observer"})
        .packet_name ("StreamEnsureAuthReq")
        .timeout (std::chrono::milliseconds (5000))
        .submit<stream_auth_res_t> ();
    if (!auth || auth.value ().actor.actor_id != actor_id
        || auth.value ().session_node_rid != "session-a") {
        throw std::runtime_error ("SM-D9 stream auth reply mismatch");
    }

    auto first = stream.request (actor_ping_req_t{"observer-1"})
                   .packet_name ("ActorPingReq")
                   .timeout (std::chrono::milliseconds (5000))
                   .submit<actor_ping_res_t> ();
    auto second = stream.request (actor_ping_req_t{"observer-2"})
                    .packet_name ("ActorPingReq")
                    .timeout (std::chrono::milliseconds (5000))
                    .submit<actor_ping_res_t> ();
    if (!first || first.value ().actor_id != actor_id || first.value ().value != "observer-1") {
        throw std::runtime_error ("SM-D9 first ping reply mismatch");
    }
    if (!second || second.value ().actor_id != actor_id || second.value ().value != "observer-2") {
        throw std::runtime_error ("SM-D9 second ping reply mismatch");
    }

    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (2);
    for (;;) {
        {
            std::lock_guard lock (observations_mutex);
            if (observations.size () >= 3) {
                break;
            }
        }
        if (std::chrono::steady_clock::now () >= deadline) {
            break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }

    {
        std::lock_guard lock (observations_mutex);
        auto summary = [&] {
            std::ostringstream stream_summary;
            for (const auto &observation : observations) {
                if (stream_summary.tellp () > 0) {
                    stream_summary << ",";
                }
                stream_summary << observation.name << "#"
                               << static_cast<int> (observation.kind) << "#"
                               << (observation.request_seq
                                     ? std::to_string (*observation.request_seq)
                                     : std::string ("none"));
            }
            return stream_summary.str ();
        };
        auto has_response = [&] (std::uint64_t request_seq) {
            for (const auto &observation : observations) {
                if (observation.kind == zlink::stream_connector::message_kind_t::response
                    && observation.name == "reply" && observation.request_seq == request_seq) {
                    return true;
                }
            }
            return false;
        };
        if (!has_response (1)) {
            throw std::runtime_error ("SM-D9 auth response observation missing: " + summary ());
        }
        if (!has_response (2)) {
            throw std::runtime_error ("SM-D9 first ping response observation missing: " + summary ());
        }
        if (!has_response (3)) {
            throw std::runtime_error ("SM-D9 second ping response observation missing: " + summary ());
        }
    }

    observer.close ();
    (void) stream.close ();
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
