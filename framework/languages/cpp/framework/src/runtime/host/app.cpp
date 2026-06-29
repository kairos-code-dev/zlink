/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework/contracts/configuration/app.hpp>

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/channels/channel_host_service.hpp"
#include "runtime/channels/channel_runtime.hpp"
#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/channels/route_channel_host_service.hpp"
#include "runtime/dispatch/coroutine_executor.hpp"
#include "runtime/host/actor_gateway_spot_bridge.hpp"
#include "runtime/host/framework_runtime.hpp"
#include "runtime/http/http_host_service.hpp"
#include "runtime/spots/spot_node_host_service.hpp"
#include "runtime/spots/spot_runtime.hpp"
#include "runtime/streams/stream_host_service.hpp"

#include <zlink/Contracts/Service/registry.hpp>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <chrono>
#include <memory>
#include <thread>
#include <typeindex>
#include <utility>
#include <vector>

namespace zlink::framework::detail
{

bool has_inbound_channel (const std::vector<channel_snapshot_t> &channels)
{
    for (const auto &channel : channels) {
        if (channel.server.enabled && !channel.server.bind_endpoints.empty ()) {
            return true;
        }
        if (channel.subscriber.enabled && !channel.subscriber.connect_endpoints.empty ()) {
            return true;
        }
    }
    return false;
}

class registry_host_service_t final : public hosted_service_t
{
  public:
    explicit registry_host_service_t (registry_options_snapshot_t options) :
        _options (std::move (options))
    {
    }

    void start (service_provider_t &) override
    {
        auto &registry = _runtime.registry ();
        registry.set_heartbeat (_options.heartbeat_interval, _options.heartbeat_timeout);
        registry.set_broadcast_interval (_options.broadcast_interval);
        for (const auto &peer : _options.peer_pub_endpoints) {
            registry.add_peer (peer);
        }
        registry.bind (_options.pub_endpoint, _options.router_endpoint);
    }

    void stop () noexcept override { _runtime.drain (); }

  private:
    registry_options_snapshot_t _options;
    runtime::framework_runtime_t _runtime;
};

class app_state_t
{
  public:
    app_state_t () : metrics (monitoring) {}
    ~app_state_t ()
    {
        hosted_services.clear ();
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
        channel_runtime_t::from (zlink.message_bus ()).shutdown ();
        for (auto it = started.rbegin (); it != started.rend (); ++it) {
            (*it)->stop ();
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
    detail::bind_zlink_monitoring (_state->zlink, _state->monitoring);
    detail::bind_stream_serializers (_state->zlink, _state->serializers);
    auto &actor_gateway_runtime =
      _state->services.build_provider ().get_required<detail::actor_gateway_runtime_t> ();
    actor_gateway_runtime.bind_serializers (_state->serializers);
    actor_gateway_runtime.set_dispatch (options.configure_dispatch ());
    detail::channel_runtime_t::from (_state->zlink.message_bus ())
      .bind_discovery (_state->zlink.discovery_options ());
    const auto registry_snapshot = _state->zlink.registry_options ();
    if (!registry_snapshot.pub_endpoint.empty () && !registry_snapshot.router_endpoint.empty ()) {
        add_hosted_service (std::make_unique<detail::registry_host_service_t> (registry_snapshot));
    }
    const auto channel_snapshot = _state->zlink.channels ();
    detail::channel_runtime_manager_t::from (_state->zlink)
      .initialize_route_channels (_state->zlink);
    if (detail::has_inbound_channel (channel_snapshot)) {
        add_hosted_service (std::make_unique<runtime::channel_host_service_t> (
          _state->zlink.message_bus (), channel_snapshot, _state->zlink.discovery_options (),
          _state->handlers, _state->serializers));
    }
    const auto stream_snapshot = _state->zlink.streams ();
    const auto spot_node_snapshot = _state->zlink.spot_nodes ();
    std::vector<runtime::spot_node_host_service_t::node_runtime_t> spot_node_runtimes;
    if (!spot_node_snapshot.empty ()) {
        for (const auto &spot_node : spot_node_snapshot) {
            auto runtime = detail::spot_node_runtime_t::from (_state->zlink, spot_node.name);
            if (runtime) {
                spot_node_runtimes.push_back (
                  runtime::spot_node_host_service_t::node_runtime_t{spot_node, *runtime});
            }
        }
        add_hosted_service (std::make_unique<runtime::spot_node_host_service_t> (
          spot_node_runtimes, _state->zlink.discovery_options ()));
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
          _state->zlink.message_bus (), _state->serializers, _state->zlink.registry_query (),
          _state->zlink.discovery_options (), std::move (route_spot_node_runtimes),
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
        add_hosted_service (std::make_unique<runtime::stream_host_service_t> (
          detail::stream_runtime_t::from (_state->zlink), stream_snapshot,
          options.stream_session_factories ()));
    }
    if (!http_snapshot.endpoints.empty ()) {
        add_hosted_service (
          std::make_unique<runtime::http_host_service_t> (http_snapshot, _state->health));
    }
    runtime::configure_handler_coroutine_executor (options.handler_coroutine_workers ());
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
        detail::drain_zlink_builder_runtime (_state->zlink);
        provider.close ();
        throw;
    }

    _state->stop_hosted_services (started);
    detail::drain_zlink_builder_runtime (_state->zlink);
    provider.close ();
    runtime::shutdown_handler_coroutine_executor ();
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
