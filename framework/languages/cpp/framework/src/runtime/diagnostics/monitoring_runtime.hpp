/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/eventing/events.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <vector>

namespace zlink::framework::detail
{

struct monitoring_source_registration_t
{
    std::string source_name;
    std::chrono::milliseconds interval{0};
};

struct socket_monitoring_source_registration_t
{
    std::string source_name;
    std::vector<socket_event_kind_t> events;
};

class monitoring_runtime_state_t
{
  public:
    std::vector<socket_monitoring_source_registration_t> socket_sources;
    std::vector<monitoring_source_registration_t> location_sources;
    std::vector<monitoring_source_registration_t> spot_sources;
    std::vector<std::string> stream_sources;
    std::vector<std::string> actor_sources;
    bool runtime_metrics_enabled = false;
    std::map<std::type_index, std::vector<std::function<void (const void *)>>> handlers;
    std::function<void (const runtime_event_base_t &)> tracing_hook;
};

class monitoring_runtime_t
{
  public:
    explicit monitoring_runtime_t (std::shared_ptr<monitoring_runtime_state_t> state);

    static monitoring_runtime_t from (const monitoring_builder_t &builder);
    const std::shared_ptr<monitoring_runtime_state_t> &state () const noexcept { return _state; }

    void publish_socket (socket_event_payload_t event) const;
    void publish_location_snapshot (std::string source_name,
                                    location_runtime_status_t status,
                                    std::vector<location_topology_entry_t> topology,
                                    std::vector<location_service_summary_t> summary) const;
    void publish_location_changes (
      std::string source_name,
      location_runtime_status_t status,
      bool status_changed,
      std::optional<std::vector<location_topology_entry_t>> topology,
      std::optional<std::vector<location_service_summary_t>> summary) const;
    void publish_spot_snapshot (spot_event_payload_t event) const;
    void publish_stream (stream_event_payload_t event) const;
    void publish_actor (actor_event_payload_t event) const;
    void publish_timer_failure (std::string source_name,
                                spot_rid_t spot_rid,
                                timer_failure_event_t failure) const;
    void publish_metric (metric_event_payload_t event) const;

  private:
    template <typename TEvent> void publish (TEvent event) const
    {
        runtime_event_publisher_t (_state).publish (std::move (event));
    }

    std::shared_ptr<monitoring_runtime_state_t> _state;
};

} // namespace zlink::framework::detail
