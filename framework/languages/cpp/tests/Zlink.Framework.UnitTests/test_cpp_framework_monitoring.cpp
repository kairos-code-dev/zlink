/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/framework.hpp>

#include "runtime/diagnostics/monitoring_runtime.hpp"
#include "runtime/diagnostics/runtime_metrics.hpp"

#include "runtime/channels/channel_runtime.hpp"
#include "runtime/diagnostics/monitoring_runtime.hpp"
#include "runtime/locations/in_memory_location_store.hpp"
#include "runtime/locations/location_runtime.hpp"
#include "runtime/spots/spot_runtime.hpp"

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeindex>

namespace
{

struct timer_handler_t
{
};

class monitoring_spot_t : public zlink::framework::spot_t
{
  public:
    void configure (zlink::framework::spot_context_t &) {}
};

} // namespace

int main ()
{
    using namespace std::chrono_literals;

    zlink::framework::app_t app = zlink::framework::app_t::create ();

    int socket_events = 0;
    int location_events = 0;
    int location_summary_events = 0;
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
      .add_socket_events ("filtered.server",
                          {zlink::framework::socket_event_kind_t::connection_ready})
      .add_location_events ("location", 1s)
      .add_spot_events ("stage-node", 1s)
      .add_stream_events ("game.stream")
      .add_actor_events ("game.actor")
      .on_trace ([&] (const zlink::framework::runtime_event_base_t &event) {
          if (!event.source_name.empty ()) {
              ++trace_events;
          }
      })
      .on<zlink::framework::socket_event_payload_t> (
        [&] (const zlink::framework::socket_event_payload_t &event) {
            if (event.source_name == "profile.server"
                && event.event == zlink::framework::socket_event_kind_t::connected) {
                ++socket_events;
            }
            if (event.source_name == "filtered.server") {
                ++filtered_socket_events;
            }
            if (event.source_name == "unregistered.server") {
                ++ignored_events;
            }
        })
      .on<zlink::framework::location_event_payload_t> (
        [&] (const zlink::framework::location_event_payload_t &event) {
            if (event.source_name == "location"
                && event.event == zlink::framework::location_event_kind_t::topology_changed
                && !event.topology.empty ()) {
                ++location_events;
            }
            if (event.source_name == "location"
                && event.event == zlink::framework::location_event_kind_t::service_summary_changed
                && !event.service_summary.empty ()) {
                ++location_summary_events;
            }
            if (event.source_name == "ignored.location") {
                ++ignored_events;
            }
        })
      .on<zlink::framework::spot_event_payload_t> (
        [&] (const zlink::framework::spot_event_payload_t &event) {
            if (event.source_name == "stage-node"
                && event.event
                     == zlink::framework::spot_event_kind_t::timer_stopped_after_unhandled_exception
                && event.timer_diagnostic && event.timer_diagnostic->exception_message == "boom") {
                timer_failure_is_summary = true;
            }
            if (event.source_name == "stage-node"
                && event.event == zlink::framework::spot_event_kind_t::subjects_changed
                && !event.subjects.empty ()) {
                ++spot_events;
            }
            if (event.source_name == "ignored.spot" || event.source_name == "ignored.timer") {
                ++ignored_events;
            }
        })
      .on<zlink::framework::stream_event_payload_t> (
        [&] (const zlink::framework::stream_event_payload_t &event) {
            if (event.event == zlink::framework::stream_event_kind_t::transport_error) {
                stream_transport_distinct = event.message == "connection reset";
            }
            if (event.event == zlink::framework::stream_event_kind_t::handler_exception) {
                stream_handler_distinct = event.message == "handler failed";
            }
            if (event.source_name == "ignored.stream") {
                ++ignored_events;
            }
            ++stream_events;
        })
      .on<zlink::framework::actor_event_payload_t> (
        [&] (const zlink::framework::actor_event_payload_t &event) {
            if (event.actor_id == "alice"
                && event.event == zlink::framework::actor_event_kind_t::bound) {
                ++actor_events;
            }
            if (event.actor_id == "bob"
                && event.event == zlink::framework::actor_event_kind_t::unbound) {
                publisher_seen = true;
            }
            if (event.source_name == "ignored.actor") {
                ++ignored_events;
            }
        })
      .on<zlink::framework::metric_event_payload_t> (
        [&] (const zlink::framework::metric_event_payload_t &event) {
            const auto surface = event.tags.find ("surface");
            if (event.source_name == "runtime.metrics" && event.name == "active_http_requests"
                && event.value == 3 && surface != event.tags.end () && surface->second == "http") {
                ++metric_events;
            }
        });

    const auto runtime = zlink::framework::detail::monitoring_runtime_t::from (app.monitoring ());

    runtime.publish_socket (zlink::framework::socket_event_payload_t{
      zlink::framework::runtime_event_base_t{"profile.server"},
      zlink::framework::socket_event_kind_t::connected, "tcp://127.0.0.1:7001",
      "tcp://127.0.0.1:7002", 1, 0});
    runtime.publish_socket (zlink::framework::socket_event_payload_t{
      zlink::framework::runtime_event_base_t{"filtered.server"},
      zlink::framework::socket_event_kind_t::disconnected, "tcp://127.0.0.1:7001",
      "tcp://127.0.0.1:7002", 1, 0});
    runtime.publish_socket (zlink::framework::socket_event_payload_t{
      zlink::framework::runtime_event_base_t{"filtered.server"},
      zlink::framework::socket_event_kind_t::connection_ready, "tcp://127.0.0.1:7001",
      "tcp://127.0.0.1:7002", 1, 0});
    runtime.publish_socket (zlink::framework::socket_event_payload_t{
      zlink::framework::runtime_event_base_t{"unregistered.server"},
      zlink::framework::socket_event_kind_t::connected, "tcp://127.0.0.1:7001",
      "tcp://127.0.0.1:7002", 1, 0});
    runtime.publish_location_snapshot (
      "location",
      zlink::framework::location_runtime_status_t{.store_healthy = true,
                                                  .watch_enabled = false,
                                                  .polling_interval = 1s,
                                                  .owner_lease_healthy = true},
      {zlink::framework::location_topology_entry_t{
        .kind = zlink::framework::location_kind_t::spot,
        .mesh_name = std::string ("stage-node"),
        .role = zlink::framework::location_role_t::spot,
        .node_rid = zlink::routing_id_t::from ("node"),
        .state = zlink::framework::location_topology_state_t::ready,
        .desired_count = 1,
        .ready_count = 1}},
      {zlink::framework::location_service_summary_t{
        .mesh_name = "stage-node",
        .auto_connect_type = zlink::framework::location_auto_connect_type_t::spot_mesh,
        .role = zlink::framework::location_role_t::spot,
        .total_count = 1,
        .ready_count = 1}});
    runtime.publish_location_snapshot (
      "ignored.location",
      zlink::framework::location_runtime_status_t{.store_healthy = true,
                                                  .watch_enabled = false,
                                                  .polling_interval = 1s,
                                                  .owner_lease_healthy = true},
      {zlink::framework::location_topology_entry_t{
        .kind = zlink::framework::location_kind_t::spot,
        .mesh_name = std::string ("stage-node"),
        .role = zlink::framework::location_role_t::spot,
        .node_rid = zlink::routing_id_t::from ("node"),
        .state = zlink::framework::location_topology_state_t::ready,
        .desired_count = 1,
        .ready_count = 1}},
      {});
    runtime.publish_spot_snapshot (
      zlink::framework::spot_event_payload_t{zlink::framework::runtime_event_base_t{"stage-node"},
                                             zlink::framework::spot_event_kind_t::subjects_changed,
                                             "stage-node",
                                             {},
                                             {"stage.updated"},
                                             std::nullopt});
    runtime.publish_spot_snapshot (
      zlink::framework::spot_event_payload_t{zlink::framework::runtime_event_base_t{"ignored.spot"},
                                             zlink::framework::spot_event_kind_t::subjects_changed,
                                             "stage-node",
                                             {},
                                             {"stage.updated"},
                                             std::nullopt});
    runtime.publish_stream (zlink::framework::stream_event_payload_t{
      zlink::framework::runtime_event_base_t{"game.stream", std::chrono::system_clock::now (),
                                             zlink::framework::runtime_event_severity_t::error},
      zlink::framework::stream_event_kind_t::transport_error, "game.stream", "session-1",
      "connection reset"});
    runtime.publish_stream (zlink::framework::stream_event_payload_t{
      zlink::framework::runtime_event_base_t{"game.stream", std::chrono::system_clock::now (),
                                             zlink::framework::runtime_event_severity_t::error},
      zlink::framework::stream_event_kind_t::handler_exception, "game.stream", "session-1",
      "handler failed"});
    runtime.publish_stream (zlink::framework::stream_event_payload_t{
      zlink::framework::runtime_event_base_t{"ignored.stream"},
      zlink::framework::stream_event_kind_t::transport_error, "ignored.stream", "session-1",
      "ignored"});
    runtime.publish_actor (
      zlink::framework::actor_event_payload_t{zlink::framework::runtime_event_base_t{"game.actor"},
                                              zlink::framework::actor_event_kind_t::bound,
                                              "player",
                                              "alice",
                                              "session-1",
                                              {}});
    runtime.publish_actor (zlink::framework::actor_event_payload_t{
      zlink::framework::runtime_event_base_t{"ignored.actor"},
      zlink::framework::actor_event_kind_t::bound,
      "player",
      "alice",
      "session-1",
      {}});
    app.monitoring ().publisher ().publish (zlink::framework::actor_event_payload_t{
      zlink::framework::runtime_event_base_t{"game.actor"},
      zlink::framework::actor_event_kind_t::unbound, "player", "bob", "session-2", "closed"});
    runtime.publish_timer_failure (
      "stage-node", zlink::framework::spot_rid_t::from_string ("stage-rid"),
      zlink::framework::timer_failure_event_t{
        "heartbeat", std::type_index (typeid (timer_handler_t)), 7, true, "boom"});
    runtime.publish_timer_failure (
      "ignored.timer", zlink::framework::spot_rid_t::from_string ("stage-rid"),
      zlink::framework::timer_failure_event_t{
        "heartbeat", std::type_index (typeid (timer_handler_t)), 7, true, "boom"});
    app.metrics ().add_runtime_metrics ().record_runtime_metric ("active_http_requests", 3,
                                                                 {{"surface", "http"}});

    /* runtime-metrics §4 catalog surface: the internal emitter fills unit,
     * instrument kind and temporality; emission folds when no handler. */
    std::vector<zlink::framework::metric_event_payload_t> catalog_events;
    app.monitoring ().on<zlink::framework::metric_event_payload_t> (
      [&] (const zlink::framework::metric_event_payload_t &event) {
          if (event.source_name == "zlink.framework") {
              catalog_events.push_back (event);
          }
      });
    zlink::framework::runtime::runtime_metrics_t catalog_metrics (runtime.state ());
    if (!catalog_metrics.enabled ()) {
        return 20;
    }
    catalog_metrics.updown ("zlink.spot.count", "{spot}", 1, {{"kind", "user"}});
    catalog_metrics.counter ("zlink.spot.created", "{spot}", 1, {{"kind", "user"}});
    catalog_metrics.histogram ("zlink.channel.request.duration", "s", 0.25,
                               {{"channel", "profile"}});
    catalog_metrics.observable ("zlink.location.peers", "{peer}", 4);
    if (catalog_events.size () != 4) {
        return 21;
    }
    if (catalog_events[0].instrument_kind
          != zlink::framework::metric_instrument_kind_t::updown
        || catalog_events[0].temporality != zlink::framework::metric_temporality_t::delta
        || catalog_events[0].unit != "{spot}"
        || catalog_events[0].tags.at ("kind") != "user") {
        return 22;
    }
    if (catalog_events[1].instrument_kind
          != zlink::framework::metric_instrument_kind_t::counter
        || catalog_events[1].temporality != zlink::framework::metric_temporality_t::delta) {
        return 23;
    }
    if (catalog_events[2].instrument_kind
          != zlink::framework::metric_instrument_kind_t::histogram
        || catalog_events[2].temporality != zlink::framework::metric_temporality_t::sample
        || catalog_events[2].unit != "s" || catalog_events[2].value != 0.25) {
        return 24;
    }
    if (catalog_events[3].instrument_kind
          != zlink::framework::metric_instrument_kind_t::observable
        || catalog_events[3].temporality != zlink::framework::metric_temporality_t::current) {
        return 25;
    }
    auto unsubscribed_state =
      std::make_shared<zlink::framework::detail::monitoring_runtime_state_t> ();
    if (zlink::framework::runtime::runtime_metrics_t (unsubscribed_state).enabled ()) {
        return 26;
    }

    /* runtime-metrics §4.5: the location runtime emits the peers observable
     * and the store.errors counter through the same catalog emitter. */
    catalog_events.clear ();
    zlink::framework::runtime::in_memory_location_store_t catalog_store;
    zlink::framework::runtime::location_runtime_t catalog_location (catalog_store);
    catalog_location.bind_monitoring (runtime.state ());
    catalog_location.observe_discovered_peers (3);
    catalog_location.record_store_error ();
    if (catalog_events.size () != 2) {
        return 27;
    }
    if (catalog_events[0].name != "zlink.location.peers" || catalog_events[0].value != 3
        || catalog_events[0].instrument_kind
             != zlink::framework::metric_instrument_kind_t::observable) {
        return 28;
    }
    if (catalog_events[1].name != "zlink.location.store.errors"
        || catalog_events[1].value != 1
        || catalog_events[1].instrument_kind
             != zlink::framework::metric_instrument_kind_t::counter
        || catalog_events[1].unit != "{error}") {
        return 29;
    }

    if (socket_events != 1 || filtered_socket_events != 1 || location_events != 1
        || location_summary_events != 1 || spot_events != 1
        || stream_events != 2 || actor_events != 1 || metric_events != 1) {
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

    zlink::framework::monitoring_builder_t throttled_spot_monitoring;
    int throttled_spot_events = 0;
    std::vector<std::string> latest_subjects;
    throttled_spot_monitoring.add_spot_events ("throttled.spot", 40ms)
      .on<zlink::framework::spot_event_payload_t> (
        [&] (const zlink::framework::spot_event_payload_t &event) {
            ++throttled_spot_events;
            latest_subjects = event.subjects;
        });
    auto throttled_runtime =
      zlink::framework::detail::monitoring_runtime_t::from (throttled_spot_monitoring);
    throttled_runtime.publish_spot_snapshot (
      {zlink::framework::runtime_event_base_t{"throttled.spot"},
       zlink::framework::spot_event_kind_t::subjects_changed, "throttled.spot", {}, {"one"},
       std::nullopt});
    throttled_runtime.publish_spot_snapshot (
      {zlink::framework::runtime_event_base_t{"throttled.spot"},
       zlink::framework::spot_event_kind_t::subjects_changed, "throttled.spot", {}, {"two"},
       std::nullopt});
    if (throttled_spot_events != 1) {
        return 36;
    }
    std::this_thread::sleep_for (50ms);
    throttled_runtime.flush_spot_snapshots ("throttled.spot");
    if (throttled_spot_events != 2 || latest_subjects != std::vector<std::string>{"two"}) {
        return 37;
    }

    bool duplicate_source_failed = false;
    try {
        app.monitoring ().add_socket_events ("profile.server");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        duplicate_source_failed =
          error.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
    }
    if (!duplicate_source_failed) {
        return 10;
    }

    bool empty_source_failed = false;
    try {
        zlink::framework::app_t::create ().monitoring ().add_socket_events (" ");
    }
    catch (const zlink::framework::framework_exception_t &error) {
        empty_source_failed =
          error.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
    }
    if (!empty_source_failed) {
        return 11;
    }

    bool invalid_location_interval_failed = false;
    try {
        zlink::framework::app_t::create ().monitoring ().add_location_events ("location", 0ms);
    }
    catch (const zlink::framework::framework_exception_t &error) {
        invalid_location_interval_failed =
          error.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
    }
    if (!invalid_location_interval_failed) {
        return 12;
    }

    bool missing_spot_source_failed = false;
    try {
        auto invalid_app = zlink::framework::app_t::create ();
        invalid_app.monitoring ().add_spot_events ("missing.spot", 1s);
        invalid_app.add_zlink_framework ([] (zlink::framework::zlink_framework_options_t &) {});
    }
    catch (const zlink::framework::framework_exception_t &error) {
        missing_spot_source_failed =
          error.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
    }
    if (!missing_spot_source_failed) {
        return 14;
    }

    bool missing_socket_source_failed = false;
    try {
        auto invalid_app = zlink::framework::app_t::create ();
        invalid_app.monitoring ().add_socket_events ("missing.server");
        invalid_app.add_zlink_framework ([] (zlink::framework::zlink_framework_options_t &) {});
    }
    catch (const zlink::framework::framework_exception_t &error) {
        missing_socket_source_failed =
          error.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
    }
    if (!missing_socket_source_failed) {
        return 15;
    }

    bool duplicate_spot_source_failed = false;
    try {
        auto invalid_app = zlink::framework::app_t::create ();
        invalid_app.monitoring ()
          .add_spot_events ("stage-node", 1s)
          .add_spot_events ("stage-node", 1s);
    }
    catch (const zlink::framework::framework_exception_t &error) {
        duplicate_spot_source_failed =
          error.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
    }
    if (!duplicate_spot_source_failed) {
        return 13;
    }

    bool monitoring_handler_failure_isolated = false;
    auto failure_app = zlink::framework::app_t::create ();
    failure_app.monitoring ()
      .add_socket_events ("failure.server")
      .on<zlink::framework::socket_event_payload_t> (
        [] (const zlink::framework::socket_event_payload_t &) {
            throw std::runtime_error ("monitoring dispatch failure for unit test");
        })
      .on<zlink::framework::socket_event_payload_t> (
        [&] (const zlink::framework::socket_event_payload_t &event) {
            monitoring_handler_failure_isolated =
              event.source_name == "failure.server"
              && event.event == zlink::framework::socket_event_kind_t::connected;
        });
    zlink::framework::detail::monitoring_runtime_t::from (failure_app.monitoring ())
      .publish_socket (zlink::framework::socket_event_payload_t{
        zlink::framework::runtime_event_base_t{"failure.server"},
        zlink::framework::socket_event_kind_t::connected, "tcp://127.0.0.1:7001",
        "tcp://127.0.0.1:7002", 1, 0});
    if (!monitoring_handler_failure_isolated) {
        return 16;
    }

    app.health ()
      .add_zlink_runtime_check ()
      .add_channel_check ("profile.server")
      .add_location_check ("location")
      .add_stream_endpoint_check ("game.stream")
      .add_hosted_service_check ("worker");
    auto healthy = app.health ().report ();
    if (healthy.status != zlink::framework::health_status_t::healthy || !healthy.ready ()
        || !healthy.live () || healthy.checks.size () != 5) {
        return 6;
    }

    app.health ().set_status ("profile.server", zlink::framework::health_status_t::unhealthy,
                              "channel disconnected");
    auto not_ready = app.health ().report ();
    if (not_ready.status != zlink::framework::health_status_t::unhealthy
        || not_ready.readiness != zlink::framework::health_status_t::unhealthy
        || not_ready.liveness != zlink::framework::health_status_t::healthy || not_ready.ready ()
        || !not_ready.live ()) {
        return 7;
    }

    app.health ().set_status ("worker", zlink::framework::health_status_t::unhealthy,
                              "hosted service stopped");
    auto not_live = app.health ().report ();
    if (not_live.liveness != zlink::framework::health_status_t::unhealthy || not_live.live ()) {
        return 8;
    }

    zlink::framework::monitoring_builder_t auto_monitoring;
    zlink::framework::zlink_builder_t auto_zlink;
    zlink::framework::serializer_registry_t auto_serializers;
    int auto_channel_events = 0;
    int auto_spot_events = 0;
    auto_monitoring.add_socket_events ("auto.channel")
      .add_spot_events ("auto.spot.node", 1s)
      .on<zlink::framework::socket_event_payload_t> (
        [&] (const zlink::framework::socket_event_payload_t &event) {
            if (event.source_name == "auto.channel"
                && event.event == zlink::framework::socket_event_kind_t::peer_admission_changed) {
                ++auto_channel_events;
            }
        })
      .on<zlink::framework::spot_event_payload_t> (
        [&] (const zlink::framework::spot_event_payload_t &event) {
            if (event.source_name == "auto.spot.node"
                && event.event == zlink::framework::spot_event_kind_t::subjects_changed
                && !event.subjects.empty ()) {
                ++auto_spot_events;
            }
        });
    auto_zlink.add_node ("auto-node");
    auto_zlink.channel ("auto.channel").enable_server ().bind ("inproc://auto-channel");
    auto spot_node = auto_zlink.add_spot_node ("auto.spot.node");
    spot_node.add_spot<monitoring_spot_t> ("auto.spot");
    zlink::framework::detail::channel_runtime_t::from (auto_zlink.message_bus ())
      .bind_serializers (auto_serializers);
    zlink::framework::detail::bind_zlink_monitoring (auto_zlink, auto_monitoring);
    zlink::framework::detail::channel_runtime_t::from (auto_zlink.message_bus ())
      .set_server_peer_weight ("auto.channel", zlink::peer_weight_t::value (7));
    auto spot_runtime =
      zlink::framework::detail::spot_node_runtime_t::from (auto_zlink, "auto.spot.node");
    if (!spot_runtime) {
        return 14;
    }
    auto created_spot = spot_runtime->create_spot ("auto.spot");
    if (created_spot.state != zlink::framework::spot_create_state_t::created) {
        return 14;
    }
    if (auto_channel_events != 1 || auto_spot_events != 1) {
        return 15;
    }

    /* graceful drain state machine (graceful-drain-handoff §6): idempotent
     * shared operation, shared terminal result, readiness flip and drain
     * lifecycle events without source registration. */
    {
        auto drain_app = zlink::framework::app_t::create ();
        std::vector<zlink::framework::drain_state_t> transitions;
        drain_app.monitoring ().on<zlink::framework::drain_event_t> (
          [&] (const zlink::framework::drain_event_t &event) {
              if (event.source_name == "drain") {
                  transitions.push_back (event.state);
              }
          });
        if (!drain_app.is_ready ()) {
            return 30;
        }
        bool zero_deadline_rejected = false;
        try {
            (void) drain_app.drain (std::chrono::milliseconds (0));
        }
        catch (const zlink::framework::framework_exception_t &) {
            zero_deadline_rejected = true;
        }
        if (!zero_deadline_rejected) {
            return 31;
        }
        auto early_waiter = drain_app.await_drained ();
        auto first = drain_app.drain (std::chrono::milliseconds (2000));
        auto second = drain_app.drain (std::chrono::milliseconds (1));
        const auto first_result = first.result ().value ();
        const auto second_result = second.result ().value ();
        const auto early_result = early_waiter.result ().value ();
        if (!std::holds_alternative<zlink::framework::drained_t> (first_result)
            || !std::holds_alternative<zlink::framework::drained_t> (second_result)
            || !std::holds_alternative<zlink::framework::drained_t> (early_result)) {
            return 32;
        }
        if (drain_app.is_ready ()) {
            return 33;
        }
        const auto late_result = drain_app.await_drained ().result ().value ();
        if (!std::holds_alternative<zlink::framework::drained_t> (late_result)) {
            return 34;
        }
        if (transitions.empty ()
            || transitions.front () != zlink::framework::drain_state_t::draining
            || transitions.back () != zlink::framework::drain_state_t::drained) {
            return 35;
        }
    }


    return 0;
}
