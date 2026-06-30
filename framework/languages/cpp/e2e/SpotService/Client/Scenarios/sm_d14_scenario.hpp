/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <zlink/framework/codecs/json_stream_connector.hpp>
#include <zlink/stream_connector.hpp>

#include <chrono>
#include <future>
#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::spot_service::client::scenarios
{

template <typename TReply, typename TRequest>
inline zlink::stream_connector::result_t<TReply>
sm_d14_submit_tls_request (zlink::stream_connector::connector_t &connector,
                           TRequest request,
                           std::string packet_name)
{
    auto promise =
      std::make_shared<std::promise<zlink::stream_connector::result_t<TReply>>> ();
    auto future = promise->get_future ();
    connector.request (std::move (request))
      .packet_name (std::move (packet_name))
      .timeout (std::chrono::milliseconds (5000))
      .template submit<TReply> (
        [promise] (zlink::stream_connector::result_t<TReply> result) mutable {
          promise->set_value (std::move (result));
      });
    return future.get ();
}

inline void run_sm_d14_scenario (const std::string &session_tls_stream_endpoint)
{
    if (session_tls_stream_endpoint.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_TLS_STREAM_ENDPOINT is required for SM-D14");
    }

    zlink::stream_connector::connector_options_t options;
    options.endpoint = session_tls_stream_endpoint;
    options.transport = zlink::stream_connector::transport_t::tls;
    options.connect_timeout = std::chrono::milliseconds (5000);
    options.request_timeout = std::chrono::milliseconds (5000);
    options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::immediate;

    {
        auto strict = zlink::stream_connector::connector_factory_t::create (options);
        const auto connected = strict.connect ();
        if (connected) {
            (void) strict.close ();
            throw std::runtime_error (
              "SM-D14 strict TLS validation accepted the self-signed certificate");
        }
    }

    options.skip_server_certificate_validation = true;
    auto tls = zlink::stream_connector::connector_factory_t::create (options);
    auto connected = tls.connect ();
    if (!connected) {
        throw std::runtime_error ("SM-D14 TLS stream connect failed");
    }

    constexpr auto actor_id = "actor-sm-d14-tls";
    auto auth = sm_d14_submit_tls_request<stream_auth_res_t> (
      tls, stream_ensure_auth_req_t{"play-a", actor_id, "SM-D14 TLS"},
      "StreamEnsureAuthReq");
    if (!auth) {
        throw std::runtime_error (
          std::string ("SM-D14 TLS auth failed: ")
          + (auth.error () ? auth.error ()->message : "unknown stream auth error"));
    }
    if (auth.value ().actor.actor_id != actor_id
        || auth.value ().session_node_rid != "session-a") {
        throw std::runtime_error (
          "SM-D14 TLS auth reply mismatch: actor=" + auth.value ().actor.actor_id
          + " session=" + auth.value ().session_node_rid);
    }

    auto push_wait =
      tls.wait_for<actor_push_notify_t> (std::chrono::milliseconds (10000))
        .to_future ("SM-D14 TLS push notify missing");
    auto reply =
      sm_d14_submit_tls_request<actor_push_res_t> (tls, actor_push_req_t{"tls-push"}, "PushReq");
    if (!reply || !reply.value ().pushed || reply.value ().actor_id != actor_id) {
        throw std::runtime_error ("SM-D14 TLS actor push reply mismatch");
    }

    const auto notify = push_wait.get ();
    if (notify.actor_id != actor_id || notify.value != "tls-push") {
        throw std::runtime_error ("SM-D14 TLS push notify mismatch");
    }

    (void) tls.close ();
}

} // namespace zlink::framework::e2e::spot_service::client::scenarios
