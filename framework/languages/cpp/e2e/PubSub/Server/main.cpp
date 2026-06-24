/* SPDX-License-Identifier: MPL-2.0 */

#include "../Shared/pubsub_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace e2e = zlink::framework::e2e::pubsub;

namespace
{

std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

bool env_has_topic (const std::string &topics, const std::string &topic)
{
    return ("," + topics + ",").find ("," + topic + ",") != std::string::npos;
}

std::string kind_name (zlink::framework::dispatch_message_kind_t value)
{
    switch (value) {
        case zlink::framework::dispatch_message_kind_t::publish:
            return "publish";
        case zlink::framework::dispatch_message_kind_t::send:
            return "send";
        case zlink::framework::dispatch_message_kind_t::request:
            return "request";
        default:
            return "other";
    }
}

std::string reason_name (zlink::framework::dispatch_error_reason_t value)
{
    switch (value) {
        case zlink::framework::dispatch_error_reason_t::handler_missing:
            return "handlerMissing";
        case zlink::framework::dispatch_error_reason_t::payload_decode_failed:
            return "payloadDecodeFailed";
        case zlink::framework::dispatch_error_reason_t::handler_exception:
            return "handlerException";
        default:
            return "other";
    }
}

std::string action_name (zlink::framework::dispatch_error_action_t value)
{
    return value == zlink::framework::dispatch_error_action_t::drop ? "drop" : "replyError";
}

class evidence_state_t
{
  public:
    evidence_state_t (std::string subscriber_id, int handler_delay_ms) :
        subscriber_id (std::move (subscriber_id)), handler_delay_ms (handler_delay_ms)
    {
    }

    void record_event (std::string topic, std::string value)
    {
        std::lock_guard lock (_mutex);
        events.push_back ({subscriber_id, std::move (topic), std::move (value)});
    }

    void record_error (const zlink::framework::message_dispatch_error_event_t &error)
    {
        std::string message;
        if (error.exception) {
            try {
                std::rethrow_exception (error.exception);
            }
            catch (const std::exception &ex) {
                message = ex.what ();
            }
            catch (...) {
                message = "unknown";
            }
        }
        std::lock_guard lock (_mutex);
        errors.push_back ({kind_name (error.message_kind), reason_name (error.reason),
                           action_name (error.action), error.packet_name.value_or (""),
                           error.topic.value_or (""), std::move (message)});
    }

    e2e::evidence_snapshot_t snapshot () const
    {
        std::lock_guard lock (_mutex);
        return {.subscriber_id = subscriber_id, .events = events, .errors = errors};
    }

    std::string subscriber_id;
    int handler_delay_ms = 0;

  private:
    mutable std::mutex _mutex;
    std::vector<e2e::evidence_event_t> events;
    std::vector<e2e::dispatch_error_evidence_t> errors;
};

template <const char *Topic> class event_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<evidence_state_t>;
    using event_type = e2e::event_notify_t;
    static constexpr const char *topic_name = Topic;

    explicit event_handler_t (evidence_state_t &state) : _state (state) {}

    void handle (const e2e::event_notify_t &event,
                 const zlink::framework::publish_context_t &context)
    {
        if (_state.handler_delay_ms > 0) {
            std::this_thread::sleep_for (std::chrono::milliseconds (_state.handler_delay_ms));
        }
        _state.record_event (context.topic, event.value);
    }

  private:
    evidence_state_t &_state;
};

inline constexpr char fanout_topic_name[] = "fanout";
inline constexpr char alpha_topic_name[] = "alpha";
inline constexpr char beta_topic_name[] = "beta";

using fanout_handler_t = event_handler_t<fanout_topic_name>;
using alpha_handler_t = event_handler_t<alpha_topic_name>;
using beta_handler_t = event_handler_t<beta_topic_name>;

class evidence_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<evidence_state_t>;

    explicit evidence_handler_t (evidence_state_t &state) : _state (state) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        zlink::framework::http_response_t response;
        response.body = nlohmann::json (_state.snapshot ()).dump ();
        return response;
    }

  private:
    evidence_state_t &_state;
};

void configure_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.add_json ()
      .add_json<e2e::event_notify_t> ()
      .add_json<e2e::evidence_event_t> ()
      .add_json<e2e::dispatch_error_evidence_t> ()
      .add_json<e2e::evidence_snapshot_t> ();
}

} // namespace

int main (int argc, char **argv)
{
    const auto role = env_or ("ZLINK_CPP_E2E_ROLE", "subscriber");
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
              .trace_node_id ("cpp-ps-registry");
            options.enable_registry (pub, router);
        });
        return app.run (argc, argv);
    }

    const auto subscriber_id = env_or ("ZLINK_CPP_E2E_SUBSCRIBER_ID", "sub-1");
    const auto topics = env_or ("ZLINK_CPP_E2E_TOPICS", "fanout");
    const auto registry_router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    const auto publisher_endpoint = env_or ("ZLINK_CPP_E2E_PUBLISHER_ENDPOINT");
    const auto http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto handler_delay_ms = std::stoi (env_or ("ZLINK_CPP_E2E_HANDLER_DELAY_MS", "0"));

    auto state = std::make_unique<evidence_state_t> (subscriber_id, handler_delay_ms);
    auto *state_ptr = state.get ();
    app.logging ()
      .use_file (log_dir + "/" + subscriber_id + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/" + subscriber_id + "-flow.log")
          .trace_node_id ("cpp-ps-" + subscriber_id)
          .set_message_dispatch_error_observer (
            [state_ptr] (const zlink::framework::message_dispatch_error_event_t &error) {
                state_ptr->record_error (error);
            });
        options.services ().add_singleton<evidence_state_t> (std::move (state));
        configure_codecs (options.codecs ());
        if (env_has_topic (topics, e2e::topic_fanout)) {
            options.handlers ().add_publish<fanout_handler_t> (e2e::handler_group);
        }
        if (env_has_topic (topics, e2e::topic_alpha)) {
            options.handlers ().add_publish<alpha_handler_t> (e2e::handler_group);
        }
        if (env_has_topic (topics, e2e::topic_beta)) {
            options.handlers ().add_publish<beta_handler_t> (e2e::handler_group);
        }
        options.use_discovery ().add_registry_endpoint (registry_router);
        auto channel = options.add_fanout_channel (e2e::event_channel);
        if (publisher_endpoint.empty ()) {
            channel.enable_subscriber ();
        } else {
            channel.enable_subscriber (publisher_endpoint);
        }
        channel.use_handler_group (e2e::handler_group);
        options.http ()
          .listen (http_endpoint)
          .map_health ("/health")
          .map_get<evidence_handler_t> ("/evidence");
    });
    return app.run (argc, argv);
}
