/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/locations/diagnostics.hpp>
#include <zlink/framework/contracts/spots/spot.hpp>
#include <zlink/framework/contracts/streams/stream.hpp>
#include <zlink/framework/contracts/timers/timer.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <vector>

namespace zlink::framework
{

namespace detail
{
class app_state_t;
class monitoring_runtime_state_t;
class monitoring_runtime_t;
} // namespace detail

enum class runtime_event_severity_t
{
    trace = 0,
    info = 1,
    warning = 2,
    error = 3
};

enum class health_status_t
{
    healthy = 0,
    degraded = 1,
    unhealthy = 2
};

enum class socket_event_kind_t
{
    connected = 0,
    connection_ready = 1,
    disconnected = 2,
    handshake_failed = 3,
    peer_admission_changed = 4,
    closed = 5
};

enum class location_event_kind_t
{
    status_changed = 0,
    topology_changed = 1,
    service_summary_changed = 2
};

enum class spot_event_kind_t
{
    timer_handler_failed = 0,
    timer_stopped_after_unhandled_exception = 1
};

enum class stream_event_kind_t
{
    connected = 0,
    disconnected = 1,
    transport_error = 2,
    handler_exception = 3
};

enum class actor_event_kind_t
{
    bound = 0,
    unbound = 1,
    relay_failed = 2,
    session_disconnected = 3
};

struct runtime_event_base_t
{
    std::string source_name;
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now ();
    runtime_event_severity_t severity = runtime_event_severity_t::info;
    std::string node_name;
    std::string correlation_id;
    health_status_t health = health_status_t::healthy;
};

/* Runtime metric catalog fields (runtime-metrics §3, cpp-monitoring §8).
 * counter/updown updates carry `delta`, observable gauges `current`, and
 * histogram records `sample`; an OTel bridge maps instruments from these
 * fields instead of re-hardcoding the catalog per name. */
enum class metric_instrument_kind_t
{
    counter,
    updown,
    observable,
    histogram
};

enum class metric_temporality_t
{
    delta,
    current,
    sample
};

/* Drain lifecycle observation (graceful-drain-handoff §9): no source
 * registration — any registered handler receives transitions, and the
 * source name is the fixed value "drain". */
enum class drain_state_t
{
    serving,
    draining,
    drained,
    force_stopping
};

struct drain_event_t : runtime_event_base_t
{
    drain_state_t state = drain_state_t::serving;
};

struct metric_event_payload_t : runtime_event_base_t
{
    std::string name;
    double value = 0;
    std::string unit;
    metric_instrument_kind_t instrument_kind = metric_instrument_kind_t::counter;
    metric_temporality_t temporality = metric_temporality_t::delta;
    std::map<std::string, std::string> tags;
};

struct socket_event_payload_t : runtime_event_base_t
{
    socket_event_kind_t event = socket_event_kind_t::connected;
    std::string local_address;
    std::string remote_address;
};

struct location_event_payload_t : runtime_event_base_t
{
    location_event_kind_t event = location_event_kind_t::status_changed;
    std::optional<location_runtime_status_t> status;
    std::vector<location_topology_entry_t> topology;
    std::vector<location_service_summary_t> service_summary;
};

struct spot_timer_diagnostic_t
{
    spot_id_t spot_id;
    bool entry_spot = false;
    std::string timer_name;
    std::string handler_type;
    std::uint64_t delivery_index = 0;
    std::uint64_t scheduled_index = 0;
    std::string exception_type;
    std::string exception_message;
};

struct spot_event_payload_t : runtime_event_base_t
{
    spot_event_kind_t event = spot_event_kind_t::timer_handler_failed;
    std::optional<spot_timer_diagnostic_t> timer_diagnostic;
};

struct stream_event_payload_t : runtime_event_base_t
{
    stream_event_kind_t event = stream_event_kind_t::connected;
    std::string stream_name;
    std::string session_id;
    std::string message;
};

struct actor_event_payload_t : runtime_event_base_t
{
    actor_event_kind_t event = actor_event_kind_t::bound;
    std::string actor_type;
    std::string actor_id;
    std::string session_id;
    std::string message;
};

class monitoring_builder_t
{
  public:
    monitoring_builder_t ();
    ~monitoring_builder_t ();

    monitoring_builder_t (monitoring_builder_t &&) noexcept;
    monitoring_builder_t &operator= (monitoring_builder_t &&) noexcept;
    monitoring_builder_t (const monitoring_builder_t &) = delete;
    monitoring_builder_t &operator= (const monitoring_builder_t &) = delete;

    monitoring_builder_t &add_socket_events (std::string source_name);
    monitoring_builder_t &add_socket_events (std::string source_name,
                                             std::initializer_list<socket_event_kind_t> events);
    monitoring_builder_t &add_location_events (std::string source_name,
                                               std::chrono::milliseconds interval);
    monitoring_builder_t &add_stream_events (std::string source_name);
    monitoring_builder_t &add_actor_events (std::string source_name);
    monitoring_builder_t &on_trace (std::function<void (const runtime_event_base_t &)> hook);
    template <typename TEvent>
    monitoring_builder_t &on (std::function<void (const TEvent &)> handler)
    {
        return on_erased (std::type_index (typeid (TEvent)),
                          [handler = std::move (handler)] (const void *event) {
                              handler (*static_cast<const TEvent *> (event));
                          });
    }

  private:
    friend class app_t;
    friend class metrics_builder_t;
    friend class detail::monitoring_runtime_t;

    explicit monitoring_builder_t (std::shared_ptr<detail::monitoring_runtime_state_t> state);
    monitoring_builder_t &on_erased (std::type_index event_type,
                                     std::function<void (const void *)> handler);

    std::shared_ptr<detail::monitoring_runtime_state_t> _state;
};

class metrics_builder_t
{
  public:
    metrics_builder_t ();
    ~metrics_builder_t ();

    metrics_builder_t (metrics_builder_t &&) noexcept;
    metrics_builder_t &operator= (metrics_builder_t &&) noexcept;
    metrics_builder_t (const metrics_builder_t &) = delete;
    metrics_builder_t &operator= (const metrics_builder_t &) = delete;

    metrics_builder_t &add_runtime_metrics ();
    bool runtime_metrics_enabled () const noexcept;
    metrics_builder_t &record_runtime_metric (std::string name,
                                              double value,
                                              std::map<std::string, std::string> tags = {});

  private:
    friend class app_t;
    friend class detail::app_state_t;

    explicit metrics_builder_t (const monitoring_builder_t &monitoring);

    std::shared_ptr<detail::monitoring_runtime_state_t> _state;
};

} // namespace zlink::framework
