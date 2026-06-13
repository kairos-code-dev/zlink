/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework/contracts/configuration/app.hpp>

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/channels/channel_host_service.hpp"
#include "runtime/channels/channel_runtime.hpp"
#include "runtime/dispatch/coroutine_executor.hpp"
#include "runtime/http/http_host_service.hpp"
#include "runtime/spots/spot_runtime.hpp"
#include "runtime/streams/stream_host_service.hpp"

#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>
#include <typeindex>
#include <utility>
#include <vector>

namespace zlink::framework::detail
{

bool has_server_channel (const std::vector<channel_snapshot_t> &channels)
{
    for (const auto &channel : channels) {
        if (channel.server.enabled && !channel.server.bind_endpoints.empty ()) {
            return true;
        }
    }
    return false;
}

spot_actor_message_metadata_t project_stream_metadata (const stream_header_t &header)
{
    spot_actor_message_metadata_t metadata;
    for (const auto &[key, value] : header.metadata ().values ()) {
        metadata.values.emplace (key, value);
    }
    return metadata;
}

class app_state_t
{
  public:
    app_state_t () : metrics (monitoring) {}

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
};

} // namespace zlink::framework::detail

namespace
{

std::atomic<zlink::framework::detail::app_state_t *> g_active_app{nullptr};

void handle_process_signal (int) noexcept
{
    if (auto *state = g_active_app.load (std::memory_order_acquire)) {
        state->stop_requested.store (true, std::memory_order_release);
    }
}

} // namespace

namespace zlink::framework
{

app_t::app_t () : _state (std::make_unique<detail::app_state_t> ())
{
}

app_t::~app_t () = default;

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

app_t &app_advanced_t::use_zlink (std::function<void (zlink_builder_t &)> configure)
{
    configure (_app->_zlink_builder ());
    return *_app;
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
    _state->services.add_singleton<channel_client_t> (
      std::make_unique<channel_client_t> (_state->zlink.message_bus ()));
    zlink_framework_options_t options (_state->services, _state->handlers, _state->serializers,
                                       _state->zlink, _state->monitoring);
    if (configure) {
        configure (options);
    }
    const auto http_snapshot = options.http ().snapshot ();
    options.apply ();
    const auto channel_snapshot = _state->zlink.channels ();
    if (detail::has_server_channel (channel_snapshot)) {
        add_hosted_service (std::make_unique<runtime::channel_host_service_t> (
          _state->zlink.message_bus (), channel_snapshot, _state->handlers, _state->serializers));
    }
    const auto stream_snapshot = _state->zlink.streams ();
    for (const auto &spot_node : _state->zlink.spot_nodes ()) {
        if (!spot_node.actor_gateway_enabled) {
            continue;
        }
        auto runtime = detail::spot_node_runtime_t::from (_state->zlink, spot_node.name);
        if (!runtime) {
            continue;
        }
        if (spot_node.entry_spot_name) {
            try {
                (void) runtime->create_spot (*spot_node.entry_spot_name);
            }
            catch (const framework_exception_t &) {
            }
        }
        if (!_state->services.contains (std::type_index (typeid (detail::spot_node_runtime_t)))) {
            _state->services.add_singleton<detail::spot_node_runtime_t> (
              std::make_unique<detail::spot_node_runtime_t> (*runtime));
        }
        auto framework_provider = _state->services.build_provider ();
        auto &actor_gateway = framework_provider.get_required<detail::actor_gateway_runtime_t> ();
        runtime->on_destroy_actor ([&actor_gateway] (const actor_ref_t &actor_ref) {
            return actor_gateway.destroy_actor (actor_ref);
        });
        runtime->on_actor_ref_updated ([&actor_gateway] (const actor_ref_t &actor_ref) {
            return actor_gateway.update_actor_ref (actor_ref);
        });
        actor_gateway.on_relay (
          [runtime = *runtime, services = &_state->services, serializers = &_state->serializers] (
            const actor_ref_t &actor_ref, actor_context_t actor_context,
            const stream_header_t &header, const zlink::message_t &payload) mutable {
              auto provider = services->build_provider ();
              return runtime.relay_actor_packet (
                actor_ref, std::move (actor_context), header.packet_name (), payload, provider,
                *serializers, detail::project_stream_metadata (header));
          });
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
    g_active_app.store (_state.get (), std::memory_order_release);
    std::signal (SIGINT, handle_process_signal);
    std::signal (SIGTERM, handle_process_signal);

    auto provider = _state->services.build_provider ();
    std::vector<hosted_service_t *> started;
    try {
        _state->start_hosted_services (provider, started);
        while (!_state->stop_requested.load (std::memory_order_acquire)) {
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }
    catch (...) {
        _state->stop_hosted_services (started);
        provider.close ();
        auto *expected = _state.get ();
        g_active_app.compare_exchange_strong (expected, nullptr, std::memory_order_acq_rel);
        throw;
    }

    _state->stop_hosted_services (started);
    provider.close ();
    auto *expected = _state.get ();
    g_active_app.compare_exchange_strong (expected, nullptr, std::memory_order_acq_rel);
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
