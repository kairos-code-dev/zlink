/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "monitoring_runtime.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <utility>

namespace
{

bool is_blank (const std::string &value)
{
    return value.empty () || std::all_of (value.begin (), value.end (), [] (char ch) {
               return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
           });
}

void validate_source_name (const std::string &source_name, const char *kind)
{
    if (is_blank (source_name)) {
        throw zlink::framework::framework_exception_t (
          zlink::framework::framework_error_kind_t::request_protocol_error,
          std::string ("monitoring ") + kind + " source name must not be empty");
    }
}

template <typename TSource, typename TName>
bool contains_source (const std::vector<TSource> &sources, const TName &source_name)
{
    return std::any_of (sources.begin (), sources.end (),
                        [&] (const TSource &source) { return source.source_name == source_name; });
}

bool contains_source (const std::vector<std::string> &sources, const std::string &source_name)
{
    return std::find (sources.begin (), sources.end (), source_name) != sources.end ();
}

template <typename TSource>
void ensure_unique_source (const std::vector<TSource> &sources,
                           const std::string &source_name,
                           const char *kind)
{
    if (contains_source (sources, source_name)) {
        throw zlink::framework::framework_exception_t (
          zlink::framework::framework_error_kind_t::request_protocol_error,
          std::string ("duplicate monitoring ") + kind + " source");
    }
}

void validate_polling_interval (std::chrono::milliseconds interval, const char *kind)
{
    if (interval <= std::chrono::milliseconds::zero ()) {
        throw zlink::framework::framework_exception_t (
          zlink::framework::framework_error_kind_t::request_protocol_error,
          std::string ("monitoring ") + kind + " interval must be greater than zero");
    }
}

} // namespace

namespace zlink::framework::detail
{
void monitoring_runtime_t::publish_erased (std::type_index event_type,
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
        try {
            handler (event);
        }
        catch (const std::exception &ex) {
            std::cerr << "monitoring-event-dispatch: " << ex.what () << '\n';
        }
        catch (...) {
            std::cerr << "monitoring-event-dispatch: unknown monitoring handler failure\n";
        }
    }
}

} // namespace zlink::framework::detail

namespace zlink::framework
{

monitoring_builder_t::monitoring_builder_t () :
    _state (std::make_shared<detail::monitoring_runtime_state_t> ())
{
}

monitoring_builder_t::monitoring_builder_t (
  std::shared_ptr<detail::monitoring_runtime_state_t> state) :
    _state (std::move (state))
{
}

monitoring_builder_t::~monitoring_builder_t () = default;
monitoring_builder_t::monitoring_builder_t (monitoring_builder_t &&) noexcept = default;
monitoring_builder_t &monitoring_builder_t::operator= (monitoring_builder_t &&) noexcept = default;

monitoring_builder_t &monitoring_builder_t::add_socket_events (std::string source_name)
{
    return add_socket_events (std::move (source_name), {});
}

monitoring_builder_t &
monitoring_builder_t::add_socket_events (std::string source_name,
                                         std::initializer_list<socket_event_kind_t> events)
{
    validate_source_name (source_name, "socket");
    ensure_unique_source (_state->socket_sources, source_name, "socket");
    _state->socket_sources.push_back (detail::socket_monitoring_source_registration_t{
      std::move (source_name), std::vector<socket_event_kind_t> (events.begin (), events.end ())});
    return *this;
}

monitoring_builder_t &monitoring_builder_t::add_location_events (std::string source_name,
                                                                 std::chrono::milliseconds interval)
{
    validate_source_name (source_name, "location");
    validate_polling_interval (interval, "location");
    ensure_unique_source (_state->location_sources, source_name, "location");
    _state->location_sources.push_back (
      detail::monitoring_source_registration_t{std::move (source_name), interval});
    return *this;
}

monitoring_builder_t &monitoring_builder_t::add_stream_events (std::string source_name)
{
    validate_source_name (source_name, "stream");
    ensure_unique_source (_state->stream_sources, source_name, "stream");
    _state->stream_sources.push_back (std::move (source_name));
    return *this;
}

monitoring_builder_t &monitoring_builder_t::add_actor_events (std::string source_name)
{
    validate_source_name (source_name, "actor");
    ensure_unique_source (_state->actor_sources, source_name, "actor");
    _state->actor_sources.push_back (std::move (source_name));
    return *this;
}

monitoring_builder_t &
monitoring_builder_t::on_trace (std::function<void (const runtime_event_base_t &)> hook)
{
    _state->tracing_hook = std::move (hook);
    return *this;
}

monitoring_builder_t &monitoring_builder_t::on_erased (std::type_index event_type,
                                                       std::function<void (const void *)> handler)
{
    _state->handlers[event_type].push_back (std::move (handler));
    return *this;
}

metrics_builder_t::metrics_builder_t () :
    _state (std::make_shared<detail::monitoring_runtime_state_t> ())
{
}

metrics_builder_t::metrics_builder_t (const monitoring_builder_t &monitoring) :
    _state (monitoring._state)
{
}

metrics_builder_t::~metrics_builder_t () = default;

metrics_builder_t::metrics_builder_t (metrics_builder_t &&) noexcept = default;

metrics_builder_t &metrics_builder_t::operator= (metrics_builder_t &&) noexcept = default;

metrics_builder_t &metrics_builder_t::add_runtime_metrics ()
{
    _state->runtime_metrics_enabled = true;
    return *this;
}

bool metrics_builder_t::runtime_metrics_enabled () const noexcept
{
    return _state && _state->runtime_metrics_enabled;
}

metrics_builder_t &metrics_builder_t::record_runtime_metric (
  std::string name, double value, std::map<std::string, std::string> tags)
{
    if (runtime_metrics_enabled ()) {
        detail::monitoring_runtime_t (_state).publish_metric (metric_event_payload_t{
          runtime_event_base_t{"runtime.metrics"}, std::move (name), value, std::string{},
          metric_instrument_kind_t::counter, metric_temporality_t::delta, std::move (tags)});
    }
    return *this;
}

} // namespace zlink::framework

namespace zlink::framework::detail
{

monitoring_runtime_t::monitoring_runtime_t (std::shared_ptr<monitoring_runtime_state_t> state) :
    _state (std::move (state))
{
}

monitoring_runtime_t monitoring_runtime_t::from (const monitoring_builder_t &builder)
{
    return monitoring_runtime_t (builder._state);
}

void monitoring_runtime_t::publish_metric (metric_event_payload_t event) const
{
    publish (std::move (event));
}

void monitoring_runtime_t::publish_drain (drain_event_t event) const
{
    publish (std::move (event));
}

void monitoring_runtime_t::publish_socket (socket_event_payload_t event) const
{
    const auto found = std::find_if (_state->socket_sources.begin (), _state->socket_sources.end (),
                                     [&] (const socket_monitoring_source_registration_t &source) {
                                         return source.source_name == event.source_name;
                                     });
    if (found == _state->socket_sources.end ()) {
        return;
    }
    if (!found->events.empty ()
        && std::find (found->events.begin (), found->events.end (), event.event)
             == found->events.end ()) {
        return;
    }
    publish (std::move (event));
}

void monitoring_runtime_t::publish_location_snapshot (
  std::string source_name,
  location_runtime_status_t status,
  std::vector<location_topology_entry_t> topology,
  std::vector<location_service_summary_t> summary) const
{
    publish_location_changes (
      std::move (source_name), std::move (status), true,
      topology.empty () ? std::nullopt
                        : std::optional<std::vector<location_topology_entry_t>> (
                            std::move (topology)),
      summary.empty () ? std::nullopt
                       : std::optional<std::vector<location_service_summary_t>> (
                           std::move (summary)));
}

void monitoring_runtime_t::publish_location_changes (
  std::string source_name,
  location_runtime_status_t status,
  bool status_changed,
  std::optional<std::vector<location_topology_entry_t>> topology,
  std::optional<std::vector<location_service_summary_t>> summary) const
{
    if (!contains_source (_state->location_sources, source_name)) {
        return;
    }
    if (status_changed) {
        publish (location_event_payload_t{runtime_event_base_t{source_name},
                                          location_event_kind_t::status_changed,
                                          status,
                                          {},
                                          {}});
    }
    if (topology) {
        publish (location_event_payload_t{runtime_event_base_t{source_name},
                                          location_event_kind_t::topology_changed,
                                          status,
                                          std::move (*topology),
                                          {}});
    }
    if (summary) {
        publish (location_event_payload_t{runtime_event_base_t{std::move (source_name)},
                                          location_event_kind_t::service_summary_changed,
                                          std::move (status),
                                          {},
                                          std::move (*summary)});
    }
}

void monitoring_runtime_t::publish_stream (stream_event_payload_t event) const
{
    if (!contains_source (_state->stream_sources, event.source_name)) {
        return;
    }
    publish (std::move (event));
}

void monitoring_runtime_t::publish_actor (actor_event_payload_t event) const
{
    if (!contains_source (_state->actor_sources, event.source_name)) {
        return;
    }
    publish (std::move (event));
}

void monitoring_runtime_t::publish_timer_failure (std::string source_name,
                                                  spot_id_t spot_id,
                                                  timer_failure_event_t failure) const
{
    auto event_kind = failure.stopped ? spot_event_kind_t::timer_stopped_after_unhandled_exception
                                      : spot_event_kind_t::timer_handler_failed;
    publish (spot_event_payload_t{
      runtime_event_base_t{source_name,
                           std::chrono::system_clock::now (),
                           runtime_event_severity_t::error,
                           {},
                           {},
                           health_status_t::degraded},
      event_kind,
      spot_timer_diagnostic_t{std::move (spot_id), false, std::move (failure.timer_name),
                              failure.handler_type.name (), failure.delivery_index,
                              failure.delivery_index, "std::exception",
                              std::move (failure.message)}});
}

} // namespace zlink::framework::detail
