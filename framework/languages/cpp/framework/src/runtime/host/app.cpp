/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework/contracts/configuration/app.hpp>

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/actors/actor_client.hpp"
#include "runtime/channels/channel_host_service.hpp"
#include "runtime/channels/channel_runtime.hpp"
#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/channels/route_channel_host_service.hpp"
#include "runtime/diagnostics/monitoring_runtime.hpp"
#include "runtime/dispatch/coroutine_executor.hpp"
#include "runtime/host/actor_gateway_spot_bridge.hpp"
#include "runtime/host/framework_runtime.hpp"
#include "runtime/http/http_host_service.hpp"
#include "runtime/locations/in_memory_location_store.hpp"
#include "runtime/locations/location_auto_connect_host_service.hpp"
#include "runtime/locations/location_host_service.hpp"
#include "runtime/locations/location_lifecycle.hpp"
#include "runtime/locations/location_monitoring_host_service.hpp"
#include "runtime/locations/location_runtime.hpp"
#include "runtime/locations/store_location_resolvers.hpp"
#include "runtime/spots/spot_node_host_service.hpp"
#include "runtime/spots/spot_runtime.hpp"
#include "runtime/streams/stream_host_service.hpp"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

namespace zlink::framework::detail
{
void configure_handler_invocation_executor ();
void shutdown_handler_invocation_executor () noexcept;
} // namespace zlink::framework::detail

namespace zlink::framework::detail
{

class store_actor_directory_t final : public actor_directory_t
{
  public:
    explicit store_actor_directory_t (location_store_t &store) : _store (store) {}

    task_t<std::optional<actor_ref_t>> find (std::string actor_id) override
    {
        auto row = _store.resolve_actor (actor_location_key_t{std::move (actor_id)}).result ();
        if (!row) {
            return task_t<std::optional<actor_ref_t>> (
              result_t<std::optional<actor_ref_t>>::failure (
                row.error_kind (),
                row.error () ? row.error ()->what () : "actor location lookup failed",
                row.error () && row.error ()->is_retriable ()));
        }
        if (!row.value () || !row.value ()->actor_ref) {
            return task_t<std::optional<actor_ref_t>> (
              result_t<std::optional<actor_ref_t>>::success (std::nullopt));
        }
        return task_t<std::optional<actor_ref_t>> (
          result_t<std::optional<actor_ref_t>>::success (*row.value ()->actor_ref));
    }

    task_t<actor_ref_t> ensure (std::string, message_t, actor_placement_t) override
    {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
          framework_error_kind_t::request_failed,
          "actor_directory_t::ensure is not configured for this framework host"));
    }

  private:
    location_store_t &_store;
};

bool has_inbound_channel (const std::vector<channel_snapshot_t> &channels)
{
    for (const auto &channel : channels) {
        if (channel.server.enabled && !channel.server.bind_endpoints.empty ()) {
            return true;
        }
        if (channel.subscriber.enabled
            && (channel.subscriber.discovery || !channel.subscriber.connect_endpoints.empty ())) {
            return true;
        }
    }
    return false;
}

zlink::routing_id_t
location_owner_node_rid (const std::vector<spot_node_snapshot_t> &spot_node_snapshot)
{
    if (spot_node_snapshot.empty ()) {
        return zlink::routing_id_t::from ("framework");
    }
    const auto &first_node = spot_node_snapshot.front ();
    if (first_node.routing_id) {
        return *first_node.routing_id;
    }
    return zlink::routing_id_t::from (first_node.name);
}

bool monitoring_socket_source_exists (const std::vector<channel_snapshot_t> &channels,
                                      const std::string &source_name)
{
    const auto channel_name_exists =
      std::any_of (channels.begin (), channels.end (),
                   [&] (const channel_snapshot_t &channel) { return channel.name == source_name; });
    if (channel_name_exists) {
        return true;
    }
    const auto separator = source_name.rfind ('.');
    if (separator == std::string::npos) {
        return false;
    }
    if (separator == 0 || separator + 1 == source_name.size ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "socket monitoring source must use '<channel>.<capability>'");
    }
    const auto channel_name = source_name.substr (0, separator);
    const auto capability = source_name.substr (separator + 1);
    return std::any_of (channels.begin (), channels.end (),
                        [&] (const channel_snapshot_t &channel) {
                            if (channel.name != channel_name) {
                                return false;
                            }
                            return (capability == "server" && channel.server.enabled)
                                   || (capability == "client" && channel.client.enabled)
                                   || (capability == "publisher" && channel.publisher.enabled)
                                   || (capability == "subscriber" && channel.subscriber.enabled);
                        });
}

void validate_monitoring_sources (const monitoring_builder_t &monitoring,
                                  const std::vector<channel_snapshot_t> &channels,
                                  const std::vector<spot_node_snapshot_t> &spot_nodes)
{
    const auto state = monitoring_runtime_t::from (monitoring).state ();
    for (const auto &source : state->socket_sources) {
        if (!monitoring_socket_source_exists (channels, source.source_name)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "socket monitoring source '" + source.source_name
                                           + "' is not registered");
        }
    }
    for (const auto &source : state->spot_sources) {
        const auto exists = std::any_of (spot_nodes.begin (), spot_nodes.end (),
                                         [&] (const spot_node_snapshot_t &spot_node) {
                                             return spot_node.name == source.source_name;
                                         });
        if (!exists) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "spot monitoring source '" + source.source_name
                                           + "' is not registered");
        }
    }
}

class app_state_t
{
  public:
    app_state_t () : metrics (monitoring) {}
    ~app_state_t ()
    {
        const char *trace_value = std::getenv ("ZLINK_CPP_HOST_STOP_TRACE");
        const bool trace_enabled = trace_value != nullptr && std::string_view (trace_value) != "0"
                                   && std::string_view (trace_value) != "";
        if (trace_enabled) {
            std::cerr << "zlink-cpp-host-stop stage=before-app-state-destroy-services"
                      << std::endl;
        }
        hosted_services.clear ();
        if (trace_enabled) {
            std::cerr << "zlink-cpp-host-stop stage=after-app-state-destroy-services"
                      << std::endl;
        }
    }

    void start_hosted_services (service_provider_t &provider,
                                std::vector<hosted_service_t *> &started)
    {
        for (const auto &service : hosted_services) {
            service->start (provider);
            started.push_back (service.get ());
        }
    }

    void stop_hosted_services (const std::vector<hosted_service_t *> &started) noexcept
    {
        const char *trace_value = std::getenv ("ZLINK_CPP_HOST_STOP_TRACE");
        const bool trace_enabled = trace_value != nullptr && std::string_view (trace_value) != "0"
                                   && std::string_view (trace_value) != "";
        auto stop_service = [trace_enabled] (hosted_service_t *service) {
            if (trace_enabled) {
                std::cerr << "zlink-cpp-host-stop stage=before service="
                          << typeid (*service).name () << std::endl;
            }
            service->stop ();
            if (trace_enabled) {
                std::cerr << "zlink-cpp-host-stop stage=after service="
                          << typeid (*service).name () << std::endl;
            }
        };
        std::vector<hosted_service_t *> stream_services;
        stream_services.reserve (started.size ());
        std::vector<hosted_service_t *> http_services;
        http_services.reserve (started.size ());
        std::vector<hosted_service_t *> spot_services;
        spot_services.reserve (started.size ());
        std::vector<hosted_service_t *> route_services;
        route_services.reserve (started.size ());
        for (auto it = started.rbegin (); it != started.rend (); ++it) {
            if (dynamic_cast<runtime::stream_host_service_t *> (*it) != nullptr) {
                stream_services.push_back (*it);
            } else if (dynamic_cast<runtime::http_host_service_t *> (*it) != nullptr) {
                http_services.push_back (*it);
            } else if (dynamic_cast<runtime::spot_node_host_service_t *> (*it) != nullptr) {
                spot_services.push_back (*it);
            } else if (dynamic_cast<runtime::route_channel_host_service_t *> (*it) != nullptr) {
                route_services.push_back (*it);
            }
        }
        for (auto *service : stream_services) {
            service->request_stop ();
        }
        for (auto it = started.rbegin (); it != started.rend (); ++it) {
            if (dynamic_cast<runtime::stream_host_service_t *> (*it) != nullptr) {
                continue;
            }
            (*it)->request_stop ();
        }
        for (auto *service : http_services) {
            stop_service (service);
        }
        for (auto *service : stream_services) {
            stop_service (service);
        }
        for (auto *service : spot_services) {
            stop_service (service);
        }
        for (auto *service : route_services) {
            stop_service (service);
        }
        for (auto it = started.rbegin (); it != started.rend (); ++it) {
            if (dynamic_cast<runtime::stream_host_service_t *> (*it) != nullptr) {
                continue;
            }
            if (dynamic_cast<runtime::http_host_service_t *> (*it) != nullptr) {
                continue;
            }
            if (dynamic_cast<runtime::spot_node_host_service_t *> (*it) != nullptr) {
                continue;
            }
            if (dynamic_cast<runtime::route_channel_host_service_t *> (*it) != nullptr) {
                continue;
            }
            stop_service (*it);
        }
    }

    service_collection_t services;
    handler_registry_t handlers;
    config_builder_t config;
    logging_builder_t logging;
    monitoring_builder_t monitoring;
    metrics_builder_t metrics;
    health_builder_t health;
    zlink_builder_t zlink;
    serializer_registry_t serializers;
    std::vector<std::unique_ptr<hosted_service_t>> hosted_services;
    std::atomic_bool stop_requested = false;
    int exit_code = 0;
    // Shared, runtime-mutable message-flow mode (set_message_flow_mode). Created
    // once here (never reassigned) so concurrent set/apply only touch the atomic,
    // not the shared_ptr. Installed into dispatch options at apply.
    std::shared_ptr<std::atomic<message_flow_log_mode_t>> message_flow_mode =
      std::make_shared<std::atomic<message_flow_log_mode_t>> (message_flow_log_mode_t::errors_only);
};

} // namespace zlink::framework::detail

namespace
{

volatile std::sig_atomic_t g_stop_signal_requested = 0;

void handle_process_signal (int) noexcept
{
    g_stop_signal_requested = 1;
}

bool host_stop_trace_enabled ()
{
    const char *value = std::getenv ("ZLINK_CPP_HOST_STOP_TRACE");
    return value != nullptr && std::string_view (value) != "0" && std::string_view (value) != "";
}

} // namespace

namespace zlink::framework
{

app_t::app_t () : _state (std::make_unique<detail::app_state_t> ())
{
}

app_t::~app_t ()
{
}

app_t::app_t (app_t &&) noexcept = default;

app_t &app_t::operator= (app_t &&) noexcept = default;

app_t app_t::create ()
{
    return {};
}

app_advanced_t::app_advanced_t (app_t &app) noexcept : _app (&app)
{
}

service_collection_t &app_advanced_t::services () noexcept
{
    return _app->_services ();
}

handler_registry_t &app_advanced_t::handlers () noexcept
{
    return _app->_handlers ();
}

zlink_builder_t &app_advanced_t::zlink () noexcept
{
    return _app->_zlink_builder ();
}

config_builder_t &app_t::config () noexcept
{
    return _state->config;
}

logging_builder_t &app_t::logging () noexcept
{
    return _state->logging;
}

monitoring_builder_t &app_t::monitoring () noexcept
{
    return _state->monitoring;
}

app_t &app_t::set_message_flow_mode (message_flow_log_mode_t mode) noexcept
{
    // Note: before apply() this is overwritten by the configured mode (config seeds
    // at apply); the intended use is runtime toggling after the app is running.
    _state->message_flow_mode->store (mode, std::memory_order_relaxed);
    return *this;
}

message_flow_log_mode_t app_t::message_flow_mode () const noexcept
{
    return _state->message_flow_mode->load (std::memory_order_relaxed);
}

metrics_builder_t &app_t::metrics () noexcept
{
    return _state->metrics;
}

health_builder_t &app_t::health () noexcept
{
    return _state->health;
}

app_advanced_t app_t::advanced () noexcept
{
    return app_advanced_t (*this);
}

service_collection_t &app_t::_services () noexcept
{
    return _state->services;
}

handler_registry_t &app_t::_handlers () noexcept
{
    return _state->handlers;
}

zlink_builder_t &app_t::_zlink_builder () noexcept
{
    return _state->zlink;
}

serializer_registry_t &app_t::_serializers () noexcept
{
    return _state->serializers;
}

app_t &app_t::add_zlink_framework (std::function<void (zlink_framework_options_t &)> configure)
{
    detail::channel_runtime_t::from (_state->zlink.message_bus ())
      .bind_serializers (_state->serializers);
    if (!_state->services.contains (std::type_index (typeid (logger_factory_t)))) {
        _state->services.add_singleton<logger_factory_t> (
          std::make_unique<logger_factory_t> (_state->logging.factory ()));
    }
    if (!_state->services.contains (std::type_index (typeid (detail::actor_gateway_runtime_t)))) {
        _state->services.add_singleton<detail::actor_gateway_runtime_t> ();
    }
    if (!_state->services.contains (std::type_index (typeid (session_actor_manager_t)))) {
        _state->services.add_factory<session_actor_manager_t> (
          [] (service_provider_t &provider) {
              return std::make_unique<session_actor_manager_t> (
                provider.get_required<detail::actor_gateway_runtime_t> ().manager ());
          },
          service_lifetime_t::scoped);
    }
    if (!_state->services.contains (std::type_index (typeid (actor_gateway_t)))) {
        _state->services.add_factory<actor_gateway_t> (
          [] (service_provider_t &provider) {
              return std::make_unique<actor_gateway_t> (
                provider.get_required<detail::actor_gateway_runtime_t> ().gateway ());
          },
          service_lifetime_t::scoped);
    }
    _state->services.add_singleton<channel_client_t> (
      std::make_unique<channel_client_t> (_state->zlink.message_bus ()));
    _state->services.add_singleton<channel_runtime_options_t> (
      std::make_unique<channel_runtime_options_t> (_state->zlink.message_bus ()));
    _state->services.add_singleton<publisher_t> (
      std::make_unique<publisher_t> (_state->zlink.publisher ()));
    _state->services.add_factory<route_client_t> (
      [this] (service_provider_t &) {
          return std::make_unique<route_client_t> (
            _state->zlink.route_client (_state->serializers));
      },
      service_lifetime_t::singleton);
    if (!_state->services.contains (std::type_index (typeid (serializer_registry_t)))) {
        _state->services.add_factory<serializer_registry_t> (
          [serializers = &_state->serializers] (service_provider_t &) {
              return std::shared_ptr<serializer_registry_t> (
                serializers, [] (serializer_registry_t *) noexcept {});
          },
          service_lifetime_t::singleton);
    }
    zlink_framework_options_t options (_state->services, _state->handlers, _state->serializers,
                                       _state->zlink, _state->monitoring);
    if (configure) {
        configure (options);
    }
    // Route message-flow tracing and dispatch errors to a logger. The user picks:
    //  - diagnostics.log_file set  -> SEPARATED: a dedicated file logger, so tracing
    //    never mixes with application logs. (Its logging state stays alive through
    //    the logger_t copies carried in the propagated dispatch options.)
    //  - otherwise, if an app logging sink is configured -> MERGED: the shared
    //    application logger captures both app and tracing logs together.
    //  - otherwise -> left unset, std::clog fallback (and no unbounded in-memory
    //    record buffering for high-volume traffic).
    // Install the shared, runtime-mutable message-flow mode so set_message_flow_mode
    // can flip tracing on/off live. Seeded from the configured mode; shared across
    // all surfaces because dispatch options copy the shared_ptr.
    // Seed the (already-created) shared atomic from the configured mode and share it
    // with every surface via dispatch options. The shared_ptr is never reassigned,
    // so runtime set_message_flow_mode races only on the atomic (safe).
    _state->message_flow_mode->store (options.configure_dispatch ().diagnostics.message_flow (),
                                      std::memory_order_relaxed);
    options.configure_dispatch ().message_flow_live (_state->message_flow_mode);
    if (const auto &diagnostics_log_file = options.configure_dispatch ().diagnostics.log_file ();
        diagnostics_log_file) {
        logging_builder_t flow_logging;
        flow_logging.use_file (*diagnostics_log_file);
        options.configure_dispatch ().diagnostics_logger =
          flow_logging.factory ().create ("zlink.framework.dispatch");
    } else if (_state->logging.has_output_sink ()) {
        options.configure_dispatch ().diagnostics_logger =
          _state->logging.factory ().create ("zlink.framework.dispatch");
    }
    const auto http_snapshot = options.http ().snapshot ();
    options.apply ();
    if (!_state->services.contains (std::type_index (typeid (location_store_t)))) {
        auto store = std::make_shared<runtime::in_memory_location_store_t> ();
        _state->services.add_factory<location_store_t> (
          [store] (service_provider_t &) {
              return std::static_pointer_cast<location_store_t> (store);
          },
          service_lifetime_t::singleton);
        _state->services.add_factory<location_change_stamp_store_t> (
          [store] (service_provider_t &) {
              return std::static_pointer_cast<location_change_stamp_store_t> (store);
          },
          service_lifetime_t::singleton);
    }
    if (!_state->services.contains (std::type_index (typeid (runtime::location_runtime_t)))) {
        const auto location_options = options.location_options ();
        _state->services.add_factory<runtime::location_runtime_t> (
          [location_options] (service_provider_t &provider) {
              return std::make_unique<runtime::location_runtime_t> (
                provider.get_required<location_store_t> (), location_options);
          },
          service_lifetime_t::singleton);
    }
    if (!_state->services.contains (std::type_index (typeid (runtime::location_lifecycle_t)))) {
        _state->services.add_factory<runtime::location_lifecycle_t> (
          [] (service_provider_t &provider) {
              return std::make_unique<runtime::location_lifecycle_t> (
                provider.get_required<runtime::location_runtime_t> ());
          },
          service_lifetime_t::singleton);
    }
    if (!_state->services.contains (
          std::type_index (typeid (runtime::store_location_resolvers_t)))) {
        _state->services.add_factory<runtime::store_location_resolvers_t> (
          [] (service_provider_t &provider) {
              return std::make_unique<runtime::store_location_resolvers_t> (
                provider.get_required<location_store_t> ());
          },
          service_lifetime_t::singleton);
    }
    if (!_state->services.contains (std::type_index (typeid (peer_location_resolver_t)))) {
        _state->services.add_factory<peer_location_resolver_t> (
          [] (service_provider_t &provider) {
              return std::shared_ptr<peer_location_resolver_t> (
                &provider.get_required<runtime::store_location_resolvers_t> (),
                [] (peer_location_resolver_t *) noexcept {});
          },
          service_lifetime_t::singleton);
    }
    if (!_state->services.contains (std::type_index (typeid (spot_location_resolver_t)))) {
        _state->services.add_factory<spot_location_resolver_t> (
          [] (service_provider_t &provider) {
              return std::shared_ptr<spot_location_resolver_t> (
                &provider.get_required<runtime::store_location_resolvers_t> (),
                [] (spot_location_resolver_t *) noexcept {});
          },
          service_lifetime_t::singleton);
    }
    if (!_state->services.contains (std::type_index (typeid (actor_location_resolver_t)))) {
        _state->services.add_factory<actor_location_resolver_t> (
          [] (service_provider_t &provider) {
              return std::shared_ptr<actor_location_resolver_t> (
                &provider.get_required<runtime::store_location_resolvers_t> (),
                [] (actor_location_resolver_t *) noexcept {});
          },
          service_lifetime_t::singleton);
    }
    if (!_state->services.contains (std::type_index (typeid (actor_directory_t)))) {
        _state->services.add_factory<actor_directory_t> (
          [] (service_provider_t &provider) {
              return std::shared_ptr<actor_directory_t> (
                std::make_shared<detail::store_actor_directory_t> (
                  provider.get_required<location_store_t> ()));
          },
          service_lifetime_t::singleton);
    }
    if (!_state->services.contains (std::type_index (typeid (location_readiness_t)))) {
        _state->services.add_factory<location_readiness_t> (
          [] (service_provider_t &provider) {
              return std::shared_ptr<location_readiness_t> (
                &provider.get_required<runtime::store_location_resolvers_t> (),
                [] (location_readiness_t *) noexcept {});
          },
          service_lifetime_t::singleton);
    }
    if (!_state->services.contains (std::type_index (typeid (location_runtime_query_t)))) {
        const auto location_options = options.location_options ();
        _state->services.add_factory<location_runtime_query_t> (
          [location_options] (service_provider_t &provider) {
              return std::shared_ptr<location_runtime_query_t> (
                std::make_shared<runtime::store_location_runtime_query_t> (
                  provider.get_required<location_store_t> (),
                  provider.get_required<runtime::location_runtime_t> (), location_options));
          },
          service_lifetime_t::singleton);
    }
    detail::bind_zlink_monitoring (_state->zlink, _state->monitoring);
    detail::bind_stream_serializers (_state->zlink, _state->serializers);
    auto &actor_gateway_runtime =
      _state->services.build_provider ().get_required<detail::actor_gateway_runtime_t> ();
    auto &location_lifecycle =
      _state->services.build_provider ().get_required<runtime::location_lifecycle_t> ();
    auto &spot_location_resolver =
      _state->services.build_provider ().get_required<spot_location_resolver_t> ();
    actor_gateway_runtime.bind_serializers (_state->serializers);
    actor_gateway_runtime.set_dispatch (options.configure_dispatch ());
    const auto channel_snapshot = _state->zlink.channels ();
    detail::channel_runtime_manager_t::from (_state->zlink)
      .initialize_route_channels (_state->zlink);
    const auto spot_node_snapshot = _state->zlink.spot_nodes ();
    add_hosted_service (std::make_unique<runtime::location_host_service_t> (
      detail::location_owner_node_rid (spot_node_snapshot)));
    auto monitoring_state = detail::monitoring_runtime_t::from (_state->monitoring).state ();
    if (!monitoring_state->location_sources.empty ()) {
        add_hosted_service (
          std::make_unique<runtime::location_monitoring_host_service_t> (monitoring_state));
    }
    const auto stream_snapshot = _state->zlink.streams ();
    detail::validate_monitoring_sources (_state->monitoring, channel_snapshot, spot_node_snapshot);
    std::vector<runtime::spot_node_host_service_t::node_runtime_t> spot_node_runtimes;
    if (!spot_node_snapshot.empty ()) {
        for (const auto &spot_node : spot_node_snapshot) {
            auto runtime = detail::spot_node_runtime_t::from (_state->zlink, spot_node.name);
            if (runtime) {
                runtime->bind_location_lifecycle (location_lifecycle);
                runtime->bind_spot_location_resolver (spot_location_resolver);
                spot_node_runtimes.push_back (
                  runtime::spot_node_host_service_t::node_runtime_t{spot_node, *runtime});
            }
        }
        add_hosted_service (
          std::make_unique<runtime::spot_node_host_service_t> (spot_node_runtimes));
    }
    add_hosted_service (std::make_unique<runtime::location_auto_connect_host_service_t> (
      _state->zlink.message_bus (), channel_snapshot, options.route_mesh_client_channels ()));
    if (detail::has_inbound_channel (channel_snapshot)) {
        add_hosted_service (std::make_unique<runtime::channel_host_service_t> (
          _state->zlink.message_bus (), channel_snapshot, _state->handlers, _state->serializers));
    }
    if (!_state->services.contains (std::type_index (typeid (actor_client_t)))) {
        std::vector<detail::spot_node_runtime_t> actor_client_spot_nodes;
        actor_client_spot_nodes.reserve (spot_node_runtimes.size ());
        for (const auto &spot_node : spot_node_runtimes) {
            actor_client_spot_nodes.push_back (spot_node.runtime);
        }
        _state->services.add_factory<actor_client_t> (
          [spot_nodes = std::move (actor_client_spot_nodes)] (service_provider_t &provider) {
              return runtime::make_actor_client (provider.get_required<location_store_t> (),
                                                 provider.get_required<serializer_registry_t> (),
                                                 spot_nodes);
          },
          service_lifetime_t::singleton);
    }
    if (!_state->zlink.route_channels ().empty ()) {
        std::vector<runtime::route_channel_host_service_t::spot_node_runtime_t>
          route_spot_node_runtimes;
        route_spot_node_runtimes.reserve (spot_node_runtimes.size ());
        for (const auto &spot_node : spot_node_runtimes) {
            route_spot_node_runtimes.push_back (
              runtime::route_channel_host_service_t::spot_node_runtime_t{spot_node.snapshot,
                                                                         spot_node.runtime});
        }
        add_hosted_service (std::make_unique<runtime::route_channel_host_service_t> (
          _state->zlink.message_bus (), _state->serializers, std::move (route_spot_node_runtimes),
          detail::build_route_internal_dispatchers (
            _state->zlink, spot_node_snapshot, _state->zlink.route_channels (),
            _state->services.build_provider ().get_required<detail::actor_gateway_runtime_t> (),
            _state->serializers)));
    }
    detail::configure_actor_gateway_spot_bridge (_state->zlink, _state->services,
                                                 _state->serializers, spot_node_snapshot);
    const bool has_spot_publisher_client =
      std::any_of (spot_node_snapshot.begin (), spot_node_snapshot.end (),
                   [] (const spot_node_snapshot_t &spot_node) {
                       return spot_node.pub_bind_endpoint.has_value ();
                   });
    if (has_spot_publisher_client
        && !_state->services.contains (std::type_index (typeid (spot_publisher_client_t)))) {
        _state->services.add_factory<spot_publisher_client_t> (
          [] (service_provider_t &provider) {
              return std::make_unique<spot_publisher_client_t> (
                provider.get_required<spot_node_manager_t> (),
                provider.get_required<serializer_registry_t> ());
          },
          service_lifetime_t::singleton);
    }
    if (!stream_snapshot.empty ()) {
        detail::configure_stream_dispatch_executor ();
        add_hosted_service (std::make_unique<runtime::stream_host_service_t> (
          detail::stream_runtime_t::from (_state->zlink), stream_snapshot,
          options.stream_session_factories ()));
    }
    if (!http_snapshot.endpoints.empty ()) {
        add_hosted_service (std::make_unique<runtime::http_host_service_t> (
          http_snapshot, _state->health, options.handler_coroutine_workers ()));
    }
    runtime::configure_handler_coroutine_executor (options.handler_coroutine_workers ());
    detail::configure_handler_invocation_executor ();
    return *this;
}

app_t &app_t::add_module (module_t &module)
{
    module.configure_services (_state->services);
    module.configure_zlink (_state->zlink);
    module.configure_handlers (_state->handlers);
    module.configure_monitoring (_state->monitoring);
    return *this;
}

app_t &app_t::add_hosted_service (std::unique_ptr<hosted_service_t> service)
{
    if (!service) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "hosted service must not be null");
    }
    _state->hosted_services.push_back (std::move (service));
    return *this;
}

int app_t::run (int argc, char **argv)
{
    _state->config.load_cli (argc, argv);
    _state->config.model ().set ("host.signal_handlers", "installed");
    g_stop_signal_requested = 0;
    std::signal (SIGINT, handle_process_signal);
    std::signal (SIGTERM, handle_process_signal);

    auto provider = _state->services.build_provider ();
    std::vector<hosted_service_t *> started;
    try {
        _state->start_hosted_services (provider, started);
        while (!_state->stop_requested.load (std::memory_order_acquire)) {
            if (g_stop_signal_requested != 0) {
                _state->stop_requested.store (true, std::memory_order_release);
                break;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }
    catch (...) {
        _state->stop_hosted_services (started);
        detail::channel_runtime_t::from (_state->zlink.message_bus ()).shutdown ();
        detail::drain_zlink_builder_runtime (_state->zlink);
        runtime::shutdown_handler_coroutine_executor ();
        detail::shutdown_stream_dispatch_executor ();
        detail::shutdown_handler_invocation_executor ();
        provider.close ();
        throw;
    }

    const bool trace_enabled = host_stop_trace_enabled ();
    _state->stop_hosted_services (started);
    if (trace_enabled) {
        std::cerr << "zlink-cpp-host-stop stage=before-channel-runtime-shutdown" << std::endl;
    }
    detail::channel_runtime_t::from (_state->zlink.message_bus ()).shutdown ();
    if (trace_enabled) {
        std::cerr << "zlink-cpp-host-stop stage=after-channel-runtime-shutdown" << std::endl;
        std::cerr << "zlink-cpp-host-stop stage=before-drain-runtime" << std::endl;
    }
    detail::drain_zlink_builder_runtime (_state->zlink);
    if (trace_enabled) {
        std::cerr << "zlink-cpp-host-stop stage=after-drain-runtime" << std::endl;
        std::cerr << "zlink-cpp-host-stop stage=before-coroutine-executor-shutdown" << std::endl;
    }
    runtime::shutdown_handler_coroutine_executor ();
    if (trace_enabled) {
        std::cerr << "zlink-cpp-host-stop stage=after-coroutine-executor-shutdown" << std::endl;
        std::cerr << "zlink-cpp-host-stop stage=before-stream-executor-shutdown" << std::endl;
    }
    detail::shutdown_stream_dispatch_executor ();
    if (trace_enabled) {
        std::cerr << "zlink-cpp-host-stop stage=after-stream-executor-shutdown" << std::endl;
        std::cerr << "zlink-cpp-host-stop stage=before-handler-invocation-executor-shutdown"
                  << std::endl;
    }
    detail::shutdown_handler_invocation_executor ();
    if (trace_enabled) {
        std::cerr << "zlink-cpp-host-stop stage=after-handler-invocation-executor-shutdown"
                  << std::endl;
        std::cerr << "zlink-cpp-host-stop stage=before-provider-close" << std::endl;
    }
    provider.close ();
    if (trace_enabled) {
        std::cerr << "zlink-cpp-host-stop stage=after-provider-close" << std::endl;
    }
    return _state->stop_requested.load (std::memory_order_acquire) ? 0 : _state->exit_code;
}

void app_t::stop () noexcept
{
    _state->stop_requested.store (true, std::memory_order_release);
}

void app_t::request_stop () noexcept
{
    stop ();
}

} // namespace zlink::framework
