/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/pubsub_contracts.hpp"

#include <zlink/framework.hpp>

#include <cstdlib>
#include <string>

namespace zlink::framework::e2e::pubsub::server
{

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

inline bool env_has_topic (const std::string &topics, const std::string &topic)
{
    return ("," + topics + ",").find ("," + topic + ",") != std::string::npos;
}

inline void configure_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.add_json ();
    codecs.add_json<event_notify_t,
                    evidence_event_t,
                    dispatch_error_evidence_t,
                    evidence_snapshot_t> ();
}

inline std::string kind_name (zlink::framework::dispatch_message_kind_t value)
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

inline std::string reason_name (zlink::framework::dispatch_error_reason_t value)
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

inline std::string action_name (zlink::framework::dispatch_error_action_t value)
{
    return value == zlink::framework::dispatch_error_action_t::drop ? "drop" : "replyError";
}

inline void configure_flow (zlink::framework::zlink_framework_options_t &options,
                            const std::string &log_dir,
                            const std::string &label)
{
    options.configure_dispatch ()
      .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
      .trace_log_file (log_dir + "/" + label + "-flow.log")
      .trace_label ("cpp-ps-" + label);
}

} // namespace zlink::framework::e2e::pubsub::server
