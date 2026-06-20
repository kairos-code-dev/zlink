/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>
#include <zlink/http_client.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace
{

struct scenario_options_t
{
    std::string scenario_file;
    std::string work_dir;
    std::string log_dir;
    std::string report_file;
    std::string ready_file;
    std::string zlink_endpoint;
    std::string http_endpoint;
    std::string channel;
    std::string server_name = "api-a";
    std::string mode = "client-server";
    std::string route_peer_endpoints;
    std::string executed_script;
    std::string server_pids;
};

struct scenario_roles_t
{
    struct server_role_t
    {
        std::string name;
        std::string channel;
    };

    struct client_role_t
    {
        std::string name;
        std::string routing_id;
    };

    std::vector<server_role_t> server;
    std::vector<client_role_t> client;
};

struct scenario_step_t
{
    struct wait_ready_t
    {
        std::string target;
        std::vector<std::string> targets;
    };

    struct wait_for_t
    {
        int milliseconds = 0;
    };

    struct client_request_t
    {
        std::string client;
        std::string channel;
        std::string request_id;
        std::string value;
        std::string expect_reply;
    };

    struct assert_evidence_t
    {
        std::string target;
        std::string request_id;
        std::string command_id;
    };

    struct client_send_t
    {
        std::string client;
        std::string channel;
        std::string packet_name;
        std::string command_id;
        std::string value;
    };

    struct client_timeout_request_t
    {
        std::string client;
        std::string channel;
        std::string packet_name;
        std::string request_id;
        std::string value;
        int timeout_ms = 0;
        std::string expect_error;
    };

    struct concurrent_timeout_and_request_t
    {
        std::string client;
        std::string channel;
        std::string packet_name;
        std::string timeout_request_id;
        std::string timeout_value;
        int timeout_ms = 0;
        std::string request_id;
        std::string value;
        std::string expect_reply;
        int request_delay_ms = 0;
        std::string expect_error;
    };

    struct round_robin_request_t
    {
        std::string client;
        std::string channel;
        std::string packet_name;
        int warmup_count = 0;
        int request_count = 0;
        std::string request_id_prefix;
        std::string value_prefix;
    };

    struct route_targeted_request_t
    {
        std::string client;
        std::string channel;
        std::string packet_name;
        std::string target_rid;
        std::string non_target_rid;
        std::string source_rid;
        std::string missing_rid;
        int missing_timeout_ms = 0;
        std::string request_id;
        std::string value;
        std::string expect_reply;
    };

    struct unregistered_request_t
    {
        std::string client;
        std::string channel;
        std::string packet_name;
        std::string request_id;
        std::string value;
        std::string expect_error;
    };

    struct unregistered_send_t
    {
        std::string client;
        std::string channel;
        std::string packet_name;
        std::string command_id;
        std::string value;
        std::string expect_action;
    };

    struct handler_exception_request_t
    {
        std::string client;
        std::string channel;
        std::string packet_name;
        std::string request_id;
        std::string value;
        std::string expect_error;
    };

    struct assert_distribution_t
    {
        std::map<std::string, int> expected_counts;
    };

    std::optional<wait_ready_t> wait_ready;
    std::optional<wait_for_t> wait_for;
    std::optional<client_request_t> client_request;
    std::optional<client_send_t> client_send;
    std::optional<client_timeout_request_t> client_timeout_request;
    std::optional<concurrent_timeout_and_request_t> concurrent_timeout_and_request;
    std::optional<assert_evidence_t> assert_evidence;
    std::optional<round_robin_request_t> round_robin_request;
    std::optional<route_targeted_request_t> route_targeted_request;
    std::optional<unregistered_request_t> unregistered_request;
    std::optional<unregistered_send_t> unregistered_send;
    std::optional<handler_exception_request_t> handler_exception_request;
    std::optional<assert_distribution_t> assert_distribution;
};

struct scenario_expect_t
{
    struct reply_t
    {
        std::string request_id;
        std::string value;
        std::string request_id_prefix;
        std::string value_prefix;
    };

    struct server_evidence_t
    {
        std::vector<std::string> request_ids;
        std::vector<std::string> command_ids;
        std::vector<std::string> route_request_ids;
        std::vector<std::string> route_source_rids;
        std::vector<std::string> dispatch_errors;
        std::map<std::string, int> request_counts;
    };

    reply_t reply;
    server_evidence_t server_evidence;
};

struct scenario_artifacts_t
{
    std::string logs;
    std::string evidence;
    std::string report;
};

struct scenario_document_t
{
    std::string id;
    std::string name;
    scenario_roles_t roles;
    std::vector<scenario_step_t> steps;
    scenario_expect_t expect;
    scenario_artifacts_t artifacts;
};

struct scenario_profile_request_t
{
    static constexpr const char *packet_name = "ScenarioProfileRequest";

    std::string request_id;
    std::string value;
};

struct scenario_profile_reply_t
{
    std::string request_id;
    std::string value;
    std::string server_id;
};

struct scenario_profile_command_t
{
    static constexpr const char *packet_name = "ScenarioProfileCommand";

    std::string command_id;
    std::string value;
};

struct scenario_evidence_snapshot_t
{
    std::string server_id;
    std::vector<std::string> request_ids;
    std::vector<std::string> command_ids;
    std::vector<std::string> route_request_ids;
    std::vector<std::string> route_source_rids;
    std::vector<std::string> dispatch_errors;
};

struct scenario_health_t
{
    std::string status;
};

struct empty_http_request_t
{
};

struct scenario_evidence_t
{
    explicit scenario_evidence_t (std::string server_id = "api-a") :
        server_id (std::move (server_id))
    {
    }

    void add_request_id (std::string request_id)
    {
        std::lock_guard lock (mutex);
        request_ids.push_back (std::move (request_id));
    }

    void add_command_id (std::string command_id)
    {
        std::lock_guard lock (mutex);
        command_ids.push_back (std::move (command_id));
    }

    void add_route_request (std::string request_id, std::string source_rid)
    {
        std::lock_guard lock (mutex);
        route_request_ids.push_back (std::move (request_id));
        route_source_rids.push_back (std::move (source_rid));
    }

    void add_dispatch_error (std::string marker)
    {
        std::lock_guard lock (mutex);
        dispatch_errors.push_back (std::move (marker));
    }

    std::vector<std::string> snapshot_requests () const
    {
        std::lock_guard lock (mutex);
        return request_ids;
    }

    std::vector<std::string> snapshot_commands () const
    {
        std::lock_guard lock (mutex);
        return command_ids;
    }

    std::vector<std::string> snapshot_route_requests () const
    {
        std::lock_guard lock (mutex);
        return route_request_ids;
    }

    std::vector<std::string> snapshot_route_sources () const
    {
        std::lock_guard lock (mutex);
        return route_source_rids;
    }

    std::vector<std::string> snapshot_dispatch_errors () const
    {
        std::lock_guard lock (mutex);
        return dispatch_errors;
    }

    mutable std::mutex mutex;
    std::string server_id;
    std::vector<std::string> request_ids;
    std::vector<std::string> command_ids;
    std::vector<std::string> route_request_ids;
    std::vector<std::string> route_source_rids;
    std::vector<std::string> dispatch_errors;
};

std::string dispatch_surface_name (zlink::framework::dispatch_error_surface_t value)
{
    switch (value) {
        case zlink::framework::dispatch_error_surface_t::channel:
            return "channel";
        case zlink::framework::dispatch_error_surface_t::dealer_mesh_channel:
            return "dealer_mesh_channel";
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

std::string dispatch_kind_name (zlink::framework::dispatch_message_kind_t value)
{
    switch (value) {
        case zlink::framework::dispatch_message_kind_t::request:
            return "request";
        case zlink::framework::dispatch_message_kind_t::send:
            return "send";
        case zlink::framework::dispatch_message_kind_t::publish:
            return "publish";
        case zlink::framework::dispatch_message_kind_t::response:
            return "response";
        case zlink::framework::dispatch_message_kind_t::error:
            return "error";
        case zlink::framework::dispatch_message_kind_t::actor_request:
            return "actor_request";
        case zlink::framework::dispatch_message_kind_t::actor_send:
            return "actor_send";
    }
    return "unknown";
}

std::string dispatch_reason_name (zlink::framework::dispatch_error_reason_t value)
{
    switch (value) {
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

std::string dispatch_action_name (zlink::framework::dispatch_error_action_t value)
{
    switch (value) {
        case zlink::framework::dispatch_error_action_t::reply_error:
            return "reply_error";
        case zlink::framework::dispatch_error_action_t::drop:
            return "drop";
    }
    return "unknown";
}

std::string dispatch_error_marker (
  const zlink::framework::message_dispatch_error_event_t &event)
{
    std::ostringstream output;
    output << "surface=" << dispatch_surface_name (event.surface)
           << " kind=" << dispatch_kind_name (event.message_kind)
           << " reason=" << dispatch_reason_name (event.reason)
           << " action=" << dispatch_action_name (event.action);
    if (event.packet_name) {
        output << " packetName=" << *event.packet_name;
    }
    if (event.channel_name) {
        output << " channelName=" << *event.channel_name;
    }
    if (event.correlation_id) {
        output << " correlationId=" << *event.correlation_id;
    }
    return output.str ();
}

class scenario_profile_handler_t
{
  public:
    using request_type = scenario_profile_request_t;
    using reply_type = scenario_profile_reply_t;
    using dependency_types = zlink::framework::dependency_list_t<scenario_evidence_t>;

    explicit scenario_profile_handler_t (scenario_evidence_t &evidence) : _evidence (&evidence) {}

    reply_type handle (const request_type &request)
    {
        _evidence->add_request_id (request.request_id);
        if (request.value == "throw") {
            throw zlink::framework::framework_exception_t (
              zlink::framework::framework_error_kind_t::request_failed,
              "DERR-007 handler exception");
        }
        if (request.value == "slow" || request.request_id.find ("timeout") != std::string::npos) {
            std::this_thread::sleep_for (std::chrono::milliseconds (1000));
        }
        return {.request_id = request.request_id,
                .value = "profile:" + request.value,
                .server_id = _evidence->server_id};
    }

  private:
    scenario_evidence_t *_evidence;
};

class scenario_profile_command_handler_t
{
  public:
    using message_type = scenario_profile_command_t;
    using dependency_types = zlink::framework::dependency_list_t<scenario_evidence_t>;

    explicit scenario_profile_command_handler_t (scenario_evidence_t &evidence) :
        _evidence (&evidence)
    {
    }

    void handle (const message_type &message)
    {
        _evidence->add_command_id (message.command_id);
    }

  private:
    scenario_evidence_t *_evidence;
};

class scenario_route_ping_handler_t
{
  public:
    using request_type = scenario_profile_request_t;
    using reply_type = scenario_profile_reply_t;
    using dependency_types = zlink::framework::dependency_list_t<scenario_evidence_t>;

    explicit scenario_route_ping_handler_t (scenario_evidence_t &evidence) :
        _evidence (&evidence)
    {
    }

    reply_type handle (const request_type &request,
                       const zlink::framework::route_handler_context_t &context)
    {
        _evidence->add_route_request (request.request_id,
                                      context.source_node_rid.to_string ());
        return {.request_id = request.request_id,
                .value = "route:" + request.value,
                .server_id = _evidence->server_id};
    }

  private:
    scenario_evidence_t *_evidence;
};

class health_http_handler_t
{
  public:
    using request_type = empty_http_request_t;
    using reply_type = scenario_health_t;

    reply_type handle (const request_type &, zlink::framework::http_context_t &)
    {
        return {.status = "ready"};
    }
};

class evidence_http_handler_t
{
  public:
    using request_type = empty_http_request_t;
    using reply_type = scenario_evidence_snapshot_t;
    using dependency_types = zlink::framework::dependency_list_t<scenario_evidence_t>;

    explicit evidence_http_handler_t (scenario_evidence_t &evidence) : _evidence (&evidence) {}

    reply_type handle (const request_type &, zlink::framework::http_context_t &)
    {
        return {.server_id = _evidence->server_id,
                .request_ids = _evidence->snapshot_requests (),
                .command_ids = _evidence->snapshot_commands (),
                .route_request_ids = _evidence->snapshot_route_requests (),
                .route_source_rids = _evidence->snapshot_route_sources (),
                .dispatch_errors = _evidence->snapshot_dispatch_errors ()};
    }

  private:
    scenario_evidence_t *_evidence;
};

void to_json (nlohmann::json &json, const scenario_profile_request_t &value)
{
    json = nlohmann::json{{"requestId", value.request_id}, {"value", value.value}};
}

void from_json (const nlohmann::json &json, scenario_profile_request_t &value)
{
    value.request_id = json.value ("requestId", "");
    value.value = json.value ("value", "");
}

void to_json (nlohmann::json &json, const scenario_profile_reply_t &value)
{
    json = nlohmann::json{{"requestId", value.request_id},
                          {"value", value.value},
                          {"serverId", value.server_id}};
}

void from_json (const nlohmann::json &json, scenario_profile_reply_t &value)
{
    value.request_id = json.value ("requestId", "");
    value.value = json.value ("value", "");
    value.server_id = json.value ("serverId", "");
}

void to_json (nlohmann::json &json, const scenario_profile_command_t &value)
{
    json = nlohmann::json{{"commandId", value.command_id}, {"value", value.value}};
}

void from_json (const nlohmann::json &json, scenario_profile_command_t &value)
{
    value.command_id = json.value ("commandId", "");
    value.value = json.value ("value", "");
}

void to_json (nlohmann::json &json, const scenario_evidence_snapshot_t &value)
{
    json = nlohmann::json{{"serverId", value.server_id},
                          {"requestIds", value.request_ids},
                          {"commandIds", value.command_ids},
                          {"routeRequestIds", value.route_request_ids},
                          {"routeSourceRids", value.route_source_rids},
                          {"dispatchErrors", value.dispatch_errors}};
}

void from_json (const nlohmann::json &json, scenario_evidence_snapshot_t &value)
{
    value.server_id = json.value ("serverId", "");
    value.request_ids = json.value ("requestIds", std::vector<std::string>{});
    value.command_ids = json.value ("commandIds", std::vector<std::string>{});
    value.route_request_ids =
      json.value ("routeRequestIds", std::vector<std::string>{});
    value.route_source_rids =
      json.value ("routeSourceRids", std::vector<std::string>{});
    value.dispatch_errors =
      json.value ("dispatchErrors", std::vector<std::string>{});
}

void to_json (nlohmann::json &json, const scenario_health_t &value)
{
    json = nlohmann::json{{"status", value.status}};
}

void from_json (const nlohmann::json &json, scenario_health_t &value)
{
    value.status = json.value ("status", "");
}

void to_json (nlohmann::json &json, const empty_http_request_t &)
{
    json = nlohmann::json::object ();
}

void from_json (const nlohmann::json &, empty_http_request_t &)
{
}

void from_json (const nlohmann::json &json, scenario_roles_t::server_role_t &value)
{
    value.name = json.value ("name", "");
    value.channel = json.value ("channel", "");
}

void from_json (const nlohmann::json &json, scenario_roles_t::client_role_t &value)
{
    value.name = json.value ("name", "");
    value.routing_id = json.value ("routingId", "");
}

void from_json (const nlohmann::json &json, scenario_roles_t &value)
{
    value.server = json.value ("server", std::vector<scenario_roles_t::server_role_t>{});
    value.client = json.value ("client", std::vector<scenario_roles_t::client_role_t>{});
}

void from_json (const nlohmann::json &json, scenario_step_t::wait_ready_t &value)
{
    value.target = json.value ("target", "");
    value.targets = json.value ("targets", std::vector<std::string>{});
}

void from_json (const nlohmann::json &json, scenario_step_t::wait_for_t &value)
{
    value.milliseconds = json.value ("milliseconds", 0);
}

void from_json (const nlohmann::json &json, scenario_step_t::client_request_t &value)
{
    value.client = json.value ("client", "");
    value.channel = json.value ("channel", "");
    value.request_id = json.value ("requestId", "");
    value.value = json.value ("value", "");
    value.expect_reply = json.value ("expectReply", "");
}

void from_json (const nlohmann::json &json, scenario_step_t::assert_evidence_t &value)
{
    value.target = json.value ("target", "");
    value.request_id = json.value ("requestId", "");
    value.command_id = json.value ("commandId", "");
}

void from_json (const nlohmann::json &json, scenario_step_t::client_send_t &value)
{
    value.client = json.value ("client", "");
    value.channel = json.value ("channel", "");
    value.packet_name = json.value ("packetName", "");
    value.command_id = json.value ("commandId", "");
    value.value = json.value ("value", "");
}

void from_json (const nlohmann::json &json, scenario_step_t::client_timeout_request_t &value)
{
    value.client = json.value ("client", "");
    value.channel = json.value ("channel", "");
    value.packet_name = json.value ("packetName", "");
    value.request_id = json.value ("requestId", "");
    value.value = json.value ("value", "");
    value.timeout_ms = json.value ("timeoutMs", 0);
    value.expect_error = json.value ("expectError", "");
}

void from_json (const nlohmann::json &json,
                scenario_step_t::concurrent_timeout_and_request_t &value)
{
    value.client = json.value ("client", "");
    value.channel = json.value ("channel", "");
    value.packet_name = json.value ("packetName", "");
    value.timeout_request_id = json.value ("timeoutRequestId", "");
    value.timeout_value = json.value ("timeoutValue", "");
    value.timeout_ms = json.value ("timeoutMs", 0);
    value.request_id = json.value ("requestId", "");
    value.value = json.value ("value", "");
    value.expect_reply = json.value ("expectReply", "");
    value.request_delay_ms = json.value ("requestDelayMs", 0);
    value.expect_error = json.value ("expectError", "");
}

void from_json (const nlohmann::json &json, scenario_step_t::round_robin_request_t &value)
{
    value.client = json.value ("client", "");
    value.channel = json.value ("channel", "");
    value.packet_name = json.value ("packetName", "");
    value.warmup_count = json.value ("warmupCount", 0);
    value.request_count = json.value ("requestCount", 0);
    value.request_id_prefix = json.value ("requestIdPrefix", "");
    value.value_prefix = json.value ("valuePrefix", "");
}

void from_json (const nlohmann::json &json, scenario_step_t::route_targeted_request_t &value)
{
    value.client = json.value ("client", "");
    value.channel = json.value ("channel", "");
    value.packet_name = json.value ("packetName", "");
    value.target_rid = json.value ("targetRid", "");
    value.non_target_rid = json.value ("nonTargetRid", "");
    value.source_rid = json.value ("sourceRid", "");
    value.missing_rid = json.value ("missingRid", "");
    value.missing_timeout_ms = json.value ("missingTimeoutMs", 0);
    value.request_id = json.value ("requestId", "");
    value.value = json.value ("value", "");
    value.expect_reply = json.value ("expectReply", "");
}

void from_json (const nlohmann::json &json, scenario_step_t::unregistered_request_t &value)
{
    value.client = json.value ("client", "");
    value.channel = json.value ("channel", "");
    value.packet_name = json.value ("packetName", "");
    value.request_id = json.value ("requestId", "");
    value.value = json.value ("value", "");
    value.expect_error = json.value ("expectError", "");
}

void from_json (const nlohmann::json &json, scenario_step_t::unregistered_send_t &value)
{
    value.client = json.value ("client", "");
    value.channel = json.value ("channel", "");
    value.packet_name = json.value ("packetName", "");
    value.command_id = json.value ("commandId", "");
    value.value = json.value ("value", "");
    value.expect_action = json.value ("expectAction", "");
}

void from_json (const nlohmann::json &json,
                scenario_step_t::handler_exception_request_t &value)
{
    value.client = json.value ("client", "");
    value.channel = json.value ("channel", "");
    value.packet_name = json.value ("packetName", "");
    value.request_id = json.value ("requestId", "");
    value.value = json.value ("value", "");
    value.expect_error = json.value ("expectError", "");
}

void from_json (const nlohmann::json &json, scenario_step_t::assert_distribution_t &value)
{
    value.expected_counts = json.value ("expectedCounts", std::map<std::string, int>{});
}

void from_json (const nlohmann::json &json, scenario_step_t &value)
{
    if (json.contains ("waitReady")) {
        value.wait_ready = json.at ("waitReady").get<scenario_step_t::wait_ready_t> ();
    }
    if (json.contains ("waitFor")) {
        value.wait_for = json.at ("waitFor").get<scenario_step_t::wait_for_t> ();
    }
    if (json.contains ("clientRequest")) {
        value.client_request = json.at ("clientRequest").get<scenario_step_t::client_request_t> ();
    }
    if (json.contains ("clientSend")) {
        value.client_send = json.at ("clientSend").get<scenario_step_t::client_send_t> ();
    }
    if (json.contains ("clientTimeoutRequest")) {
        value.client_timeout_request =
          json.at ("clientTimeoutRequest").get<scenario_step_t::client_timeout_request_t> ();
    }
    if (json.contains ("concurrentTimeoutAndRequest")) {
        value.concurrent_timeout_and_request =
          json.at ("concurrentTimeoutAndRequest")
            .get<scenario_step_t::concurrent_timeout_and_request_t> ();
    }
    if (json.contains ("assertEvidence")) {
        value.assert_evidence =
          json.at ("assertEvidence").get<scenario_step_t::assert_evidence_t> ();
    }
    if (json.contains ("roundRobinRequest")) {
        value.round_robin_request =
          json.at ("roundRobinRequest").get<scenario_step_t::round_robin_request_t> ();
    }
    if (json.contains ("routeTargetedRequest")) {
        value.route_targeted_request =
          json.at ("routeTargetedRequest")
            .get<scenario_step_t::route_targeted_request_t> ();
    }
    if (json.contains ("unregisteredRequest")) {
        value.unregistered_request =
          json.at ("unregisteredRequest").get<scenario_step_t::unregistered_request_t> ();
    }
    if (json.contains ("unregisteredSend")) {
        value.unregistered_send =
          json.at ("unregisteredSend").get<scenario_step_t::unregistered_send_t> ();
    }
    if (json.contains ("handlerExceptionRequest")) {
        value.handler_exception_request =
          json.at ("handlerExceptionRequest")
            .get<scenario_step_t::handler_exception_request_t> ();
    }
    if (json.contains ("assertDistribution")) {
        value.assert_distribution =
          json.at ("assertDistribution").get<scenario_step_t::assert_distribution_t> ();
    }
}

void from_json (const nlohmann::json &json, scenario_expect_t::reply_t &value)
{
    value.request_id = json.value ("requestId", "");
    value.value = json.value ("value", "");
    value.request_id_prefix = json.value ("requestIdPrefix", "");
    value.value_prefix = json.value ("valuePrefix", "");
}

void from_json (const nlohmann::json &json, scenario_expect_t::server_evidence_t &value)
{
    value.request_ids = json.value ("requestIds", std::vector<std::string>{});
    value.command_ids = json.value ("commandIds", std::vector<std::string>{});
    value.route_request_ids =
      json.value ("routeRequestIds", std::vector<std::string>{});
    value.route_source_rids =
      json.value ("routeSourceRids", std::vector<std::string>{});
    value.dispatch_errors =
      json.value ("dispatchErrors", std::vector<std::string>{});
    value.request_counts = json.value ("requestCounts", std::map<std::string, int>{});
}

void from_json (const nlohmann::json &json, scenario_expect_t &value)
{
    value.reply = json.at ("reply").get<scenario_expect_t::reply_t> ();
    value.server_evidence =
      json.at ("serverEvidence").get<scenario_expect_t::server_evidence_t> ();
}

void from_json (const nlohmann::json &json, scenario_artifacts_t &value)
{
    value.logs = json.value ("logs", "");
    value.evidence = json.value ("evidence", "");
    value.report = json.value ("report", "");
}

void from_json (const nlohmann::json &json, scenario_document_t &value)
{
    value.id = json.value ("id", "");
    value.name = json.value ("name", "");
    value.roles = json.at ("roles").get<scenario_roles_t> ();
    value.steps = json.value ("steps", std::vector<scenario_step_t>{});
    value.expect = json.at ("expect").get<scenario_expect_t> ();
    value.artifacts = json.at ("artifacts").get<scenario_artifacts_t> ();
}

void ensure (bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error ("ensure failed: " + message);
    }
}

scenario_options_t parse_options (int argc, char **argv)
{
    scenario_options_t options;
    for (int index = 2; index + 1 < argc; index += 2) {
        const std::string name = argv[index];
        const std::string value = argv[index + 1];
        if (name == "--scenario") {
            options.scenario_file = value;
        } else if (name == "--work-dir") {
            options.work_dir = value;
        } else if (name == "--log-dir") {
            options.log_dir = value;
        } else if (name == "--report") {
            options.report_file = value;
        } else if (name == "--ready-file") {
            options.ready_file = value;
        } else if (name == "--zlink-endpoint") {
            options.zlink_endpoint = value;
        } else if (name == "--http-endpoint") {
            options.http_endpoint = value;
        } else if (name == "--channel") {
            options.channel = value;
        } else if (name == "--server-name") {
            options.server_name = value;
        } else if (name == "--mode") {
            options.mode = value;
        } else if (name == "--route-peer-endpoints") {
            options.route_peer_endpoints = value;
        } else if (name == "--executed-script") {
            options.executed_script = value;
        } else if (name == "--server-pids") {
            options.server_pids = value;
        } else {
            throw std::runtime_error ("unknown option: " + name);
        }
    }
    ensure (!options.scenario_file.empty (), "scenario option is required");
    ensure (!options.work_dir.empty (), "work dir option is required");
    ensure (!options.log_dir.empty (), "log dir option is required");
    ensure (!options.report_file.empty (), "report option is required");
    ensure (!options.ready_file.empty (), "ready file option is required");
    ensure (!options.zlink_endpoint.empty (), "zlink endpoint option is required");
    ensure (!options.http_endpoint.empty (), "http endpoint option is required");
    ensure (!options.channel.empty (), "channel option is required");
    ensure (options.mode == "client-server" || options.mode == "route-peer",
            "mode must be client-server or route-peer");
    return options;
}

scenario_document_t read_scenario (const std::string &path)
{
    std::ifstream input (path);
    ensure (input.good (), "scenario file must be readable");
    nlohmann::json json;
    input >> json;
    return json.get<scenario_document_t> ();
}

zlink::http_client::client_t make_http_client (const std::string &endpoint)
{
    return zlink::http_client::client_t::create ()
      .base_url (endpoint)
      .json ()
      .timeout (std::chrono::milliseconds (3000))
      .build ();
}

bool contains (const std::vector<std::string> &values, const std::string &needle)
{
    return std::find (values.begin (), values.end (), needle) != values.end ();
}

std::vector<std::string> split_csv (const std::string &value)
{
    std::vector<std::string> result;
    std::stringstream input (value);
    std::string item;
    while (std::getline (input, item, ',')) {
        if (!item.empty ()) {
            result.push_back (item);
        }
    }
    return result;
}

std::string current_time_utc ()
{
    const auto now = std::chrono::system_clock::now ();
    const auto time = std::chrono::system_clock::to_time_t (now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s (&utc, &time);
#else
    gmtime_r (&time, &utc);
#endif
    std::ostringstream output;
    output << std::put_time (&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str ();
}

std::string evidence_paths (const std::vector<std::string> &http_endpoints)
{
    std::ostringstream output;
    for (std::size_t index = 0; index < http_endpoints.size (); ++index) {
        if (index > 0) {
            output << ',';
        }
        output << http_endpoints[index] << "/evidence";
    }
    return output.str ();
}

nlohmann::json process_ids (const std::string &server_pids,
                            const std::vector<scenario_roles_t::client_role_t> &clients)
{
    nlohmann::json result = nlohmann::json::object ();
    for (const auto &entry : split_csv (server_pids)) {
        const auto separator = entry.find ('=');
        ensure (separator != std::string::npos, "server pid entry must use name=pid");
        const auto name = entry.substr (0, separator);
        const auto pid = entry.substr (separator + 1);
        ensure (!name.empty (), "server pid entry name must not be empty");
        ensure (!pid.empty (), "server pid entry pid must not be empty");
        result["server:" + name] = std::stoll (pid);
    }
    ensure (!clients.empty (), "scenario must declare at least one client role");
    result["client:" + clients.front ().name] = static_cast<long long> (::getpid ());
    return result;
}

scenario_evidence_snapshot_t wait_for_evidence (
  zlink::http_client::client_t &http,
  const scenario_step_t::assert_evidence_t &evidence_step)
{
    scenario_evidence_snapshot_t last;
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);
    while (std::chrono::steady_clock::now () < deadline) {
        last = http.get ("/evidence").fetch<scenario_evidence_snapshot_t> ();
        const bool request_matched =
          evidence_step.request_id.empty () || contains (last.request_ids, evidence_step.request_id);
        const bool command_matched =
          evidence_step.command_id.empty () || contains (last.command_ids, evidence_step.command_id);
        if (request_matched && command_matched) {
            return last;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    return last;
}

class scenario_client_service_t final : public zlink::framework::hosted_service_t
{
  public:
    scenario_client_service_t (zlink::framework::app_t &app, scenario_options_t options) :
        _app (&app),
        _options (std::move (options))
    {
    }

    void start (zlink::framework::service_provider_t &services) override
    {
        auto &channels = services.get_required<zlink::framework::channel_client_t> ();
        const auto scenario = read_scenario (_options.scenario_file);

        ensure (scenario.id == "CH-001" || scenario.id == "CH-002" || scenario.id == "CH-004"
                  || scenario.id == "CH-006" || scenario.id == "CH-007"
                  || scenario.id == "DERR-001" || scenario.id == "DERR-002"
                  || scenario.id == "DERR-007",
                "scenario id must be supported");
        ensure (!scenario.roles.server.empty (), "scenario must declare a server role");
        ensure (std::any_of (scenario.roles.server.begin (), scenario.roles.server.end (),
                             [&] (const auto &server) {
                                 return server.channel == _options.channel;
                             }),
                "scenario server channel must match the configured channel");
        ensure (!scenario.roles.client.empty (), "scenario must declare a client role");
        ensure (std::filesystem::weakly_canonical (
                  std::filesystem::path (_options.work_dir) / scenario.artifacts.logs)
                  == std::filesystem::weakly_canonical (_options.log_dir),
                "scenario logs artifact must match configured log directory");
        ensure (std::filesystem::weakly_canonical (
                  std::filesystem::path (_options.work_dir) / scenario.artifacts.report)
                  == std::filesystem::weakly_canonical (_options.report_file),
                "scenario report artifact must match configured report file");
        ensure (scenario.artifacts.evidence == "server:/evidence"
                  || scenario.artifacts.evidence == "servers:/evidence",
                "scenario evidence artifact must use supported evidence endpoint");
        ensure (!_options.executed_script.empty (),
                "executed script path is required for scenario report");
        ensure (!_options.server_pids.empty (),
                "server process id map is required for scenario report");

        const auto http_endpoints = split_csv (_options.http_endpoint);
        std::vector<zlink::http_client::client_t> http_clients;
        for (const auto &endpoint : http_endpoints) {
            http_clients.push_back (make_http_client (endpoint));
        }
        ensure (!http_clients.empty (), "at least one HTTP evidence endpoint is required");
        auto &http = http_clients.front ();
        std::map<std::string, std::size_t> server_index_by_name;
        for (std::size_t index = 0; index < scenario.roles.server.size (); ++index) {
            server_index_by_name[scenario.roles.server[index].name] = index;
        }
        std::map<std::string, int> replies_by_server;
        for (const auto &step : scenario.steps) {
            if (step.wait_ready) {
                auto targets = step.wait_ready->targets;
                if (targets.empty ()) {
                    targets.push_back (step.wait_ready->target);
                }
                ensure (!targets.empty (), "readiness step must declare at least one target");
                ensure (targets.size () == http_clients.size (),
                        "readiness target count must match HTTP endpoint count");
                ensure (std::all_of (targets.begin (), targets.end (), [] (const auto &target) {
                            return target.rfind ("server:", 0) == 0;
                        }),
                        "readiness targets must be server roles");
                bool server_ready = false;
                for (int attempt = 0; attempt < 100 && !server_ready; ++attempt) {
                    server_ready = true;
                    for (const auto &client : http_clients) {
                        const auto health =
                          client.get ("/health").submit<scenario_health_t> ().result ();
                        server_ready =
                          server_ready && health && health.value ().body.status == "ready";
                    }
                    if (!server_ready) {
                        std::this_thread::sleep_for (std::chrono::milliseconds (100));
                    }
                }
                ensure (server_ready, "server readiness must be reached before client request");
                if (_options.mode == "route-peer") {
                    continue;
                }
                std::set<std::string> seen_servers;
                for (int attempt = 0; attempt < 60
                                      && seen_servers.size () < targets.size ();
                     ++attempt) {
                    const auto request_id = "ready-probe-" + std::to_string (attempt);
                    auto reply = channels
                                   .request_to_channel (
                                     _options.channel,
                                     scenario_profile_request_t{.request_id = request_id,
                                                                .value = request_id})
                                   .packet_name (scenario_profile_request_t::packet_name)
                                   .timeout (std::chrono::milliseconds (1000))
                                   .async<scenario_profile_reply_t> ()
                                   .result ();
                    if (reply.has_value () && !reply.value ().server_id.empty ()) {
                        seen_servers.insert (reply.value ().server_id);
                    }
                    if (seen_servers.size () < targets.size ()) {
                        std::this_thread::sleep_for (std::chrono::milliseconds (50));
                    }
                }
                ensure (seen_servers.size () == targets.size (),
                        "all manual endpoints must answer readiness probes");
                continue;
            }

            if (step.wait_for) {
                ensure (step.wait_for->milliseconds > 0, "waitFor must be positive");
                std::this_thread::sleep_for (
                  std::chrono::milliseconds (step.wait_for->milliseconds));
                continue;
            }

            if (step.client_request) {
                const auto &request = *step.client_request;
                ensure (request.client == scenario.roles.client.front ().name,
                        "request client must match scenario role");
                ensure (request.channel == _options.channel,
                        "request channel must match configured channel");
                auto reply = channels
                               .request_to_channel (
                                 request.channel,
                                 scenario_profile_request_t{.request_id = request.request_id,
                                                            .value = request.value})
                               .packet_name (scenario_profile_request_t::packet_name)
                               .timeout (std::chrono::milliseconds (3000))
                               .async<scenario_profile_reply_t> ()
                               .result ();
                ensure (reply.has_value (), "channel request must return a reply");
                ensure (reply.value ().request_id == request.request_id,
                        "reply request id must match");
                ensure (reply.value ().value == request.expect_reply,
                        "reply value must match scenario expectation");
                if (!scenario.expect.reply.request_id.empty ()
                    && scenario.expect.reply.request_id == request.request_id) {
                    ensure (reply.value ().request_id == scenario.expect.reply.request_id,
                            "reply request id must match expect block");
                    ensure (reply.value ().value == scenario.expect.reply.value,
                            "reply value must match expect block");
                }
                continue;
            }

            if (step.client_timeout_request) {
                const auto &request = *step.client_timeout_request;
                ensure (request.client == scenario.roles.client.front ().name,
                        "timeout request client must match scenario role");
                ensure (request.channel == _options.channel,
                        "timeout request channel must match configured channel");
                ensure (request.packet_name == scenario_profile_request_t::packet_name,
                        "timeout request packet name must match handler");
                ensure (request.timeout_ms > 0, "timeout request must set a positive timeout");
                ensure (request.expect_error == "timeout",
                        "timeout request expected error must be timeout");
                auto result = channels
                                .request_to_channel (
                                  request.channel,
                                  scenario_profile_request_t{.request_id = request.request_id,
                                                             .value = request.value})
                                .packet_name (request.packet_name)
                                .timeout (std::chrono::milliseconds (request.timeout_ms))
                                .async<scenario_profile_reply_t> ()
                                .result ();
                ensure (!result.has_value (),
                        result.has_value ()
                          ? "timeout request must fail, actual reply value="
                              + result.value ().value
                          : "timeout request must fail");
                ensure (result.error_kind () == zlink::framework::framework_error_kind_t::timeout,
                        "timeout request must fail with timeout, actual kind="
                          + std::to_string (static_cast<int> (result.error_kind ()))
                          + " message="
                          + (result.error () ? result.error ()->what () : ""));
                continue;
            }

            if (step.concurrent_timeout_and_request) {
                const auto &request = *step.concurrent_timeout_and_request;
                ensure (request.client == scenario.roles.client.front ().name,
                        "concurrent request client must match scenario role");
                ensure (request.channel == _options.channel,
                        "concurrent request channel must match configured channel");
                ensure (request.packet_name == scenario_profile_request_t::packet_name,
                        "concurrent request packet name must match handler");
                ensure (request.timeout_ms > 0,
                        "concurrent timeout request must set a positive timeout");
                ensure (request.request_delay_ms >= 0,
                        "concurrent request delay must be non-negative");
                ensure (request.expect_error == "timeout",
                        "concurrent timeout expected error must be timeout");

                auto timeout_future = std::async (std::launch::async, [&] {
                    return channels
                      .request_to_channel (
                        request.channel,
                        scenario_profile_request_t{.request_id = request.timeout_request_id,
                                                   .value = request.timeout_value})
                      .packet_name (request.packet_name)
                      .timeout (std::chrono::milliseconds (request.timeout_ms))
                      .async<scenario_profile_reply_t> ()
                      .result ();
                });

                if (request.request_delay_ms > 0) {
                    std::this_thread::sleep_for (
                      std::chrono::milliseconds (request.request_delay_ms));
                }

                auto reply = channels
                               .request_to_channel (
                                 request.channel,
                                 scenario_profile_request_t{.request_id = request.request_id,
                                                            .value = request.value})
                               .packet_name (request.packet_name)
                               .timeout (std::chrono::milliseconds (3000))
                               .async<scenario_profile_reply_t> ()
                               .result ();
                ensure (reply.has_value (),
                        "concurrent normal request must return a reply");
                ensure (reply.value ().request_id == request.request_id,
                        "concurrent normal reply request id must match");
                ensure (reply.value ().value == request.expect_reply,
                        "concurrent normal reply value must match");

                auto timeout_result = timeout_future.get ();
                ensure (!timeout_result.has_value (),
                        timeout_result.has_value ()
                          ? "concurrent timeout request must fail, actual reply value="
                              + timeout_result.value ().value
                          : "concurrent timeout request must fail");
                ensure (timeout_result.error_kind ()
                          == zlink::framework::framework_error_kind_t::timeout,
                        "concurrent timeout request must fail with timeout, actual kind="
                          + std::to_string (
                            static_cast<int> (timeout_result.error_kind ()))
                          + " message="
                          + (timeout_result.error () ? timeout_result.error ()->what () : ""));
                continue;
            }

            if (step.round_robin_request) {
                const auto &request = *step.round_robin_request;
                ensure (request.client == scenario.roles.client.front ().name,
                        "request client must match scenario role");
                ensure (request.channel == _options.channel,
                        "request channel must match configured channel");
                ensure (request.packet_name == scenario_profile_request_t::packet_name,
                        "request packet name must match handler");
                ensure (request.warmup_count == 3, "CH-002 warm-up count must be 3");
                ensure (request.request_count == 90, "CH-002 request count must be 90");

                for (int index = 0; index < request.warmup_count; ++index) {
                    const auto request_id =
                      request.request_id_prefix + "-warmup-" + std::to_string (index);
                    auto reply = channels
                                   .request_to_channel (
                                     request.channel,
                                     scenario_profile_request_t{
                                       .request_id = request_id,
                                       .value = request.value_prefix + "-warmup-"
                                                + std::to_string (index)})
                                   .packet_name (request.packet_name)
                                   .timeout (std::chrono::milliseconds (3000))
                                   .async<scenario_profile_reply_t> ()
                                   .result ();
                    ensure (reply.has_value (), "warm-up request must return a reply");
                    ensure (reply.value ().request_id == request_id,
                            "warm-up reply request id must match");
                }

                for (int index = 0; index < request.request_count; ++index) {
                    const auto request_id =
                      request.request_id_prefix + "-" + std::to_string (index);
                    const auto value = request.value_prefix + "-" + std::to_string (index);
                    auto reply = channels
                                   .request_to_channel (
                                     request.channel,
                                     scenario_profile_request_t{.request_id = request_id,
                                                                .value = value})
                                   .packet_name (request.packet_name)
                                   .timeout (std::chrono::milliseconds (3000))
                                   .async<scenario_profile_reply_t> ()
                                   .result ();
                    ensure (reply.has_value (), "round-robin request must return a reply");
                    ensure (reply.value ().request_id == request_id,
                            "round-robin reply request id must match");
                    ensure (reply.value ().value == "profile:" + value,
                            "round-robin reply value must match");
                    ensure (!reply.value ().server_id.empty (),
                            "round-robin reply must include server id");
                    replies_by_server[reply.value ().server_id] += 1;
                }
                continue;
            }

            if (step.route_targeted_request) {
                const auto &request = *step.route_targeted_request;
                ensure (_options.mode == "route-peer",
                        "route targeted request must run in route-peer mode");
                ensure (request.client == scenario.roles.client.front ().name,
                        "route request client must match scenario role");
                ensure (request.channel == _options.channel,
                        "route request channel must match configured channel");
                ensure (request.packet_name == scenario_profile_request_t::packet_name,
                        "route request packet name must match handler");
                ensure (!request.target_rid.empty (), "route request target rid is required");
                ensure (!request.non_target_rid.empty (),
                        "route request non-target rid is required");
                ensure (!request.source_rid.empty (), "route request source rid is required");
                ensure (!request.missing_rid.empty (), "route request missing rid is required");
                ensure (request.missing_timeout_ms > 0,
                        "route request missing timeout must be positive");

                auto &routes = services.get_required<zlink::framework::route_client_t> ();
                auto missing = routes
                                 .request (
                                   request.channel,
                                   zlink::routing_id_t::from (request.missing_rid),
                                   scenario_profile_request_t{
                                     .request_id = request.request_id + "-missing",
                                     .value = request.value})
                                 .packet_name (request.packet_name)
                                 .timeout (
                                   std::chrono::milliseconds (request.missing_timeout_ms))
                                 .async<scenario_profile_reply_t> ()
                                 .result ();
                ensure (!missing.has_value (), "missing route rid must return public error");
                ensure (missing.error_kind ()
                            == zlink::framework::framework_error_kind_t::route_not_connected
                          || missing.error_kind ()
                               == zlink::framework::framework_error_kind_t::timeout,
                        "missing route rid must fail with public route error, actual kind="
                          + std::to_string (static_cast<int> (missing.error_kind ()))
                          + " message="
                          + (missing.error () ? missing.error ()->what () : ""));

                auto reply = routes
                               .request (
                                 request.channel,
                                 zlink::routing_id_t::from (request.target_rid),
                                 scenario_profile_request_t{.request_id = request.request_id,
                                                            .value = request.value})
                               .packet_name (request.packet_name)
                               .timeout (std::chrono::milliseconds (3000))
                               .async<scenario_profile_reply_t> ()
                               .result ();
                ensure (reply.has_value (), "route targeted request must return a reply");
                ensure (reply.value ().request_id == request.request_id,
                        "route reply request id must match");
                ensure (reply.value ().value == request.expect_reply,
                        "route reply value must match scenario expectation");
                ensure (reply.value ().server_id == request.target_rid,
                        "route reply server id must match target rid");

                const auto target_index = server_index_by_name.find (request.target_rid);
                const auto non_target_index = server_index_by_name.find (request.non_target_rid);
                ensure (target_index != server_index_by_name.end (),
                        "route target rid must match a server role");
                ensure (non_target_index != server_index_by_name.end (),
                        "route non-target rid must match a server role");
                ensure (target_index->second < http_clients.size (),
                        "route target evidence endpoint must exist");
                ensure (non_target_index->second < http_clients.size (),
                        "route non-target evidence endpoint must exist");

                scenario_evidence_snapshot_t target_evidence;
                bool target_seen = false;
                for (int attempt = 0; attempt < 100 && !target_seen; ++attempt) {
                    target_evidence =
                      http_clients[target_index->second]
                        .get ("/evidence")
                        .fetch<scenario_evidence_snapshot_t> ();
                    target_seen =
                      contains (target_evidence.route_request_ids, request.request_id)
                      && contains (target_evidence.route_source_rids, request.source_rid);
                    if (!target_seen) {
                        std::this_thread::sleep_for (std::chrono::milliseconds (100));
                    }
                }
                ensure (target_seen,
                        "target server evidence must include route request and source rid");
                for (const auto &request_id :
                     scenario.expect.server_evidence.route_request_ids) {
                    ensure (contains (target_evidence.route_request_ids, request_id),
                            "target evidence must include expected route request id");
                }
                for (const auto &source_rid :
                     scenario.expect.server_evidence.route_source_rids) {
                    ensure (contains (target_evidence.route_source_rids, source_rid),
                            "target evidence must include expected route source rid");
                }

                bool non_target_seen = false;
                for (int attempt = 0; attempt < 5 && !non_target_seen; ++attempt) {
                    const auto non_target_evidence =
                      http_clients[non_target_index->second]
                        .get ("/evidence")
                        .fetch<scenario_evidence_snapshot_t> ();
                    non_target_seen =
                      contains (non_target_evidence.route_request_ids, request.request_id);
                    if (!non_target_seen) {
                        std::this_thread::sleep_for (std::chrono::milliseconds (100));
                    }
                }
                ensure (!non_target_seen,
                        "non-target server evidence must not include route request id");
                continue;
            }

            if (step.unregistered_request) {
                const auto &request = *step.unregistered_request;
                ensure (request.client == scenario.roles.client.front ().name,
                        "unregistered request client must match scenario role");
                ensure (request.channel == _options.channel,
                        "unregistered request channel must match configured channel");
                ensure (!request.packet_name.empty (),
                        "unregistered request packet name is required");
                ensure (request.expect_error == "handler_not_found",
                        "unregistered request expected error must be handler_not_found");

                auto result = channels
                                .request_to_channel (
                                  request.channel,
                                  scenario_profile_request_t{.request_id = request.request_id,
                                                             .value = request.value})
                                .packet_name (request.packet_name)
                                .timeout (std::chrono::milliseconds (3000))
                                .async<scenario_profile_reply_t> ()
                                .result ();
                ensure (!result.has_value (),
                        result.has_value ()
                          ? "unregistered request must fail, actual reply value="
                              + result.value ().value
                          : "unregistered request must fail");
                ensure (result.error_kind ()
                          == zlink::framework::framework_error_kind_t::handler_not_found,
                        "unregistered request must fail with handler_not_found, actual kind="
                          + std::to_string (static_cast<int> (result.error_kind ()))
                          + " message="
                          + (result.error () ? result.error ()->what () : ""));

                bool observed = false;
                for (int attempt = 0; attempt < 100 && !observed; ++attempt) {
                    const auto evidence =
                      http.get ("/evidence").fetch<scenario_evidence_snapshot_t> ();
                    observed = std::any_of (
                      evidence.dispatch_errors.begin (), evidence.dispatch_errors.end (),
                      [&] (const auto &marker) {
                          return marker.find ("surface=channel") != std::string::npos
                                 && marker.find ("kind=request") != std::string::npos
                                 && marker.find ("reason=handler_missing")
                                      != std::string::npos
                                 && marker.find ("action=reply_error")
                                      != std::string::npos
                                 && marker.find ("packetName=" + request.packet_name)
                                      != std::string::npos;
                      });
                    if (!observed) {
                        std::this_thread::sleep_for (std::chrono::milliseconds (100));
                    }
                }
                ensure (observed,
                        "server dispatch evidence must include unregistered request error");
                continue;
            }

            if (step.unregistered_send) {
                const auto &send = *step.unregistered_send;
                ensure (send.client == scenario.roles.client.front ().name,
                        "unregistered send client must match scenario role");
                ensure (send.channel == _options.channel,
                        "unregistered send channel must match configured channel");
                ensure (!send.packet_name.empty (),
                        "unregistered send packet name is required");
                ensure (send.expect_action == "drop",
                        "unregistered send expected action must be drop");

                auto result = channels
                                .send_to_channel (
                                  send.channel,
                                  scenario_profile_command_t{.command_id = send.command_id,
                                                             .value = send.value})
                                .packet_name (send.packet_name)
                                .async ()
                                .result ();
                ensure (result.has_value (),
                        "unregistered send submit must complete; server reports drop");

                bool observed = false;
                for (int attempt = 0; attempt < 100 && !observed; ++attempt) {
                    const auto evidence =
                      http.get ("/evidence").fetch<scenario_evidence_snapshot_t> ();
                    observed = std::any_of (
                      evidence.dispatch_errors.begin (), evidence.dispatch_errors.end (),
                      [&] (const auto &marker) {
                          return marker.find ("surface=channel") != std::string::npos
                                 && marker.find ("kind=send") != std::string::npos
                                 && marker.find ("reason=handler_missing")
                                      != std::string::npos
                                 && marker.find ("action=drop") != std::string::npos
                                 && marker.find ("packetName=" + send.packet_name)
                                      != std::string::npos;
                      });
                    if (!observed) {
                        std::this_thread::sleep_for (std::chrono::milliseconds (100));
                    }
                }
                ensure (observed,
                        "server dispatch evidence must include unregistered send drop");
                continue;
            }

            if (step.handler_exception_request) {
                const auto &request = *step.handler_exception_request;
                ensure (request.client == scenario.roles.client.front ().name,
                        "handler exception request client must match scenario role");
                ensure (request.channel == _options.channel,
                        "handler exception request channel must match configured channel");
                ensure (request.packet_name == scenario_profile_request_t::packet_name,
                        "handler exception request packet name must match handler");
                ensure (request.expect_error == "request_failed",
                        "handler exception expected error must be request_failed");

                auto result = channels
                                .request_to_channel (
                                  request.channel,
                                  scenario_profile_request_t{.request_id = request.request_id,
                                                             .value = request.value})
                                .packet_name (request.packet_name)
                                .timeout (std::chrono::milliseconds (3000))
                                .async<scenario_profile_reply_t> ()
                                .result ();
                ensure (!result.has_value (),
                        result.has_value ()
                          ? "handler exception request must fail, actual reply value="
                              + result.value ().value
                          : "handler exception request must fail");
                ensure (result.error_kind ()
                          == zlink::framework::framework_error_kind_t::request_failed,
                        "handler exception request must fail with request_failed, actual kind="
                          + std::to_string (static_cast<int> (result.error_kind ()))
                          + " message="
                          + (result.error () ? result.error ()->what () : ""));
                ensure (result.error () != nullptr
                          && std::string (result.error ()->what ())
                               == "DERR-007 handler exception",
                        "handler exception error message must be preserved");

                bool observed = false;
                for (int attempt = 0; attempt < 100 && !observed; ++attempt) {
                    const auto evidence =
                      http.get ("/evidence").fetch<scenario_evidence_snapshot_t> ();
                    observed = std::any_of (
                      evidence.dispatch_errors.begin (), evidence.dispatch_errors.end (),
                      [&] (const auto &marker) {
                          return marker.find ("surface=channel") != std::string::npos
                                 && marker.find ("kind=request") != std::string::npos
                                 && marker.find ("reason=handler_exception")
                                      != std::string::npos
                                 && marker.find ("action=reply_error")
                                      != std::string::npos
                                 && marker.find ("packetName=" + request.packet_name)
                                      != std::string::npos;
                      });
                    if (!observed) {
                        std::this_thread::sleep_for (std::chrono::milliseconds (100));
                    }
                }
                ensure (observed,
                        "server dispatch evidence must include handler exception request");
                continue;
            }

            if (step.assert_evidence) {
                const auto &evidence_step = *step.assert_evidence;
                ensure (evidence_step.target == "server:api-a",
                        "evidence target must be server:api-a");
                const auto evidence = wait_for_evidence (http, evidence_step);
                if (!evidence_step.request_id.empty ()) {
                    ensure (contains (evidence.request_ids, evidence_step.request_id),
                            "server evidence must include request id from step");
                }
                if (!evidence_step.command_id.empty ()) {
                    ensure (contains (evidence.command_ids, evidence_step.command_id),
                            "server evidence must include command id from step");
                }
                for (const auto &request_id : scenario.expect.server_evidence.request_ids) {
                    ensure (contains (evidence.request_ids, request_id),
                            "server evidence must include all expected request ids");
                }
                for (const auto &command_id : scenario.expect.server_evidence.command_ids) {
                    ensure (contains (evidence.command_ids, command_id),
                            "server evidence must include all expected command ids");
                }
                continue;
            }

            if (step.client_send) {
                const auto &send = *step.client_send;
                ensure (send.client == scenario.roles.client.front ().name,
                        "send client must match scenario role");
                ensure (send.channel == _options.channel,
                        "send channel must match configured channel");
                ensure (send.packet_name == scenario_profile_command_t::packet_name,
                        "send packet name must match handler");
                auto result = channels
                                .send_to_channel (
                                  send.channel,
                                  scenario_profile_command_t{.command_id = send.command_id,
                                                             .value = send.value})
                                .packet_name (send.packet_name)
                                .async ()
                                .result ();
                ensure (result.has_value (), "channel send must complete successfully");
                continue;
            }

            if (step.assert_distribution) {
                for (const auto &[server_id, expected_count] :
                     step.assert_distribution->expected_counts) {
                    const auto actual_count = replies_by_server[server_id];
                    ensure (actual_count == expected_count,
                            "round-robin reply count for " + server_id + " must be "
                              + std::to_string (expected_count) + ", actual "
                              + std::to_string (actual_count));
                }

                for (const auto &client : http_clients) {
                    const auto evidence =
                      client.get ("/evidence").fetch<scenario_evidence_snapshot_t> ();
                    const auto found =
                      step.assert_distribution->expected_counts.find (evidence.server_id);
                    ensure (found != step.assert_distribution->expected_counts.end (),
                            "server evidence must identify a known server");
                    int verified_requests = 0;
                    for (const auto &request_id : evidence.request_ids) {
                        if (request_id.rfind (scenario.expect.reply.request_id_prefix + "-", 0)
                              == 0
                            && request_id.find ("-warmup-") == std::string::npos) {
                            ++verified_requests;
                        }
                    }
                    ensure (verified_requests == found->second,
                            "server evidence count for " + evidence.server_id + " must be "
                              + std::to_string (found->second) + ", actual "
                              + std::to_string (verified_requests));
                }
                continue;
            }

            throw std::runtime_error ("scenario step must contain a known action");
        }

        const nlohmann::json report{
          {"scenarioId", scenario.id},
          {"language", "cpp"},
          {"runtimeVersion", __cplusplus},
          {"transportBackend", "tcp"},
          {"codec", "json"},
          {"result", "passed"},
          {"scenarioFile", _options.scenario_file},
          {"executedScriptPath", _options.executed_script},
          {"processCount", scenario.roles.server.size () + scenario.roles.client.size ()},
          {"processIds", process_ids (_options.server_pids, scenario.roles.client)},
          {"passed", 1},
          {"failed", 0},
          {"skipped", 0},
          {"logPath", _options.log_dir},
          {"evidencePath", evidence_paths (http_endpoints)},
          {"failureLayer", nullptr},
          {"completedAt", current_time_utc ()}};

        std::filesystem::create_directories (std::filesystem::path (_options.report_file).parent_path ());
        std::ofstream output (_options.report_file);
        output << report.dump (2) << '\n';
        std::cout << "scenario=" << scenario.id
                  << " language=cpp result=passed evidence=" << _options.report_file << '\n';
        _app->stop ();
    }

    void stop () noexcept override {}

  private:
    zlink::framework::app_t *_app;
    scenario_options_t _options;
};

int run_server (const scenario_options_t &options, int argc, char **argv)
{
    std::filesystem::create_directories (options.work_dir);
    auto app = zlink::framework::app_t::create ();
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        auto evidence = std::make_shared<scenario_evidence_t> (options.server_name);
        framework.services ().add_factory<scenario_evidence_t> (
          [evidence] (zlink::framework::service_provider_t &) { return evidence; },
          zlink::framework::service_lifetime_t::singleton);
        framework.services ().add_transient<scenario_route_ping_handler_t, scenario_evidence_t> ();
        framework.handlers ()
          .add<scenario_profile_handler_t> ("scenario")
          .add_send<scenario_profile_command_handler_t> ("scenario");
        framework.codecs ()
          .add_json ()
          .add_json<scenario_profile_request_t> ()
          .add_json<scenario_profile_reply_t> ()
          .add_json<scenario_profile_command_t> ()
          .add_json<scenario_health_t> ()
          .add_json<scenario_evidence_snapshot_t> ()
          .add_json<empty_http_request_t> ();
        framework.configure_dispatch ().set_message_dispatch_error_observer (
          [evidence] (const zlink::framework::message_dispatch_error_event_t &event) {
              evidence->add_dispatch_error (dispatch_error_marker (event));
          });
        if (options.mode == "route-peer") {
            auto route = framework.add_route_mesh_channel (options.channel)
                           .enable_server (options.zlink_endpoint)
                           .set_routing_id (
                             zlink::routing_id_t::from (options.server_name))
                           .add_request_handler<scenario_route_ping_handler_t,
                                                scenario_profile_request_t,
                                                scenario_profile_reply_t> (
                             scenario_profile_request_t::packet_name,
                             &scenario_route_ping_handler_t::handle);
            for (const auto &endpoint : split_csv (options.route_peer_endpoints)) {
                route.enable_client (endpoint);
            }
        } else {
            framework.add_client_server_channel (options.channel)
              .enable_server (options.zlink_endpoint)
              .use_handler_group ("scenario");
        }
        framework.http ()
          .listen (options.http_endpoint)
          .map_get<health_http_handler_t> ("/health")
          .map_get<evidence_http_handler_t> ("/evidence");
    });
    std::filesystem::create_directories (std::filesystem::path (options.ready_file).parent_path ());
    std::ofstream ready (options.ready_file);
    ready << "ready\n";
    return app.run (argc, argv);
}

int run_client (const scenario_options_t &options, int argc, char **argv)
{
    std::filesystem::create_directories (options.work_dir);
    auto app = zlink::framework::app_t::create ();
    auto service = std::make_unique<scenario_client_service_t> (app, options);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        framework.codecs ()
          .add_json ()
          .add_json<scenario_profile_request_t> ()
          .add_json<scenario_profile_reply_t> ()
          .add_json<scenario_profile_command_t> ();
        if (options.mode == "route-peer") {
            const auto scenario = read_scenario (options.scenario_file);
            ensure (!scenario.roles.client.empty (),
                    "route client scenario must declare a client role");
            const auto routing_id = scenario.roles.client.front ().routing_id.empty ()
                                      ? scenario.roles.client.front ().name
                                      : scenario.roles.client.front ().routing_id;
            auto route = framework.add_route_mesh_channel (options.channel)
                           .enable_server (options.zlink_endpoint)
                           .set_routing_id (zlink::routing_id_t::from (routing_id));
            for (const auto &endpoint : split_csv (options.route_peer_endpoints)) {
                route.enable_client (endpoint);
            }
        } else {
            auto channel = framework.add_client_server_channel (options.channel);
            for (const auto &endpoint : split_csv (options.zlink_endpoint)) {
                channel.enable_client (endpoint);
            }
        }
    });
    app.add_hosted_service (std::move (service));
    return app.run (argc, argv);
}

} // namespace

int main (int argc, char **argv)
{
    try {
        ensure (argc >= 2, "scenario E2E role is required");
        const std::string role = argv[1];
        const auto options = parse_options (argc, argv);
        if (role == "server") {
            return run_server (options, argc, argv);
        }
        if (role == "client") {
            return run_client (options, argc, argv);
        }
        throw std::runtime_error ("unknown scenario E2E role: " + role);
    }
    catch (const std::exception &ex) {
        std::cerr << ex.what () << '\n';
        return 1;
    }
}
