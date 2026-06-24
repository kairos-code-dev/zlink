/* SPDX-License-Identifier: MPL-2.0 */

#include "../Shared/registry_messaging_contracts.hpp"

#include <zlink/framework.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace e2e = zlink::framework::e2e::registry_messaging;

namespace
{

std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

std::vector<std::string> split_csv (const std::string &text)
{
    std::vector<std::string> result;
    std::stringstream input (text);
    std::string item;
    while (std::getline (input, item, ',')) {
        if (!item.empty ()) {
            result.push_back (item);
        }
    }
    return result;
}

std::optional<int> parse_int_env (const char *name)
{
    const auto value = env_or (name);
    if (value.empty ()) {
        return std::nullopt;
    }
    return std::stoi (value);
}

class scenario_state_t
{
  public:
    scenario_state_t (std::string provider_rid, std::string instance_id) :
        provider_rid (std::move (provider_rid)), instance_id (std::move (instance_id))
    {
    }

    void record (std::string marker, std::string value)
    {
        std::lock_guard lock (_mutex);
        entries.push_back ({std::move (marker), provider_rid, std::move (value)});
    }

    e2e::evidence_snapshot_t snapshot () const
    {
        std::lock_guard lock (_mutex);
        return {.provider_rid = provider_rid, .entries = entries};
    }

    std::string provider_rid;
    std::string instance_id;

  private:
    mutable std::mutex _mutex;
    std::vector<e2e::evidence_entry_t> entries;
};

class profile_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;
    using request_type = e2e::profile_request_t;
    using reply_type = e2e::profile_reply_t;

    explicit profile_request_handler_t (scenario_state_t &state) : _state (state) {}

    e2e::profile_reply_t handle (const e2e::profile_request_t &request)
    {
        if (request.value == "slow") {
            std::this_thread::sleep_for (std::chrono::seconds (1));
        }
        _state.record ("ProfileRequest", request.value);
        return {.value = "profile:" + request.value,
                .provider_rid = _state.provider_rid,
                .instance_id = _state.instance_id};
    }

  private:
    scenario_state_t &_state;
};

class profile_command_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;
    using message_type = e2e::profile_command_t;

    explicit profile_command_handler_t (scenario_state_t &state) : _state (state) {}

    void handle (const e2e::profile_command_t &command)
    {
        _state.record ("ProfileCommand", command.command_id);
    }

  private:
    scenario_state_t &_state;
};

class route_ping_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;
    using request_type = e2e::route_ping_t;
    using reply_type = e2e::route_pong_t;

    explicit route_ping_handler_t (scenario_state_t &state) : _state (state) {}

    e2e::route_pong_t handle (const e2e::route_ping_t &request,
                              const zlink::framework::route_handler_context_t &context)
    {
        _state.record ("ScenarioRoutePing", request.value);
        return {.value = "route:" + request.value,
                .target_rid = _state.provider_rid,
                .source_rid = context.source_node_rid.to_string ()};
    }

  private:
    scenario_state_t &_state;
};

class evidence_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;

    explicit evidence_handler_t (scenario_state_t &state) : _state (state) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        zlink::framework::http_response_t response;
        response.body = nlohmann::json (_state.snapshot ()).dump ();
        return response;
    }

  private:
    scenario_state_t &_state;
};

void configure_common_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.add_json ()
      .add_json<e2e::profile_request_t> ()
      .add_json<e2e::profile_reply_t> ()
      .add_json<e2e::profile_command_t> ()
      .add_json<e2e::route_ping_t> ()
      .add_json<e2e::route_pong_t> ()
      .add_json<e2e::evidence_entry_t> ()
      .add_json<e2e::evidence_snapshot_t> ();
}

} // namespace

int main (int argc, char **argv)
{
    const auto role = env_or ("ZLINK_CPP_E2E_ROLE", "provider");
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    auto app = zlink::framework::app_t::create ();

    if (role == "registry") {
        const auto pub = env_or ("ZLINK_CPP_E2E_REGISTRY_PUB");
        const auto router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
        app.logging ()
          .use_file (log_dir + "/registry.log")
          .set_min_level (zlink::framework::log_level_t::debug);
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.configure_dispatch ()
              .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
              .trace_log_file (log_dir + "/registry-flow.log")
              .trace_node_id ("cpp-rm-registry");
            options.enable_registry (pub, router);
        });
        return app.run (argc, argv);
    }

    const auto provider_rid = env_or ("ZLINK_CPP_E2E_PROVIDER_RID", "api-a");
    const auto instance_id = env_or ("ZLINK_CPP_E2E_PROVIDER_INSTANCE", provider_rid);
    const auto api_endpoint = env_or ("ZLINK_CPP_E2E_API_ENDPOINT");
    const auto workflow_endpoint = env_or ("ZLINK_CPP_E2E_WORKFLOW_ENDPOINT");
    const auto route_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_ENDPOINT");
    const auto http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto registry_router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    const auto server_weight = parse_int_env ("ZLINK_CPP_E2E_SERVER_WEIGHT");
    const auto max_message_size = parse_int_env ("ZLINK_CPP_E2E_MAX_MESSAGE_SIZE");

    app.logging ()
      .use_file (log_dir + "/" + provider_rid + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/" + provider_rid + "-flow.log")
          .trace_node_id ("cpp-rm-" + provider_rid);
        options.services ().add_singleton<scenario_state_t> (
          std::make_unique<scenario_state_t> (provider_rid, instance_id));
        options.services ().add_transient<route_ping_handler_t, scenario_state_t> ();
        configure_common_codecs (options.codecs ());
        options.handlers ()
          .add<profile_request_handler_t> (e2e::handler_group)
          .add_send<profile_command_handler_t> (e2e::handler_group);
        for (const auto &endpoint : split_csv (registry_router)) {
            options.use_discovery ().add_registry_endpoint (endpoint);
        }
        if (!api_endpoint.empty ()) {
            auto channel = options.add_client_server_channel (e2e::api_channel);
            channel.enable_server (api_endpoint)
              .server_routing_id (zlink::routing_id_t::from (provider_rid));
            if (server_weight) {
                channel.server_peer_weight (
                  zlink::peer_weight_t::value (static_cast<std::uint32_t> (*server_weight)));
            }
            if (max_message_size) {
                channel.server_max_message_size (
                  zlink::byte_size_t::bytes (static_cast<std::int64_t> (*max_message_size)));
            }
            channel.use_handler_group (e2e::handler_group);
        }
        if (!workflow_endpoint.empty ()) {
            options.add_client_server_channel (e2e::workflow_channel)
              .enable_server (workflow_endpoint)
              .server_routing_id (zlink::routing_id_t::from (provider_rid))
              .use_handler_group (e2e::handler_group);
        }
        if (!route_endpoint.empty ()) {
            options.add_route_mesh (e2e::route_channel)
              .enable_server (route_endpoint)
              .set_routing_id (zlink::routing_id_t::from (provider_rid))
              .add_request_handler<route_ping_handler_t, e2e::route_ping_t, e2e::route_pong_t> (
                "ScenarioRoutePing", &route_ping_handler_t::handle);
        }
        if (!http_endpoint.empty ()) {
            options.http ()
              .listen (http_endpoint)
              .map_health ("/health")
              .map_get<evidence_handler_t> ("/evidence");
        }
    });
    return app.run (argc, argv);
}
