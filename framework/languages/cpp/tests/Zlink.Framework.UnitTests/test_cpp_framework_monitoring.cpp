/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include "runtime/diagnostics/monitoring_runtime.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <typeindex>

namespace
{

struct timer_handler_t
{
};

} // namespace

int
main ()
{
  using namespace std::chrono_literals;

  zlink::framework::app_t app = zlink::framework::app_t::create ();

  int socket_events = 0;
  int discovery_events = 0;
  int registry_events = 0;
  int spot_events = 0;
  int stream_events = 0;
  int actor_events = 0;
  int metric_events = 0;
  int trace_events = 0;
  int filtered_socket_events = 0;
  int ignored_events = 0;
  bool timer_failure_is_summary = false;
  bool stream_transport_distinct = false;
  bool stream_handler_distinct = false;
  bool publisher_seen = false;

  app.monitoring ()
    .add_socket_events ("profile.server")
    .add_socket_events (
      "filtered.server",
      { zlink::framework::socket_event_kind_t::connection_ready })
    .add_discovery_events ("profile.discovery")
    .add_registry_events ("registry", 1s)
    .add_spot_events ("stage-node", 1s)
    .add_spot_timer_events ("spot-timer")
    .add_stream_events ("game.stream")
    .add_actor_events ("game.actor")
    .on_trace ([&](const zlink::framework::runtime_event_base_t &event) {
      if (!event.source_name.empty ()) {
        ++trace_events;
      }
    })
    .on<zlink::framework::socket_event_payload_t> (
      [&](const zlink::framework::socket_event_payload_t &event) {
        if (event.source_name == "profile.server" &&
            event.event == zlink::framework::socket_event_kind_t::connected) {
          ++socket_events;
        }
        if (event.source_name == "filtered.server") {
          ++filtered_socket_events;
        }
        if (event.source_name == "unregistered.server") {
          ++ignored_events;
        }
      })
    .on<zlink::framework::discovery_event_payload_t> (
      [&](const zlink::framework::discovery_event_payload_t &event) {
        if (event.source_name == "profile.discovery" &&
            event.endpoint == "tcp://registry:5551") {
          ++discovery_events;
        }
        if (event.source_name == "ignored.discovery") {
          ++ignored_events;
        }
      })
    .on<zlink::framework::registry_event_payload_t> (
      [&](const zlink::framework::registry_event_payload_t &event) {
        if (event.source_name == "registry" &&
            event.event ==
              zlink::framework::registry_event_kind_t::topology_changed &&
            !event.topology.empty ()) {
          ++registry_events;
        }
        if (event.source_name == "ignored.registry") {
          ++ignored_events;
        }
      })
    .on<zlink::framework::spot_event_payload_t> (
      [&](const zlink::framework::spot_event_payload_t &event) {
        if (event.source_name == "spot-timer" &&
            event.event ==
              zlink::framework::spot_event_kind_t::timer_stopped_after_unhandled_exception &&
            event.timer_diagnostic &&
            event.timer_diagnostic->exception_message == "boom") {
          timer_failure_is_summary = true;
        }
        if (event.source_name == "stage-node" &&
            event.event == zlink::framework::spot_event_kind_t::subjects_changed &&
            !event.subjects.empty ()) {
          ++spot_events;
        }
        if (event.source_name == "ignored.spot" ||
            event.source_name == "ignored.timer") {
          ++ignored_events;
        }
      })
    .on<zlink::framework::stream_event_payload_t> (
      [&](const zlink::framework::stream_event_payload_t &event) {
        if (event.event ==
            zlink::framework::stream_event_kind_t::transport_error) {
          stream_transport_distinct = event.message == "connection reset";
        }
        if (event.event ==
            zlink::framework::stream_event_kind_t::handler_exception) {
          stream_handler_distinct = event.message == "handler failed";
        }
        if (event.source_name == "ignored.stream") {
          ++ignored_events;
        }
        ++stream_events;
      })
    .on<zlink::framework::actor_event_payload_t> (
      [&](const zlink::framework::actor_event_payload_t &event) {
        if (event.actor_id == "alice" &&
            event.event == zlink::framework::actor_event_kind_t::bound) {
          ++actor_events;
        }
        if (event.actor_id == "bob" &&
            event.event == zlink::framework::actor_event_kind_t::unbound) {
          publisher_seen = true;
        }
        if (event.source_name == "ignored.actor") {
          ++ignored_events;
        }
      })
    .on<zlink::framework::metric_event_payload_t> (
      [&](const zlink::framework::metric_event_payload_t &event) {
        const auto surface = event.tags.find ("surface");
        if (event.source_name == "runtime.metrics" &&
            event.name == "active_http_requests" &&
            event.value == 3 &&
            surface != event.tags.end () &&
            surface->second == "http") {
          ++metric_events;
        }
      });

  const auto runtime =
    zlink::framework::detail::monitoring_runtime_t::from (app.monitoring ());

  runtime.publish_socket (zlink::framework::socket_event_payload_t {
    zlink::framework::runtime_event_base_t { "profile.server" },
    zlink::framework::socket_event_kind_t::connected,
    "tcp://127.0.0.1:7001",
    "tcp://127.0.0.1:7002",
    1,
    0 });
  runtime.publish_socket (zlink::framework::socket_event_payload_t {
    zlink::framework::runtime_event_base_t { "filtered.server" },
    zlink::framework::socket_event_kind_t::disconnected,
    "tcp://127.0.0.1:7001",
    "tcp://127.0.0.1:7002",
    1,
    0 });
  runtime.publish_socket (zlink::framework::socket_event_payload_t {
    zlink::framework::runtime_event_base_t { "filtered.server" },
    zlink::framework::socket_event_kind_t::connection_ready,
    "tcp://127.0.0.1:7001",
    "tcp://127.0.0.1:7002",
    1,
    0 });
  runtime.publish_socket (zlink::framework::socket_event_payload_t {
    zlink::framework::runtime_event_base_t { "unregistered.server" },
    zlink::framework::socket_event_kind_t::connected,
    "tcp://127.0.0.1:7001",
    "tcp://127.0.0.1:7002",
    1,
    0 });
  runtime.publish_discovery (zlink::framework::discovery_event_payload_t {
    zlink::framework::runtime_event_base_t { "profile.discovery" },
    zlink::framework::discovery_event_kind_t::connected,
    "tcp://registry:5551",
    "connected" });
  runtime.publish_discovery (zlink::framework::discovery_event_payload_t {
    zlink::framework::runtime_event_base_t { "ignored.discovery" },
    zlink::framework::discovery_event_kind_t::connected,
    "tcp://registry:5551",
    "connected" });
  runtime.publish_registry_snapshot (
    "registry",
    zlink::framework::registry_status_t {
      zlink::framework::registry_state_t::running,
      "registry",
      "tcp://0.0.0.0:5550",
      "tcp://0.0.0.0:5551",
      0 },
    { zlink::framework::topology_entry_t {
      "node",
      zlink::framework::service_kind_t::spot,
      zlink::framework::service_role_t::spot_node,
      "stage-node",
      zlink::framework::topology_source_t::embedded,
      zlink::framework::topology_state_t::active } },
    {});
  runtime.publish_registry_snapshot (
    "ignored.registry",
    zlink::framework::registry_status_t {
      zlink::framework::registry_state_t::running,
      "registry",
      "tcp://0.0.0.0:5550",
      "tcp://0.0.0.0:5551",
      0 },
    { zlink::framework::topology_entry_t {
      "node",
      zlink::framework::service_kind_t::spot,
      zlink::framework::service_role_t::spot_node,
      "stage-node",
      zlink::framework::topology_source_t::embedded,
      zlink::framework::topology_state_t::active } },
    {});
  runtime.publish_spot_snapshot (zlink::framework::spot_event_payload_t {
    zlink::framework::runtime_event_base_t { "stage-node" },
    zlink::framework::spot_event_kind_t::subjects_changed,
    "stage-node",
    {},
    { "stage.updated" },
    std::nullopt });
  runtime.publish_spot_snapshot (zlink::framework::spot_event_payload_t {
    zlink::framework::runtime_event_base_t { "ignored.spot" },
    zlink::framework::spot_event_kind_t::subjects_changed,
    "stage-node",
    {},
    { "stage.updated" },
    std::nullopt });
  runtime.publish_stream (zlink::framework::stream_event_payload_t {
    zlink::framework::runtime_event_base_t {
      "game.stream",
      std::chrono::system_clock::now (),
      zlink::framework::runtime_event_severity_t::error },
    zlink::framework::stream_event_kind_t::transport_error,
    "game.stream",
    "session-1",
    "connection reset" });
  runtime.publish_stream (zlink::framework::stream_event_payload_t {
    zlink::framework::runtime_event_base_t {
      "game.stream",
      std::chrono::system_clock::now (),
      zlink::framework::runtime_event_severity_t::error },
    zlink::framework::stream_event_kind_t::handler_exception,
    "game.stream",
    "session-1",
    "handler failed" });
  runtime.publish_stream (zlink::framework::stream_event_payload_t {
    zlink::framework::runtime_event_base_t { "ignored.stream" },
    zlink::framework::stream_event_kind_t::transport_error,
    "ignored.stream",
    "session-1",
    "ignored" });
  runtime.publish_actor (zlink::framework::actor_event_payload_t {
    zlink::framework::runtime_event_base_t { "game.actor" },
    zlink::framework::actor_event_kind_t::bound,
    "player",
    "alice",
    "session-1",
    {} });
  runtime.publish_actor (zlink::framework::actor_event_payload_t {
    zlink::framework::runtime_event_base_t { "ignored.actor" },
    zlink::framework::actor_event_kind_t::bound,
    "player",
    "alice",
    "session-1",
    {} });
  app.monitoring ().publisher ().publish (
    zlink::framework::actor_event_payload_t {
      zlink::framework::runtime_event_base_t { "game.actor" },
      zlink::framework::actor_event_kind_t::unbound,
      "player",
      "bob",
      "session-2",
      "closed" });
  runtime.publish_timer_failure (
    "spot-timer",
    zlink::framework::spot_rid_t::from_string ("stage-rid"),
    zlink::framework::timer_failure_event_t {
      "heartbeat",
      std::type_index (typeid (timer_handler_t)),
      7,
      true,
      "boom" });
  runtime.publish_timer_failure (
    "ignored.timer",
    zlink::framework::spot_rid_t::from_string ("stage-rid"),
    zlink::framework::timer_failure_event_t {
      "heartbeat",
      std::type_index (typeid (timer_handler_t)),
      7,
      true,
      "boom" });
  app.metrics ()
    .add_runtime_metrics ()
    .record_runtime_metric (
      "active_http_requests",
      3,
      { { "surface", "http" } });

  if (socket_events != 1 || filtered_socket_events != 1 ||
      discovery_events != 1 || registry_events != 1 ||
      spot_events != 1 || stream_events != 2 || actor_events != 1 ||
      metric_events != 1) {
    return 1;
  }
  if (ignored_events != 0) {
    return 9;
  }
  if (!publisher_seen) {
    return 5;
  }
  if (!timer_failure_is_summary) {
    return 2;
  }
  if (!stream_transport_distinct || !stream_handler_distinct) {
    return 3;
  }
  if (trace_events < 9) {
    return 4;
  }

  bool duplicate_source_failed = false;
  try {
    app.monitoring ().add_socket_events ("profile.server");
  } catch (const zlink::framework::framework_exception_t &error) {
    duplicate_source_failed =
      error.kind () ==
      zlink::framework::framework_error_kind_t::request_protocol_error;
  }
  if (!duplicate_source_failed) {
    return 10;
  }

  bool empty_source_failed = false;
  try {
    zlink::framework::app_t::create ().monitoring ().add_socket_events (" ");
  } catch (const zlink::framework::framework_exception_t &error) {
    empty_source_failed =
      error.kind () ==
      zlink::framework::framework_error_kind_t::request_protocol_error;
  }
  if (!empty_source_failed) {
    return 11;
  }

  bool invalid_registry_interval_failed = false;
  try {
    zlink::framework::app_t::create ().monitoring ().add_registry_events (
      "registry", 0ms);
  } catch (const zlink::framework::framework_exception_t &error) {
    invalid_registry_interval_failed =
      error.kind () ==
      zlink::framework::framework_error_kind_t::request_protocol_error;
  }
  if (!invalid_registry_interval_failed) {
    return 12;
  }

  bool duplicate_spot_source_failed = false;
  try {
    auto invalid_app = zlink::framework::app_t::create ();
    invalid_app.monitoring ()
      .add_spot_events ("stage-node", 1s)
      .add_spot_events ("stage-node", 1s);
  } catch (const zlink::framework::framework_exception_t &error) {
    duplicate_spot_source_failed =
      error.kind () ==
      zlink::framework::framework_error_kind_t::request_protocol_error;
  }
  if (!duplicate_spot_source_failed) {
    return 13;
  }

  app.health ()
    .add_zlink_runtime_check ()
    .add_channel_check ("profile.server")
    .add_registry_check ("registry")
    .add_stream_endpoint_check ("game.stream")
    .add_hosted_service_check ("worker");
  auto healthy = app.health ().report ();
  if (healthy.status != zlink::framework::health_status_t::healthy ||
      !healthy.ready () || !healthy.live () ||
      healthy.checks.size () != 5) {
    return 6;
  }

  app.health ().set_status (
    "profile.server",
    zlink::framework::health_status_t::unhealthy,
    "channel disconnected");
  auto not_ready = app.health ().report ();
  if (not_ready.status != zlink::framework::health_status_t::unhealthy ||
      not_ready.readiness != zlink::framework::health_status_t::unhealthy ||
      not_ready.liveness != zlink::framework::health_status_t::healthy ||
      not_ready.ready () || !not_ready.live ()) {
    return 7;
  }

  app.health ().set_status (
    "worker",
    zlink::framework::health_status_t::unhealthy,
    "hosted service stopped");
  auto not_live = app.health ().report ();
  if (not_live.liveness != zlink::framework::health_status_t::unhealthy ||
      not_live.live ()) {
    return 8;
  }

  return 0;
}
