/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework/extensions.hpp>

#include <string>

struct flatbuffer_message_t
{
};

int main ()
{
    namespace ext = zlink::framework::extensions;

    auto descriptors = ext::known_extensions ();
    if (descriptors.size () != 11) {
        return 1;
    }
    for (const auto &descriptor : descriptors) {
        if (descriptor.target_name.rfind ("zlink::framework_extension_", 0) != 0
            || !descriptor.depends_on_core_only) {
            return 2;
        }
    }

    zlink::framework::monitoring_builder_t monitoring;
    zlink::framework::logging_builder_t logging;
    ext::metrics_extension_t metrics;
    ext::tracing_extension_t tracing;
    metrics.use_framework_logging (logging);
    metrics.observe_runtime_events (monitoring);
    tracing.use_framework_logging (logging);
    tracing.use_monitoring_events (monitoring);
    monitoring.on_trace ([&] (const zlink::framework::runtime_event_base_t &) {});
    if (!metrics.logging_policy_attached || !tracing.logging_policy_attached) {
        return 7;
    }

    ext::kafka_bridge_extension_t kafka;
    ext::grpc_bridge_extension_t grpc;
    ext::http_gateway_extension_t http;
    kafka.map_topic ("moves", "game.moves");
    grpc.map_service ("GameService", "game.moves");
    http.map_route ("/moves", "game.moves");
    if (kafka.mappings.empty () || grpc.mappings.empty () || http.mappings.empty ()) {
        return 3;
    }

    ext::advanced_retry_extension_t retry;
    retry.policy ({.max_attempts = 5, .preserve_ordering = true, .require_idempotency_key = true});
    if (!retry.current_policy.preserve_ordering || !retry.current_policy.require_idempotency_key
        || retry.current_policy.duplicate_delivery_policy.find ("idempotency")
             == std::string::npos) {
        return 4;
    }

    int dead_letter_count = 0;
    ext::dead_letter_storage_extension_t dead_letter;
    dead_letter.store (
      [&] (const zlink::framework::channel_reliability_event_t &) { ++dead_letter_count; });
    dead_letter.storage_sink (zlink::framework::channel_reliability_event_t{
      "game.moves", "id-1", zlink::framework::framework_error_kind_t::timeout, "timeout"});
    if (dead_letter_count != 1) {
        return 5;
    }
    if (dead_letter.duplicate_handling.find ("idempotency") == std::string::npos
        || dead_letter.ordering_handling.find ("order") == std::string::npos) {
        return 8;
    }

    ext::flatbuffers_codec_extension_t flatbuffers;
    ext::yaml_configuration_extension_t yaml;
    ext::custom_codec_extension_point_t custom_codec;
    ext::custom_transport_extension_point_t custom_transport;
    flatbuffers.register_type<flatbuffer_message_t> ();
    yaml.load_file ("app.yaml");
    custom_codec.add ("custom-binary");
    custom_transport.add_scheme ("custom+tcp");
    if (flatbuffers.registered_types.empty () || yaml.paths.empty () || custom_codec.codecs.empty ()
        || custom_transport.schemes.empty ()) {
        return 6;
    }

    return 0;
}
