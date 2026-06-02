/* SPDX-License-Identifier: MPL-2.0 */

#include "monitoring_runtime.hpp"

#include <utility>

namespace zlink::framework
{

runtime_event_publisher_t::runtime_event_publisher_t () = default;
runtime_event_publisher_t::runtime_event_publisher_t (
  std::shared_ptr<detail::monitoring_runtime_state_t> state)
  : _state (std::move (state))
{
}

runtime_event_publisher_t::~runtime_event_publisher_t () = default;
runtime_event_publisher_t::runtime_event_publisher_t (
  runtime_event_publisher_t &&) noexcept = default;
runtime_event_publisher_t &runtime_event_publisher_t::operator= (
  runtime_event_publisher_t &&) noexcept = default;

void
runtime_event_publisher_t::publish_erased (
  std::type_index event_type,
  const runtime_event_base_t &base,
  const void *event) const
{
  if (!_state) {
    return;
  }
  if (_state->tracing_hook) {
    _state->tracing_hook (base);
  }
  const auto found = _state->handlers.find (event_type);
  if (found == _state->handlers.end ()) {
    return;
  }
  for (const auto &handler : found->second) {
    handler (event);
  }
}

monitoring_builder_t::monitoring_builder_t ()
  : _state (std::make_shared<detail::monitoring_runtime_state_t> ())
{
}

monitoring_builder_t::monitoring_builder_t (
  std::shared_ptr<detail::monitoring_runtime_state_t> state)
  : _state (std::move (state))
{
}

monitoring_builder_t::~monitoring_builder_t () = default;
monitoring_builder_t::monitoring_builder_t (monitoring_builder_t &&) noexcept =
  default;
monitoring_builder_t &monitoring_builder_t::operator= (
  monitoring_builder_t &&) noexcept = default;

monitoring_builder_t &
monitoring_builder_t::add_socket_events (std::string source_name)
{
  _state->socket_sources.push_back (std::move (source_name));
  return *this;
}

monitoring_builder_t &
monitoring_builder_t::add_discovery_events (std::string source_name)
{
  _state->discovery_sources.push_back (std::move (source_name));
  return *this;
}

monitoring_builder_t &
monitoring_builder_t::add_registry_events (
  std::string source_name,
  std::chrono::milliseconds interval)
{
  _state->registry_sources.push_back (
    detail::monitoring_source_registration_t {
      std::move (source_name), interval });
  return *this;
}

monitoring_builder_t &
monitoring_builder_t::add_spot_events (std::string source_name,
                                       std::chrono::milliseconds interval)
{
  _state->spot_sources.push_back (
    detail::monitoring_source_registration_t {
      std::move (source_name), interval });
  return *this;
}

monitoring_builder_t &
monitoring_builder_t::add_spot_timer_events (std::string source_name)
{
  _state->spot_timer_sources.push_back (std::move (source_name));
  return *this;
}

monitoring_builder_t &
monitoring_builder_t::add_stream_events (std::string source_name)
{
  _state->stream_sources.push_back (std::move (source_name));
  return *this;
}

monitoring_builder_t &
monitoring_builder_t::add_actor_events (std::string source_name)
{
  _state->actor_sources.push_back (std::move (source_name));
  return *this;
}

monitoring_builder_t &
monitoring_builder_t::on_trace (
  std::function<void (const runtime_event_base_t &)> hook)
{
  _state->tracing_hook = std::move (hook);
  return *this;
}

runtime_event_publisher_t
monitoring_builder_t::publisher () const
{
  return runtime_event_publisher_t (_state);
}

monitoring_builder_t &
monitoring_builder_t::on_erased (
  std::type_index event_type,
  std::function<void (const void *)> handler)
{
  _state->handlers[event_type].push_back (std::move (handler));
  return *this;
}

} // namespace zlink::framework

namespace zlink::framework::detail
{

monitoring_runtime_t::monitoring_runtime_t (
  std::shared_ptr<monitoring_runtime_state_t> state)
  : _state (std::move (state))
{
}

monitoring_runtime_t
monitoring_runtime_t::from (const monitoring_builder_t &builder)
{
  return monitoring_runtime_t (builder._state);
}

void
monitoring_runtime_t::publish_socket (socket_event_payload_t event) const
{
  publish (std::move (event));
}

void
monitoring_runtime_t::publish_discovery (discovery_event_payload_t event) const
{
  publish (std::move (event));
}

void
monitoring_runtime_t::publish_registry_snapshot (
  std::string source_name,
  registry_status_t status,
  std::vector<topology_entry_t> topology,
  std::vector<service_summary_entry_t> summary) const
{
  auto event_kind = registry_event_kind_t::status_changed;
  if (!topology.empty ()) {
    event_kind = registry_event_kind_t::topology_changed;
  } else if (!summary.empty ()) {
    event_kind = registry_event_kind_t::service_summary_changed;
  }
  publish (registry_event_payload_t {
    runtime_event_base_t { std::move (source_name) },
    event_kind,
    std::move (status),
    std::move (topology),
    std::move (summary) });
}

void
monitoring_runtime_t::publish_spot_snapshot (
  spot_event_payload_t event) const
{
  publish (std::move (event));
}

void
monitoring_runtime_t::publish_stream (stream_event_payload_t event) const
{
  publish (std::move (event));
}

void
monitoring_runtime_t::publish_actor (actor_event_payload_t event) const
{
  publish (std::move (event));
}

void
monitoring_runtime_t::publish_timer_failure (
  std::string source_name,
  spot_rid_t spot_rid,
  timer_failure_event_t failure) const
{
  auto event_kind = failure.stopped
                      ? spot_event_kind_t::timer_stopped_after_unhandled_exception
                      : spot_event_kind_t::timer_handler_failed;
  publish (spot_event_payload_t {
    runtime_event_base_t {
      std::move (source_name),
      std::chrono::system_clock::now (),
      runtime_event_severity_t::error,
      {},
      {},
      health_status_t::degraded },
    event_kind,
    {},
    {},
    {},
    spot_timer_diagnostic_t {
      std::move (spot_rid),
      false,
      std::move (failure.timer_name),
      failure.handler_type.name (),
      failure.delivery_index,
      failure.delivery_index,
      "std::exception",
      std::move (failure.message) } });
}

} // namespace zlink::framework::detail
