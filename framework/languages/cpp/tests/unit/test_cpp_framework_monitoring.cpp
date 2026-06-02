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
  int trace_events = 0;
  bool timer_failure_is_summary = false;
  bool stream_transport_distinct = false;
  bool stream_handler_distinct = false;
  bool publisher_seen = false;

  app.monitoring ()
    .add_socket_events ("profile.server")
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
      })
    .on<zlink::framework::discovery_event_payload_t> (
      [&](const zlink::framework::discovery_event_payload_t &event) {
        if (event.source_name == "profile.discovery" &&
            event.endpoint == "tcp://registry:5551") {
          ++discovery_events;
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
  runtime.publish_discovery (zlink::framework::discovery_event_payload_t {
    zlink::framework::runtime_event_base_t { "profile.discovery" },
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
  runtime.publish_spot_snapshot (zlink::framework::spot_event_payload_t {
    zlink::framework::runtime_event_base_t { "stage-node" },
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
  runtime.publish_actor (zlink::framework::actor_event_payload_t {
    zlink::framework::runtime_event_base_t { "game.actor" },
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

  if (socket_events != 1 || discovery_events != 1 || registry_events != 1 ||
      spot_events != 1 || stream_events != 2 || actor_events != 1) {
    return 1;
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
  if (trace_events < 8) {
    return 4;
  }

  return 0;
}
