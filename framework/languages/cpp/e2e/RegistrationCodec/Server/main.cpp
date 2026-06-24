/* SPDX-License-Identifier: MPL-2.0 */

#include "../Shared/registration_codec_contracts.hpp"

#include <zlink/framework.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace e2e = zlink::framework::e2e::registration_codec;

namespace
{

std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

class scenario_state_t
{
  public:
    void record (std::string marker, std::string value)
    {
        std::lock_guard lock (_mutex);
        _entries.push_back ({std::move (marker), std::move (value)});
    }

    e2e::evidence_snapshot_t snapshot () const
    {
        std::lock_guard lock (_mutex);
        return {.entries = _entries};
    }

  private:
    mutable std::mutex _mutex;
    std::vector<e2e::evidence_entry_t> _entries;
};

class auto_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;
    using request_type = e2e::echo_auto_t;
    using reply_type = e2e::echo_auto_reply_t;

    explicit auto_request_handler_t (scenario_state_t &state) : _state (state) {}

    e2e::echo_auto_reply_t handle (const e2e::echo_auto_t &request)
    {
        _state.record ("RC-A1-request", request.value);
        return {.value = "auto:" + request.value};
    }

  private:
    scenario_state_t &_state;
};

class auto_send_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;
    using message_type = e2e::echo_auto_send_t;

    explicit auto_send_handler_t (scenario_state_t &state) : _state (state) {}

    void handle (const e2e::echo_auto_send_t &message)
    {
        _state.record ("RC-A1-send", message.value);
    }

  private:
    scenario_state_t &_state;
};

class json_roundtrip_handler_t
{
  public:
    using request_type = e2e::json_roundtrip_t;
    using reply_type = e2e::json_roundtrip_reply_t;

    e2e::json_roundtrip_reply_t handle (const e2e::json_roundtrip_t &request)
    {
        return {.value = "json:" + request.value};
    }
};

class custom_roundtrip_handler_t
{
  public:
    using request_type = e2e::custom_roundtrip_t;
    using reply_type = e2e::custom_roundtrip_reply_t;

    e2e::custom_roundtrip_reply_t handle (const e2e::custom_roundtrip_t &request)
    {
        return {.value = "custom:" + request.value};
    }
};

class mismatch_roundtrip_handler_t
{
  public:
    using request_type = e2e::mismatch_roundtrip_t;
    using reply_type = e2e::mismatch_roundtrip_reply_t;

    e2e::mismatch_roundtrip_reply_t handle (const e2e::mismatch_roundtrip_t &request)
    {
        return {.value = "mismatch:" + request.value};
    }
};

class manual_route_handler_t
{
  public:
    e2e::echo_manual_reply_t handle (const e2e::echo_manual_t &request,
                                     const zlink::framework::route_handler_context_t &context)
    {
        return {.value = "manual:" + request.value,
                .packet_name = context.packet_name,
                .content_type = context.content_type};
    }
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

zlink::framework::encoded_payload_t encode_text (const std::string &prefix,
                                                 const std::string &value)
{
    return zlink::framework::encoded_payload_t::from_string (prefix + ":" + value);
}

std::string decode_text (const zlink::framework::encoded_payload_t &payload,
                         const std::string &prefix)
{
    const auto text = payload.to_string ();
    const auto expected = prefix + ":";
    if (text.rfind (expected, 0) != 0) {
        throw zlink::framework::framework_exception_t (
          zlink::framework::framework_error_kind_t::payload_decode_failed,
          "custom payload prefix mismatch");
    }
    return text.substr (expected.size ());
}

void add_json_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.add_json ()
      .add_json<e2e::echo_auto_t> ()
      .add_json<e2e::echo_auto_reply_t> ()
      .add_json<e2e::echo_auto_send_t> ()
      .add_json<e2e::echo_manual_t> ()
      .add_json<e2e::echo_manual_reply_t> ()
      .add_json<e2e::json_roundtrip_t> ()
      .add_json<e2e::json_roundtrip_reply_t> ()
      .add_json<e2e::evidence_entry_t> ()
      .add_json<e2e::evidence_snapshot_t> ();
}

void add_custom_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs
      .add_serializer<e2e::custom_roundtrip_t> (
        [] (const e2e::custom_roundtrip_t &value) {
            return encode_text ("custom-request", value.value);
        },
        [] (const zlink::framework::encoded_payload_t &payload) {
            return e2e::custom_roundtrip_t{.value = decode_text (payload, "custom-request")};
        })
      .add_serializer<e2e::custom_roundtrip_reply_t> (
        [] (const e2e::custom_roundtrip_reply_t &value) {
            return encode_text ("custom-reply", value.value);
        },
        [] (const zlink::framework::encoded_payload_t &payload) {
            return e2e::custom_roundtrip_reply_t{.value = decode_text (payload, "custom-reply")};
        })
      .add_serializer<e2e::mismatch_roundtrip_t> (
        [] (const e2e::mismatch_roundtrip_t &value) {
            return encode_text ("mismatch-request", value.value);
        },
        [] (const zlink::framework::encoded_payload_t &payload) {
            return e2e::mismatch_roundtrip_t{.value = decode_text (payload, "mismatch-request")};
        })
      .add_serializer<e2e::mismatch_roundtrip_reply_t> (
        [] (const e2e::mismatch_roundtrip_reply_t &value) {
            return encode_text ("mismatch-reply", value.value);
        },
        [] (const zlink::framework::encoded_payload_t &payload) {
            return e2e::mismatch_roundtrip_reply_t{
              .value = decode_text (payload, "mismatch-reply")};
        });
}

void configure_invalid (zlink::framework::zlink_framework_options_t &options,
                        const std::string &mode,
                        const std::string &endpoint)
{
    if (mode == "duplicate") {
        options.handlers ()
          .add<auto_request_handler_t> (e2e::handler_group)
          .add<auto_request_handler_t> (e2e::handler_group);
        return;
    }
    if (mode == "wrong-group") {
        options.handlers ().add_send<auto_send_handler_t> (e2e::handler_group);
        options.add_fanout_channel ("registration.codec.invalid")
          .enable_subscriber (endpoint)
          .use_handler_group (e2e::handler_group);
        return;
    }
    if (mode == "unsupported-channel") {
        options.add_client_server_channel ("registration.codec.invalid").enable_server (endpoint);
        return;
    }
    throw std::runtime_error ("unknown invalid mode " + mode);
}

} // namespace

int main (int argc, char **argv)
{
    try {
        const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
        const auto api_endpoint = env_or ("ZLINK_CPP_E2E_API_ENDPOINT");
        const auto route_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_ENDPOINT");
        const auto http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
        const auto invalid_mode = env_or ("ZLINK_CPP_E2E_INVALID_MODE");

        auto app = zlink::framework::app_t::create ();
        app.logging ().use_file (log_dir + "/server.log").set_min_level (
          zlink::framework::log_level_t::debug);
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.configure_dispatch ()
              .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
              .trace_log_file (log_dir + "/server-flow.log")
              .trace_node_id ("cpp-rc-server");
            if (!invalid_mode.empty ()) {
                configure_invalid (options, invalid_mode, api_endpoint);
                return;
            }
            options.services ().add_singleton<scenario_state_t> (
              std::make_unique<scenario_state_t> ());
            options.services ().add_transient<manual_route_handler_t> ();
            add_json_codecs (options.codecs ());
            add_custom_codecs (options.codecs ());
            options.handlers ()
              .add<auto_request_handler_t> (e2e::handler_group)
              .add_send<auto_send_handler_t> (e2e::handler_group)
              .add<json_roundtrip_handler_t> (e2e::handler_group)
              .add<custom_roundtrip_handler_t> (e2e::handler_group)
              .add<mismatch_roundtrip_handler_t> (e2e::handler_group);
            options.add_client_server_channel (e2e::api_channel)
              .enable_server (api_endpoint)
              .use_handler_group (e2e::handler_group);
            options.add_route_mesh (e2e::route_channel)
              .enable_server (route_endpoint)
              .set_routing_id (zlink::routing_id_t::from ("rc-server"))
              .add_request_handler<manual_route_handler_t, e2e::echo_manual_t,
                                   e2e::echo_manual_reply_t> ("EchoManual",
                                                              &manual_route_handler_t::handle);
            options.http ()
              .listen (http_endpoint)
              .map_health ("/health")
              .map_get<evidence_handler_t> ("/evidence");
        });
        return app.run (argc, argv);
    }
    catch (const std::exception &error) {
        std::cerr << "registration-codec server failed: " << error.what () << "\n";
        return 2;
    }
}
