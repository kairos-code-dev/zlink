/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/registry/registry.hpp>
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
    trace,
    info,
    warning,
    error
};

enum class health_status_t
{
    healthy,
    degraded,
    unhealthy
};

enum class socket_event_kind_t
{
    connected,
    connection_ready,
    disconnected,
    handshake_failed,
    peer_admission_changed,
    closed,
    internal
};

enum class discovery_event_kind_t
{
    connected,
    disconnected,
    topology_changed,
    error
};

enum class registry_event_kind_t
{
    status_changed,
    topology_changed,
    service_summary_changed
};

enum class spot_event_kind_t
{
    status_changed,
    peers_changed,
    subjects_changed,
    timer_handler_failed,
    timer_stopped_after_unhandled_exception
};

enum class stream_event_kind_t
{
    connected,
    disconnected,
    transport_error,
    handler_exception
};

enum class actor_event_kind_t
{
    bound,
    unbound,
    relay_failed,
    session_disconnected
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

class runtime_event_publisher_t
{
  public:
    runtime_event_publisher_t ();
    ~runtime_event_publisher_t ();

    runtime_event_publisher_t (runtime_event_publisher_t &&) noexcept;
    runtime_event_publisher_t &operator= (runtime_event_publisher_t &&) noexcept;
    runtime_event_publisher_t (const runtime_event_publisher_t &) = default;
    runtime_event_publisher_t &operator= (const runtime_event_publisher_t &) = default;

    template <typename TEvent> void publish (TEvent event) const
    {
        event.timestamp = std::chrono::system_clock::now ();
        publish_erased (std::type_index (typeid (TEvent)), event, &event);
    }

  private:
    friend class monitoring_builder_t;
    friend class metrics_builder_t;
    friend class detail::monitoring_runtime_t;

    explicit runtime_event_publisher_t (std::shared_ptr<detail::monitoring_runtime_state_t> state);
    void publish_erased (std::type_index event_type, const runtime_event_base_t &base, const void *event) const;

    std::shared_ptr<detail::monitoring_runtime_state_t> _state;
};

struct metric_event_payload_t : runtime_event_base_t
{
    std::string name;
    double value = 0;
    std::map<std::string, std::string> tags;
};

struct socket_event_payload_t : runtime_event_base_t
{
    socket_event_kind_t event = socket_event_kind_t::internal;
    std::string local_address;
    std::string remote_address;
    std::uint32_t native_event = 0;
    std::uint32_t native_value = 0;
};

struct discovery_event_payload_t : runtime_event_base_t
{
    discovery_event_kind_t event = discovery_event_kind_t::connected;
    std::string endpoint;
    std::string message;
};

struct registry_event_payload_t : runtime_event_base_t
{
    registry_event_kind_t event = registry_event_kind_t::status_changed;
    std::optional<registry_status_t> status;
    std::vector<topology_entry_t> topology;
    std::vector<service_summary_entry_t> service_summary;
};

struct spot_timer_diagnostic_t
{
    spot_rid_t spot_rid;
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
    spot_event_kind_t event = spot_event_kind_t::status_changed;
    std::string spot_node_name;
    std::vector<std::string> peers;
    std::vector<std::string> subjects;
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
    monitoring_builder_t &add_discovery_events (std::string source_name);
    monitoring_builder_t &add_registry_events (std::string source_name, std::chrono::milliseconds interval);
    monitoring_builder_t &add_spot_events (std::string source_name, std::chrono::milliseconds interval);
    monitoring_builder_t &add_spot_timer_events (std::string source_name);
    monitoring_builder_t &add_stream_events (std::string source_name);
    monitoring_builder_t &add_actor_events (std::string source_name);
    monitoring_builder_t &on_trace (std::function<void (const runtime_event_base_t &)> hook);
    runtime_event_publisher_t publisher () const;

    template <typename TEvent> monitoring_builder_t &on (std::function<void (const TEvent &)> handler)
    {
        return on_erased (std::type_index (typeid (TEvent)), [handler = std::move (handler)] (const void *event) {
            handler (*static_cast<const TEvent *> (event));
        });
    }

  private:
    friend class app_t;
    friend class metrics_builder_t;
    friend class detail::monitoring_runtime_t;

    explicit monitoring_builder_t (std::shared_ptr<detail::monitoring_runtime_state_t> state);
    monitoring_builder_t &on_erased (std::type_index event_type, std::function<void (const void *)> handler);

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
    metrics_builder_t &
    record_runtime_metric (std::string name, double value, std::map<std::string, std::string> tags = {});

  private:
    friend class app_t;
    friend class detail::app_state_t;

    explicit metrics_builder_t (const monitoring_builder_t &monitoring);

    std::shared_ptr<detail::monitoring_runtime_state_t> _state;
};

} // namespace zlink::framework
