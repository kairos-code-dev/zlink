/* SPDX-License-Identifier: MPL-2.0 */
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
  std::chrono::milliseconds interval { 0 };
};

class monitoring_runtime_state_t
{
public:
  std::vector<std::string> socket_sources;
  std::vector<std::string> discovery_sources;
  std::vector<monitoring_source_registration_t> registry_sources;
  std::vector<monitoring_source_registration_t> spot_sources;
  std::vector<std::string> spot_timer_sources;
  std::vector<std::string> stream_sources;
  std::vector<std::string> actor_sources;
  std::map<std::type_index, std::vector<std::function<void (const void *)>>>
    handlers;
  std::function<void (const runtime_event_base_t &)> tracing_hook;
};

class monitoring_runtime_t
{
public:
  explicit monitoring_runtime_t (
    std::shared_ptr<monitoring_runtime_state_t> state);

  static monitoring_runtime_t from (const monitoring_builder_t &builder);

  void publish_socket (socket_event_payload_t event) const;
  void publish_discovery (discovery_event_payload_t event) const;
  void publish_registry_snapshot (std::string source_name,
                                  registry_status_t status,
                                  std::vector<topology_entry_t> topology,
                                  std::vector<service_summary_entry_t> summary)
    const;
  void publish_spot_snapshot (spot_event_payload_t event) const;
  void publish_stream (stream_event_payload_t event) const;
  void publish_actor (actor_event_payload_t event) const;
  void publish_timer_failure (std::string source_name,
                              spot_rid_t spot_rid,
                              timer_failure_event_t failure) const;

private:
  template<typename TEvent>
  void publish (TEvent event) const
  {
    event.timestamp = std::chrono::system_clock::now ();
    if (_state->tracing_hook) {
      _state->tracing_hook (event);
    }
    const auto found = _state->handlers.find (
      std::type_index (typeid (TEvent)));
    if (found == _state->handlers.end ()) {
      return;
    }
    for (const auto &handler : found->second) {
      handler (&event);
    }
  }

  std::shared_ptr<monitoring_runtime_state_t> _state;
};

} // namespace zlink::framework::detail
