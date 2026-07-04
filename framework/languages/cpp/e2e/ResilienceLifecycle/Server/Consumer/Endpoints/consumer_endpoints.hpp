/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/resilience_lifecycle_contracts.hpp"
#include "../../Shared/location_store.hpp"
#include "../Configuration/consumer_options.hpp"

#include <zlink/framework.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace zlink::framework::e2e::resilience_lifecycle::consumer
{

using resilience_lifecycle::api_channel;
using resilience_lifecycle::operation_status_t;
using resilience_lifecycle::profile_msg_t;
using resilience_lifecycle::profile_req_t;
using resilience_lifecycle::profile_res_t;
using resilience_lifecycle::request_failure_res_t;

struct topology_entry_result_t
{
    std::string name;
    std::string kind;
    std::string role;
    std::string routing_id;
    std::string endpoint;
    std::string state;
};

inline void to_json (nlohmann::json &json, const topology_entry_result_t &value)
{
    json = nlohmann::json{{"name", value.name},
                          {"kind", value.kind},
                          {"role", value.role},
                          {"routing_id", value.routing_id},
                          {"endpoint", value.endpoint},
                          {"state", value.state}};
}

inline std::vector<topology_entry_result_t> peer_topology (
  zlink::framework::location_runtime_query_t &locations,
  const std::optional<zlink::routing_id_t> &routing_id = std::nullopt)
{
    zlink::framework::peer_location_filter_t filter;
    filter.mesh_name = api_channel;
    filter.role = zlink::framework::location_role_t::router;
    if (routing_id) {
        filter.node_rid = routing_id;
    }
    auto result = locations.list_peer_locations (std::move (filter)).result ();
    if (!result) {
        throw std::runtime_error (result.error () ? result.error ()->what ()
                                                 : "location peer query failed");
    }
    std::vector<topology_entry_result_t> entries;
    for (const auto &peer : result.value ()) {
        entries.push_back (topology_entry_result_t{.name = peer.mesh_name,
                                                   .kind = "channel",
                                                   .role = "server",
                                                   .routing_id = peer.node_rid
                                                                   ? peer.node_rid->to_string ()
                                                                   : std::string{},
                                                   .endpoint = peer.endpoint,
                                                   .state = "Ready"});
    }
    return entries;
}

class topology_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::location_runtime_query_t>;

    explicit topology_handler_t (zlink::framework::location_runtime_query_t &locations) :
        _locations (locations)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        zlink::framework::http_response_t response;
        response.body = nlohmann::json (peer_topology (_locations)).dump ();
        return response;
    }

  private:
    zlink::framework::location_runtime_query_t &_locations;
};

class topology_wait_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::location_runtime_query_t>;

    explicit topology_wait_handler_t (zlink::framework::location_runtime_query_t &locations) :
        _locations (locations)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &request)
    {
        const auto body = nlohmann::json::parse (request.body.empty () ? "{}" : request.body);
        const auto routing_id =
          body.value ("routingId", body.value ("routing_id", std::string{}));
        auto state = body.value ("state", std::string{"Ready"});
        if (state == "active") {
            state = "Ready";
        }
        const auto expected_count =
          body.value ("expectedCount", body.value ("expected_count", 1));
        const auto timeout_ms =
          std::clamp (body.value ("timeoutMilliseconds",
                                  body.value ("timeout_milliseconds", 30000)),
                      1,
                      30000);
        const auto rid = routing_id.empty ()
                           ? std::optional<zlink::routing_id_t>{}
                           : zlink::routing_id_t::from (routing_id);
        const auto deadline =
          std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms);
        while (std::chrono::steady_clock::now () < deadline) {
            auto entries = peer_topology (_locations, rid);
            const auto count =
              std::count_if (entries.begin (), entries.end (), [&] (const auto &entry) {
                  return (routing_id.empty () || entry.routing_id == routing_id)
                         && entry.state == state;
              });
            const auto satisfied =
              expected_count == 0 ? count == 0 : count >= expected_count;
            if (satisfied) {
                zlink::framework::http_response_t response;
                response.body = nlohmann::json (entries).dump ();
                return response;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (250));
        }

        zlink::framework::http_response_t response;
        response.status = 504;
        response.body = nlohmann::json{{"error", "topology did not reach expected state"},
                                       {"routing_id", routing_id},
                                       {"state", state},
                                       {"expected_count", expected_count}}
                          .dump ();
        return response;
    }

  private:
    zlink::framework::location_runtime_query_t &_locations;
};

inline profile_res_t request_profile_once (zlink::framework::channel_client_t &channels,
                                           const profile_req_t &request,
                                           std::chrono::milliseconds timeout)
{
    auto call = channels.request (api_channel, request)
                  .timeout (timeout)
                  .async<profile_res_t> ();
    const auto &reply = call.result ();
    if (reply) {
        return reply.value ();
    }
    throw std::runtime_error (reply.error () ? reply.error ()->what ()
                                             : "profile request failed");
}

inline profile_res_t request_profile_with_retry (zlink::framework::channel_client_t &channels,
                                                 const profile_req_t &request)
{
    std::optional<std::string> last_error;
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (30);
    while (std::chrono::steady_clock::now () < deadline) {
        try {
            return request_profile_once (channels, request, std::chrono::milliseconds (3000));
        }
        catch (const std::exception &error) {
            last_error = error.what ();
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
    }
    throw std::runtime_error (last_error ? "profile request retry timed out: " + *last_error
                                         : "profile request retry timed out");
}

class profile_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = profile_req_t;
    using reply_type = profile_res_t;

    explicit profile_request_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    profile_res_t handle (const profile_req_t &request)
    {
        return request_profile_once (_channels, request, std::chrono::milliseconds (3000));
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

class manual_profile_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = profile_req_t;
    using reply_type = profile_res_t;

    explicit manual_profile_request_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    profile_res_t handle (const profile_req_t &request)
    {
        auto call = _channels.request ("resilience.lifecycle.api.manual", request)
                      .timeout (std::chrono::milliseconds (3000))
                      .async<profile_res_t> ();
        const auto &reply = call.result ();
        if (reply) {
            return reply.value ();
        }
        throw std::runtime_error (reply.error () ? reply.error ()->what ()
                                                 : "manual profile request failed");
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

class manual_b_profile_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = profile_req_t;
    using reply_type = profile_res_t;

    explicit manual_b_profile_request_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    profile_res_t handle (const profile_req_t &request)
    {
        auto call = _channels.request ("resilience.lifecycle.api.manual.b", request)
                      .timeout (std::chrono::milliseconds (3000))
                      .async<profile_res_t> ();
        const auto &reply = call.result ();
        if (reply) {
            return reply.value ();
        }
        throw std::runtime_error (reply.error () ? reply.error ()->what ()
                                                 : "manual provider B profile request failed");
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

class slow_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = profile_req_t;
    using reply_type = request_failure_res_t;

    explicit slow_request_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    request_failure_res_t handle (const profile_req_t &request)
    {
        auto call = _channels.request (api_channel, request)
                      .timeout (std::chrono::milliseconds (100))
                      .async<profile_res_t> ();
        const auto &reply = call.result ();
        if (reply) {
            return {.failed = false, .error_type = ""};
        }
        return {.failed = true,
                .error_type = reply.error () ? reply.error ()->what () : "request failed"};
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

class missing_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = profile_req_t;
    using reply_type = request_failure_res_t;

    explicit missing_request_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    request_failure_res_t handle (const profile_req_t &request)
    {
        auto call = _channels.request (api_channel, request)
                      .packet_name ("MissingProfileReq")
                      .timeout (std::chrono::milliseconds (3000))
                      .async<profile_res_t> ();
        const auto &reply = call.result ();
        if (reply) {
            return {.failed = false, .error_type = ""};
        }
        return {.failed = true,
                .error_type = reply.error () ? reply.error ()->what () : "request failed"};
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

class profile_command_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = profile_msg_t;
    using reply_type = operation_status_t;

    explicit profile_command_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    operation_status_t handle (const profile_msg_t &command)
    {
        _channels.send (api_channel, command).submit ();
        return {.status = "sent"};
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

class missing_profile_command_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = profile_msg_t;
    using reply_type = operation_status_t;

    explicit missing_profile_command_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    operation_status_t handle (const profile_msg_t &command)
    {
        _channels.send (api_channel, command).packet_name ("MissingProfileMsg").submit ();
        return {.status = "sent"};
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

class transient_profile_request_service_t final : public zlink::framework::hosted_service_t
{
  public:
    struct result_state_t
    {
        std::optional<profile_res_t> reply;
        std::optional<std::string> error;

        profile_res_t take_reply () const
        {
            if (error) {
                throw std::runtime_error (*error);
            }
            if (!reply) {
                throw std::runtime_error ("transient profile request produced no reply");
            }
            return *reply;
        }
    };

    transient_profile_request_service_t (zlink::framework::app_t &app,
                                         profile_req_t request,
                                         std::shared_ptr<result_state_t> result) :
        _app (app), _request (std::move (request)), _result (std::move (result))
    {
    }

    void start (zlink::framework::service_provider_t &services) override
    {
        try {
            auto &channels = services.get_required<zlink::framework::channel_client_t> ();
            _result->reply = request_profile_with_retry (channels, _request);
        }
        catch (const std::exception &error) {
            _result->error = error.what ();
        }
        _app.stop ();
    }

    void stop () noexcept override {}

  private:
    zlink::framework::app_t &_app;
    profile_req_t _request;
    std::shared_ptr<result_state_t> _result;
};

inline profile_res_t request_profile_with_new_client_host (const consumer_options_t &options,
                                                           const profile_req_t &request)
{
    const auto trace_id = request.marker.empty () ? request.value : request.marker;
    auto app = zlink::framework::app_t::create ();
    auto result = std::make_shared<transient_profile_request_service_t::result_state_t> ();
    auto service = std::make_unique<transient_profile_request_service_t> (app, request, result);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        server::add_redis_location_store (framework, options.redis_endpoint,
                                          options.redis_key_prefix);
        framework.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (options.log_dir + "/storm-" + trace_id + "-flow.log")
          .trace_label ("storm-" + trace_id);
        framework.add_client_server_channel (api_channel).enable_client ();
    });
    app.add_hosted_service (std::move (service));
    const auto exit_code = app.run (0, nullptr);
    if (exit_code != 0) {
        throw std::runtime_error ("transient profile request host failed");
    }
    return result->take_reply ();
}

class new_client_profile_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<consumer_options_t>;
    using request_type = profile_req_t;
    using reply_type = profile_res_t;

    explicit new_client_profile_request_handler_t (consumer_options_t &options) :
        _options (options)
    {
    }

    profile_res_t handle (const profile_req_t &request)
    {
        return request_profile_with_new_client_host (_options, request);
    }

  private:
    consumer_options_t &_options;
};

} // namespace zlink::framework::e2e::resilience_lifecycle::consumer
