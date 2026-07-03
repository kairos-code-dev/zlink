/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/configuration/logging.hpp>
#include <zlink/framework/contracts/eventing/events.hpp>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace zlink::framework::extensions
{

struct extension_descriptor_t
{
    std::string name;
    std::string target_name;
    bool depends_on_core_only = true;
};

class metrics_extension_t
{
  public:
    metrics_extension_t &use_framework_logging (logging_builder_t &logging)
    {
        logging.set_level ("info");
        logging_policy_attached = true;
        return *this;
    }

    metrics_extension_t &observe_runtime_events (monitoring_builder_t &monitoring)
    {
        monitoring.on_trace ([this] (const runtime_event_base_t &event) {
            observed_sources.push_back (event.source_name);
        });
        return *this;
    }

    bool logging_policy_attached = false;
    std::vector<std::string> observed_sources;
};

class tracing_extension_t
{
  public:
    tracing_extension_t &use_framework_logging (logging_builder_t &logging)
    {
        logging.set_level ("trace");
        logging_policy_attached = true;
        return *this;
    }

    tracing_extension_t &use_monitoring_events (monitoring_builder_t &monitoring)
    {
        monitoring.on_trace (
          [this] (const runtime_event_base_t &event) { traces.push_back (event.correlation_id); });
        return *this;
    }

    bool logging_policy_attached = false;
    std::vector<std::string> traces;
};

struct bridge_endpoint_t
{
    std::string name;
    std::string endpoint;
};

class kafka_bridge_extension_t
{
  public:
    kafka_bridge_extension_t &map_topic (std::string topic, std::string channel_name)
    {
        mappings.push_back ({std::move (topic), std::move (channel_name)});
        return *this;
    }

    std::vector<bridge_endpoint_t> mappings;
};

class grpc_bridge_extension_t
{
  public:
    grpc_bridge_extension_t &map_service (std::string service_name, std::string channel_name)
    {
        mappings.push_back ({std::move (service_name), std::move (channel_name)});
        return *this;
    }

    std::vector<bridge_endpoint_t> mappings;
};

class http_gateway_extension_t
{
  public:
    http_gateway_extension_t &map_route (std::string route, std::string channel_name)
    {
        mappings.push_back ({std::move (route), std::move (channel_name)});
        return *this;
    }

    std::vector<bridge_endpoint_t> mappings;
};

struct retry_policy_t
{
    std::size_t max_attempts = 3;
    bool preserve_ordering = true;
    bool require_idempotency_key = true;
    std::string duplicate_delivery_policy =
      "retries may duplicate delivery; handlers must use the idempotency key";
    std::string ordering_policy = "preserve ordering within a channel unless explicitly disabled";
};

class advanced_retry_extension_t
{
  public:
    advanced_retry_extension_t &policy (retry_policy_t value)
    {
        current_policy = value;
        return *this;
    }

    retry_policy_t current_policy;
};

class dead_letter_storage_extension_t
{
  public:
    dead_letter_storage_extension_t &
    store (std::function<void (const channel_reliability_event_t &)> sink)
    {
        storage_sink = std::move (sink);
        return *this;
    }

    std::string duplicate_handling =
      "dead-letter records retain the idempotency key for duplicate detection";
    std::string ordering_handling =
      "dead-letter storage preserves the failed operation observation order";
    std::function<void (const channel_reliability_event_t &)> storage_sink;
};

class flatbuffers_codec_extension_t
{
  public:
    template <typename T> flatbuffers_codec_extension_t &register_type ()
    {
        registered_types.push_back (typeid (T).name ());
        return *this;
    }

    std::vector<std::string> registered_types;
};

class yaml_configuration_extension_t
{
  public:
    yaml_configuration_extension_t &load_file (std::string path)
    {
        paths.push_back (std::move (path));
        return *this;
    }

    std::vector<std::string> paths;
};

class custom_codec_extension_point_t
{
  public:
    custom_codec_extension_point_t &add (std::string codec_name)
    {
        codecs.push_back (std::move (codec_name));
        return *this;
    }

    std::vector<std::string> codecs;
};

class custom_transport_extension_point_t
{
  public:
    custom_transport_extension_point_t &add_scheme (std::string scheme)
    {
        schemes.push_back (std::move (scheme));
        return *this;
    }

    std::vector<std::string> schemes;
};

inline std::vector<extension_descriptor_t> known_extensions ()
{
    return {{"metrics", "zlink::framework_extension_metrics"},
            {"tracing", "zlink::framework_extension_tracing"},
            {"kafka", "zlink::framework_extension_kafka_bridge"},
            {"grpc", "zlink::framework_extension_grpc_bridge"},
            {"http", "zlink::framework_extension_http_gateway"},
            {"advanced-retry", "zlink::framework_extension_advanced_retry"},
            {"dead-letter", "zlink::framework_extension_dead_letter_storage"},
            {"flatbuffers", "zlink::framework_extension_flatbuffers"},
            {"yaml", "zlink::framework_extension_yaml_config"},
            {"custom-codec", "zlink::framework_extension_custom_codec"},
            {"custom-transport", "zlink::framework_extension_custom_transport"},
            {"locations-redis", "zlink::framework_locations_redis", false}};
}

} // namespace zlink::framework::extensions
