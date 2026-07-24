/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/framework/contracts/configuration/app.hpp>

#include "runtime/actors/actor_client.hpp"
#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/channels/channel_host_service.hpp"
#include "runtime/channels/channel_runtime.hpp"
#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/diagnostics/monitoring_runtime.hpp"
#include "runtime/dispatch/coroutine_executor.hpp"
#include "runtime/host/framework_runtime.hpp"
#include "runtime/http/http_host_service.hpp"
#include "runtime/locations/in_memory_location_store.hpp"
#include "runtime/locations/live_location_reader.hpp"
#include "runtime/locations/location_auto_connect_host_service.hpp"
#include "runtime/locations/location_host_service.hpp"
#include "runtime/locations/location_lifecycle.hpp"
#include "runtime/locations/location_monitoring_host_service.hpp"
#include "runtime/mesh/mesh_node_host_service.hpp"
#include "runtime/mesh/route_mesh_runtime_service.hpp"
#include "runtime/mesh/route_mesh_runtime_options_service.hpp"
#include "runtime/locations/location_runtime.hpp"
#include "runtime/configuration/endpoint_connections.hpp"
#include "runtime/diagnostics/runtime_metrics.hpp"
#include "runtime/locations/store_location_resolvers.hpp"
#include "runtime/stateful/public_store_adapters.hpp"
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
    store_actor_directory_t (
      runtime::live_location_reader_t &store,
      std::shared_ptr<runtime::actor_location_observer_t> actor_locations,
      std::shared_ptr<std::string> actor_mesh_name) :
        _store (store), _actor_locations (std::move (actor_locations)),
        _actor_mesh_name (std::move (actor_mesh_name))
    {
    }

    task_t<std::optional<actor_ref_t>> find (std::string actor_id) override
    {
        auto row = _store
                     .resolve_actor (
                       actor_location_key_t{*_actor_mesh_name, std::move (actor_id)})
                     .result ();
        if (!row) {
            return task_t<std::optional<actor_ref_t>> (
              result_t<std::optional<actor_ref_t>>::failure (
                row.error_kind (),
                row.error () ? row.error ()->what () : "actor location lookup failed",
                row.error () && row.error ()->is_retriable ()));
        }
        if (!row.value () || !_actor_locations->accepts (*row.value ())) {
            return task_t<std::optional<actor_ref_t>> (
              result_t<std::optional<actor_ref_t>>::success (std::nullopt));
        }
        return task_t<std::optional<actor_ref_t>> (
          result_t<std::optional<actor_ref_t>>::success (row.value ()->actor_ref));
    }

    task_t<actor_ref_t> ensure (std::string, message_t, actor_placement_t) override
    {
        return task_t<actor_ref_t> (result_t<actor_ref_t>::failure (
          framework_error_kind_t::request_failed,
          "actor_directory_t::ensure is not configured for this framework host"));
    }

  private:
    runtime::live_location_reader_t &_store;
    std::shared_ptr<runtime::actor_location_observer_t> _actor_locations;
    std::shared_ptr<std::string> _actor_mesh_name;
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

zlink::routing_id_t location_owner_node_rid (
  const std::vector<std::shared_ptr<mesh_node_builder_state_t>> &mesh_nodes)
{
    for (const auto &node : mesh_nodes) {
        if (node && node->routing_id) {
            return *node->routing_id;
        }
    }
    return zlink::routing_id_t::from ("framework");
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
        for (auto it = started.rbegin (); it != started.rend (); ++it) {
            if (dynamic_cast<runtime::stream_host_service_t *> (*it) != nullptr) {
                stream_services.push_back (*it);
            } else if (dynamic_cast<runtime::http_host_service_t *> (*it) != nullptr) {
                http_services.push_back (*it);
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
        for (auto it = started.rbegin (); it != started.rend (); ++it) {
            if (dynamic_cast<runtime::stream_host_service_t *> (*it) != nullptr) {
                continue;
            }
            if (dynamic_cast<runtime::http_host_service_t *> (*it) != nullptr) {
                continue;
            }
            stop_service (*it);
        }
    }

    struct termination_waiter_t :
        public std::enable_shared_from_this<termination_waiter_t>
    {
        task_completion_source_t<termination_result_t> completion;
        std::atomic_bool completed = false;
        std::optional<std::stop_callback<std::function<void ()>>>
          cancellation;

        task_t<termination_result_t> task ()
        {
            return completion.task ();
        }

        void arm (std::stop_token token)
        {
            if (!token.stop_possible ())
                return;
            std::weak_ptr<termination_waiter_t> weak =
              shared_from_this ();
            cancellation.emplace (
              token, [weak] {
                  if (auto waiter = weak.lock ())
                      waiter->cancel ();
              });
        }

        void complete (termination_result_t result)
        {
            if (completed.exchange (true, std::memory_order_acq_rel))
                return;
            completion.complete (
              result_t<termination_result_t>::success (result));
        }

        void cancel ()
        {
            if (completed.exchange (true, std::memory_order_acq_rel))
                return;
            completion.complete (
              detail::boundary_failure<termination_result_t> (
                detail::boundary_error_t::cancelled,
                "termination waiter was cancelled"));
        }
    };

    struct termination_operation_t
    {
        std::mutex mutex;
        bool started = false;
        bool terminal = false;
        termination_intent_t effective_intent =
          termination_intent_t::shutdown;
        termination_result_t result{};
        std::chrono::milliseconds deadline{30000};
        std::vector<std::shared_ptr<termination_waiter_t>> waiters;
        std::thread worker;

        ~termination_operation_t ()
        {
            if (worker.joinable ()) {
                worker.join ();
            }
        }
    };

    std::shared_ptr<std::atomic_bool> draining = std::make_shared<std::atomic_bool> (false);
    std::atomic<framework_runtime_state_t> runtime_state =
      framework_runtime_state_t::preparing;
    termination_operation_t termination_operation;
    std::mutex termination_teardown_mutex;
    std::condition_variable termination_teardown_changed;
    bool run_active = false;
    bool teardown_complete = false;

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

zlink::framework::result_t<void>
one_way_native_submit_result (zlink::submit_result_t result, std::string_view operation)
{
    using namespace zlink::framework;
    switch (result) {
        case zlink::submit_result_t::ok:
            return result_t<void>::success ();
        case zlink::submit_result_t::backpressured:
            return result_t<void>::failure (
              framework_error_kind_t::worker_queue_full,
              std::string (operation) + " is backpressured", true);
        case zlink::submit_result_t::not_found:
        case zlink::submit_result_t::not_admitted:
            return result_t<void>::failure (
              framework_error_kind_t::request_target_not_found,
              std::string (operation) + " target was not found");
        case zlink::submit_result_t::not_connected:
            return result_t<void>::failure (
              framework_error_kind_t::route_not_connected,
              std::string (operation) + " route is not connected", true);
        case zlink::submit_result_t::terminated:
            return detail::boundary_failure<void> (
              detail::boundary_error_t::shutdown,
              std::string (operation) + " runtime is stopped");
        case zlink::submit_result_t::invalid_argument:
        case zlink::submit_result_t::invalid_handle:
        case zlink::submit_result_t::invalid_state:
            return result_t<void>::failure (
              framework_error_kind_t::request_protocol_error,
              std::string (operation) + " rejected an invalid call");
        default:
            return result_t<void>::failure (
              framework_error_kind_t::request_failed,
              std::string (operation) + " was not submitted");
    }
}

} // namespace

namespace zlink::framework
{

namespace
{
task_t<drain_result_t>
to_drain_task (task_t<termination_result_t> termination);
}

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
    if (_state->services.contains (
          std::type_index (typeid (relocation_store_t)))
        && !_state->services.contains (
          std::type_index (
            typeid (runtime::stateful::relocation_store_port_t)))) {
        _state->services.add_factory<
          runtime::stateful::relocation_store_port_t> (
          [] (service_provider_t &provider) {
              return std::unique_ptr<
                runtime::stateful::relocation_store_port_t> (
                std::make_unique<
                  runtime::stateful::public_relocation_store_adapter_t> (
                  provider.get_required<relocation_store_t> ()));
          },
          service_lifetime_t::singleton);
    }
    if (_state->services.contains (
          std::type_index (typeid (authority_store_t)))
        && !_state->services.contains (
          std::type_index (
            typeid (runtime::stateful::authority_relocation_port_t)))) {
        _state->services.add_factory<
          runtime::stateful::authority_relocation_port_t> (
          [] (service_provider_t &provider) {
              return std::unique_ptr<
                runtime::stateful::authority_relocation_port_t> (
                std::make_unique<
                  runtime::stateful::public_authority_store_adapter_t> (
                  provider.get_required<authority_store_t> ()));
          },
          service_lifetime_t::singleton);
    }
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
    if (!_state->services.contains (std::type_index (typeid (runtime::live_location_reader_t)))) {
        const auto location_options = options.location_options ();
        _state->services.add_factory<runtime::live_location_reader_t> (
          [location_options] (service_provider_t &provider) {
              return std::make_unique<runtime::live_location_reader_t> (
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
    const auto actor_location_observer =
      std::make_shared<runtime::actor_location_observer_t> ();
    const auto actor_mesh_name = std::make_shared<std::string> ();
    if (!_state->services.contains (
          std::type_index (typeid (runtime::store_location_resolvers_t)))) {
        const auto resolver_location_options = options.location_options ();
        _state->services.add_factory<runtime::store_location_resolvers_t> (
          [resolver_location_options, actor_location_observer] (service_provider_t &provider) {
              return std::make_unique<runtime::store_location_resolvers_t> (
                provider.get_required<runtime::live_location_reader_t> (), resolver_location_options,
                actor_location_observer);
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
    if (!_state->services.contains (std::type_index (typeid (spot_handle_resolver_t)))) {
        _state->services.add_factory<spot_handle_resolver_t> (
          [] (service_provider_t &provider) {
              return std::shared_ptr<spot_handle_resolver_t> (
                &provider.get_required<runtime::store_location_resolvers_t> (),
                [] (spot_handle_resolver_t *) noexcept {});
          },
          service_lifetime_t::singleton);
    }
    if (!_state->services.contains (std::type_index (typeid (actor_spot_handle_resolver_t)))) {
        _state->services.add_factory<actor_spot_handle_resolver_t> (
          [] (service_provider_t &provider) {
              return std::shared_ptr<actor_spot_handle_resolver_t> (
                &provider.get_required<runtime::store_location_resolvers_t> (),
                [] (actor_spot_handle_resolver_t *) noexcept {});
          },
          service_lifetime_t::singleton);
    }
    if (!_state->services.contains (std::type_index (typeid (runtime::spot_address_resolver_t)))) {
        _state->services.add_factory<runtime::spot_address_resolver_t> (
          [] (service_provider_t &provider) {
              return std::shared_ptr<runtime::spot_address_resolver_t> (
                &provider.get_required<runtime::store_location_resolvers_t> (),
                [] (runtime::spot_address_resolver_t *) noexcept {});
          },
          service_lifetime_t::singleton);
    }
    if (!_state->services.contains (std::type_index (typeid (runtime::actor_address_resolver_t)))) {
        _state->services.add_factory<runtime::actor_address_resolver_t> (
          [] (service_provider_t &provider) {
              return std::shared_ptr<runtime::actor_address_resolver_t> (
                &provider.get_required<runtime::store_location_resolvers_t> (),
                [] (runtime::actor_address_resolver_t *) noexcept {});
          },
          service_lifetime_t::singleton);
    }
    if (!_state->services.contains (std::type_index (typeid (actor_directory_t)))) {
        _state->services.add_factory<actor_directory_t> (
          [actor_location_observer, actor_mesh_name] (service_provider_t &provider) {
              return std::shared_ptr<actor_directory_t> (
                std::make_shared<detail::store_actor_directory_t> (
                  provider.get_required<runtime::live_location_reader_t> (),
                  actor_location_observer, actor_mesh_name));
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
          [location_options, actor_location_observer] (service_provider_t &provider) {
              return std::shared_ptr<location_runtime_query_t> (
                std::make_shared<runtime::store_location_runtime_query_t> (
                  provider.get_required<runtime::live_location_reader_t> (),
                  provider.get_required<runtime::location_runtime_t> (), location_options,
                  actor_location_observer));
          },
          service_lifetime_t::singleton);
    }
    detail::bind_zlink_monitoring (_state->zlink, _state->monitoring);
    detail::bind_stream_serializers (_state->zlink, _state->serializers);
    auto &actor_gateway_runtime =
      _state->services.build_provider ().get_required<detail::actor_gateway_runtime_t> ();
    _state->services.build_provider ()
      .get_required<runtime::location_runtime_t> ()
      .bind_monitoring (detail::monitoring_runtime_t::from (_state->monitoring).state ());
    actor_gateway_runtime.bind_serializers (_state->serializers);
    actor_gateway_runtime.set_dispatch (options.configure_dispatch ());
    auto channel_runtime = detail::channel_runtime_t::from (_state->zlink.message_bus ());
    const auto channel_snapshot = channel_runtime.channel_snapshots ();
    auto channel_runtime_manager = detail::channel_runtime_manager_t::from (_state->zlink);
    channel_runtime_manager.initialize_route_channels (_state->zlink);
    auto mesh_node_registrations =
      detail::mesh_node_runtime_t::registrations (_state->zlink);
    auto monitoring_state = detail::monitoring_runtime_t::from (_state->monitoring).state ();
    const auto application_mesh_registration =
      std::find_if (mesh_node_registrations.begin (),
                    mesh_node_registrations.end (),
                    [] (const auto &registration) {
                        return registration->spot_state
                                 ->snapshot.entry_spot_name.has_value ();
                    });
    const auto application_mesh_name =
      application_mesh_registration != mesh_node_registrations.end ()
        ? (*application_mesh_registration)->mesh_name
        : (mesh_node_registrations.empty ()
             ? std::string{}
             : mesh_node_registrations.front ()->mesh_name);
    *actor_mesh_name = application_mesh_name;
    {
        auto provider = _state->services.build_provider ();
        provider.get_required<runtime::store_location_resolvers_t> ()
          .set_actor_mesh_name (application_mesh_name);
        auto &location_lifecycle =
          provider.get_required<runtime::location_lifecycle_t> ();
        auto &spot_resolver =
          provider.get_required<runtime::spot_address_resolver_t> ();
        auto route_client = provider.get_required<route_client_t> ();
        for (const auto &registration : mesh_node_registrations) {
            registration->spot_state->dispatch = options.configure_dispatch ();
            registration->spot_state->monitoring = monitoring_state;
            detail::spot_node_runtime_t spot_runtime (registration->spot_state);
            spot_runtime.bind_location_lifecycle (location_lifecycle);
            spot_runtime.bind_spot_location_resolver (spot_resolver);
            spot_runtime.bind_drain_flag (_state->draining);
            spot_runtime.set_route_client (route_client);
        }
    }
    if (!mesh_node_registrations.empty ()
        && !_state->services.contains (std::type_index (typeid (spot_manager_t)))) {
        _state->services.add_singleton<spot_manager_t> (
          std::make_unique<spot_manager_t> (
            detail::spot_node_runtime_t (
              application_mesh_registration != mesh_node_registrations.end ()
                ? (*application_mesh_registration)->spot_state
                : mesh_node_registrations.front ()->spot_state)
              .manager ()));
    }
    if (!mesh_node_registrations.empty ()
        && !_state->services.contains (
          std::type_index (typeid (spot_publisher_client_t)))) {
        auto provider = _state->services.build_provider ();
        _state->services.add_singleton<spot_publisher_client_t> (
          std::make_unique<spot_publisher_client_t> (
            provider.get_required<spot_manager_t> (),
            _state->serializers));
    }
    const auto location_owner = detail::location_owner_node_rid (mesh_node_registrations);
    add_hosted_service (
      std::make_unique<runtime::location_host_service_t> (
        location_owner));
    std::vector<std::shared_ptr<detail::mesh_node_runtime_t>> mesh_nodes;
    runtime::mesh_node_host_service_t *mesh_node_service = nullptr;
    if (!mesh_node_registrations.empty ()) {
        auto mesh_service = std::make_unique<runtime::mesh_node_host_service_t> (
          std::move (mesh_node_registrations), _state->serializers,
          options.dispatch_options ());
        mesh_node_service = mesh_service.get ();
        mesh_nodes = mesh_service->nodes ();
        add_hosted_service (std::move (mesh_service));
    }
    if (!mesh_nodes.empty ()
        && !_state->services.contains (
          std::type_index (typeid (route_mesh_runtime_t)))) {
        auto provider = _state->services.build_provider ();
        auto location_runtime = provider.get<location_runtime_query_t> ();
        auto mesh_runtime =
          std::make_shared<runtime::route_mesh_runtime_service_t> (
            mesh_nodes,
            location_runtime ? &location_runtime->get () : nullptr,
            [this] (std::chrono::milliseconds deadline) {
                return to_drain_task (shutdown (deadline));
            },
            [this] { return to_drain_task (shutdown ()); });
        _state->services.add_factory<route_mesh_runtime_t> (
          [mesh_runtime] (service_provider_t &) {
              return std::static_pointer_cast<route_mesh_runtime_t> (mesh_runtime);
          },
          service_lifetime_t::singleton);
        add_hosted_service (
          std::make_unique<runtime::route_mesh_runtime_host_service_t> (
            std::move (mesh_runtime)));
    }
    if (!mesh_nodes.empty ()
        && !_state->services.contains (
          std::type_index (typeid (route_mesh_runtime_options_t)))) {
        _state->services.add_singleton<route_mesh_runtime_options_t> (
          std::make_unique<runtime::route_mesh_runtime_options_service_t> (
            mesh_nodes));
    }
    const auto spot_router_channels = options.location_options ().spot_router_channels;
    for (const auto &mesh : mesh_nodes) {
        const auto source_spot_rid = zlink::routing_id_t::from (
          mesh->routing_id ()->to_string () + ":__zlink-route-origin");
        auto send_to_spot = [mesh, source_spot_rid] (
            const zlink::routing_id_t &target_node,
            const zlink::routing_id_t &target_spot,
            std::uint64_t target_spot_generation,
            runtime::messaging::message_parts_t parts) {
              const auto submitted = mesh->send_to_spot (
                source_spot_rid, target_node, target_spot,
                target_spot_generation, parts.items ());
              return one_way_native_submit_result (submitted, "MeshNode Spot send");
          };
        auto request_to_spot = [mesh, source_spot_rid] (
            const zlink::routing_id_t &target_node,
            const zlink::routing_id_t &target_spot,
            std::uint64_t target_spot_generation,
            runtime::messaging::message_parts_t parts,
            std::chrono::milliseconds timeout) {
              detail::host::operation_id_t operation;
              const auto submitted = mesh->request_to_spot (
                source_spot_rid, target_node, target_spot, target_spot_generation,
                parts.items (), operation, timeout);
              if (submitted != zlink::submit_result_t::ok) {
                  return result_t<runtime::messaging::message_parts_t>::failure (
                    framework_error_kind_t::route_not_connected,
                    "MeshNode Spot request was not submitted");
              }
              auto completion = mesh->wait_for_completion (operation, timeout);
              if (!completion) {
                  return detail::propagate_failure<
                    runtime::messaging::message_parts_t> (
                    completion, "MeshNode Spot request failed");
              }
              if (completion.value ().record.terminal_result
                  != static_cast<int> (zlink::request_result_t::ok)) {
                  return result_t<runtime::messaging::message_parts_t>::failure (
                    framework_error_kind_t::request_failed,
                    "MeshNode '" + mesh->mesh_name ()
                      + "' Spot request returned terminal result "
                      + std::to_string (completion.value ().record.terminal_result));
              }
              return result_t<runtime::messaging::message_parts_t>::success (
                runtime::messaging::message_parts_t (
                  std::move (completion.value ().parts)));
          };
        const auto mesh_name = mesh->mesh_name ();
        const auto claimed_as_route_alias =
          std::any_of (spot_router_channels.begin (), spot_router_channels.end (),
                       [&mesh_name] (const auto &mapping) {
                           return mapping.first != mesh_name && mapping.second == mesh_name;
                       });
        if (!claimed_as_route_alias) {
            channel_runtime.bind_spot_mesh_transport (
              mesh_name, send_to_spot, request_to_spot);
        }
        if (const auto route = spot_router_channels.find (mesh_name);
            route != spot_router_channels.end () && route->second != mesh_name) {
            /* SpotHandle keeps the configured routing alias opaque. RouteMesh
             * owns the physical MeshNode, so that alias must select the same
             * node instead of a different MeshNode that happens to use the
             * alias as its MeshName. */
            channel_runtime.bind_spot_mesh_transport (
              route->second, std::move (send_to_spot), std::move (request_to_spot));
        }
        channel_runtime.bind_mesh_node_transport (
          mesh_name,
          [mesh, mesh_node_service] (const zlink::routing_id_t &target,
                                    runtime::messaging::message_parts_t parts) {
              const auto local_rid = mesh->routing_id ();
              const auto submitted = local_rid && *local_rid == target
                                       ? mesh_node_service->submit_local_node_send (
                                           mesh, parts.items ())
                                       : mesh->send_to_node (target, parts.items ());
              return one_way_native_submit_result (submitted, "MeshNode send");
          },
          [mesh] (const zlink::routing_id_t &target,
                  runtime::messaging::message_parts_t parts,
                  std::chrono::milliseconds timeout) {
              detail::host::operation_id_t operation;
              const auto submitted =
                mesh->request_to_node (target, parts.items (), operation, timeout);
              if (submitted != zlink::submit_result_t::ok) {
                  return result_t<runtime::messaging::message_parts_t>::failure (
                    framework_error_kind_t::route_not_connected,
                    "MeshNode request was not submitted");
              }
              auto completion = mesh->wait_for_completion (operation, timeout);
              if (!completion) {
                  return detail::propagate_failure<
                    runtime::messaging::message_parts_t> (
                    completion, "MeshNode request failed");
              }
              if (completion.value ().record.terminal_result
                  != static_cast<int> (zlink::request_result_t::ok)) {
                  return result_t<runtime::messaging::message_parts_t>::failure (
                    framework_error_kind_t::request_failed,
                    "MeshNode request returned a terminal error");
              }
              return result_t<runtime::messaging::message_parts_t>::success (
                runtime::messaging::message_parts_t (
                  std::move (completion.value ().parts)));
          });
        for (const auto &[channel_name, weight] : mesh->channel_weights ()) {
            (void) weight;
            channel_runtime.bind_mesh_channel_transport (
              channel_name,
              [mesh, channel_name] (runtime::messaging::message_parts_t parts) {
                  const auto submitted = mesh->send_to_channel (channel_name, parts.items ());
                  return one_way_native_submit_result (submitted,
                                                       "RouteMesh channel send");
              },
              [mesh, channel_name] (runtime::messaging::message_parts_t parts,
                                    std::chrono::milliseconds timeout) {
                  detail::host::operation_id_t operation;
                  const auto submitted = mesh->request_to_channel (
                    channel_name, parts.items (), operation, timeout);
                  if (submitted != zlink::submit_result_t::ok) {
                      return result_t<runtime::messaging::message_parts_t>::failure (
                        framework_error_kind_t::route_not_connected,
                        "RouteMesh channel request was not submitted");
                  }
                  auto completion = mesh->wait_for_completion (operation, timeout);
                  if (!completion) {
                      return detail::propagate_failure<
                        runtime::messaging::message_parts_t> (
                        completion, "RouteMesh channel request failed");
                  }
                  if (completion.value ().record.terminal_result
                      != static_cast<int> (zlink::request_result_t::ok)) {
                      return result_t<runtime::messaging::message_parts_t>::failure (
                        framework_error_kind_t::request_failed,
                        "RouteMesh channel request returned a terminal error");
                  }
                  return result_t<runtime::messaging::message_parts_t>::success (
                    runtime::messaging::message_parts_t (
                      std::move (completion.value ().parts)));
              });
        }
    }
    if (!mesh_nodes.empty ()) {
        const auto application_mesh_it =
          std::find_if (mesh_nodes.begin (), mesh_nodes.end (),
                        [&] (const auto &mesh) {
                            return mesh->mesh_name () == application_mesh_name;
                        });
        const auto application_mesh =
          application_mesh_it != mesh_nodes.end () ? *application_mesh_it
                                                   : mesh_nodes.front ();
        const auto request_timeout = std::chrono::seconds (30);
        actor_gateway_runtime.on_create (
          [application_mesh, request_timeout] (
            std::string actor_type,
            std::string actor_id,
            const std::optional<zlink::message_t> &creation_payload) {
              return application_mesh->create_application_actor (
                std::move (actor_type), std::move (actor_id), creation_payload,
                request_timeout);
          });
        actor_gateway_runtime.on_join_entry_spot (
          [application_mesh, request_timeout] (
            const actor_ref_t &actor,
            node_rid_t target_node,
            const zlink::message_t &request) {
              return application_mesh->join_application_actor_to_entry_spot (
                actor, target_node, request, request_timeout);
          });
        actor_gateway_runtime.on_join_spot (
          [application_mesh, actor_gateway_runtime, live_locations =
             &_state->services.build_provider ()
                .get_required<runtime::live_location_reader_t> (),
           request_timeout] (
            const actor_ref_t &actor,
            spot_rid_t target_spot,
            const zlink::message_t &request) {
              const auto deadline =
                std::chrono::steady_clock::now () + request_timeout;
              result_t<std::optional<spot_location_t>> located =
                result_t<std::optional<spot_location_t>>::success (std::nullopt);
              do {
                  located =
                    live_locations
                      ->resolve_spot (spot_location_key_t{
                        application_mesh->mesh_name (),
                        zlink::routing_id_t::from (
                          std::string (target_spot.value ()))})
                      .result ();
                  if (located && located.value ())
                      break;
                  std::this_thread::sleep_for (std::chrono::milliseconds (50));
              } while (std::chrono::steady_clock::now () < deadline);
              if (!located) {
                  return result_t<detail::actor_join_reply_t>::failure (
                    located.error_kind (),
                    located.error () ? located.error ()->what ()
                                     : "target Spot location lookup failed",
                    located.error () && located.error ()->is_retriable ());
              }
              if (!located.value ()) {
                  return result_t<detail::actor_join_reply_t>::failure (
                    framework_error_kind_t::spot_route_not_found,
                    "target Spot location was not found");
              }
              const auto &target = *located.value ();
              if (target.spot_generation == 0) {
                  return result_t<detail::actor_join_reply_t>::failure (
                    framework_error_kind_t::spot_route_not_found,
                    "target Spot lifecycle generation was not published");
              }
              const auto bound_session =
                actor_gateway_runtime.bound_session_route (actor);
              return application_mesh->join_application_actor_to_spot (
                actor, node_rid_t::from_string (target.node_rid.to_string ()),
                target_spot, target.spot_generation,
                request, request_timeout,
                bound_session
                  ? std::make_optional (bound_session->node_rid)
                  : std::nullopt,
                bound_session ? bound_session->session_rid : std::nullopt);
          });
        actor_gateway_runtime.on_relay (
          [application_mesh, live_locations =
             &_state->services.build_provider ()
                .get_required<runtime::live_location_reader_t> (),
           request_timeout] (
            const actor_ref_t &actor,
            actor_context_t,
            const detail::stream_header_t &header,
            const zlink::message_t &payload) {
              auto routed_actor = actor;
              auto located =
                live_locations->resolve_actor (
                  actor_location_key_t{application_mesh->mesh_name (),
                                       std::string (actor.actor_id ())})
                  .result ();
              if (located && located.value () && !located.value ()->actor_ref.empty ()
                  && located.value ()->actor_ref.generation () == actor.generation ()) {
                  routed_actor = located.value ()->actor_ref;
              }
              return application_mesh->relay_application_actor (
                routed_actor, header, payload, request_timeout);
          });
        actor_gateway_runtime.on_disconnect (
          [mesh_nodes, request_timeout] (const actor_ref_t &actor) {
              bool notified = false;
              result_t<void> last = result_t<void>::failure (
                framework_error_kind_t::spot_route_not_found,
                "Actor disconnect RouteMesh was not found");
              for (const auto &mesh : mesh_nodes) {
                  last = mesh->notify_application_actor_disconnected (
                    actor, actor.node_rid (), request_timeout);
                  notified = notified || static_cast<bool> (last);
              }
              return notified ? result_t<void>::success () : last;
          });
        const auto stream_runtime = detail::stream_runtime_t::from (_state->zlink);
        actor_gateway_runtime.on_bound_session_send (
          [application_mesh, stream_runtime] (
            const actor_ref_t &actor,
            std::uint64_t expected_binding_generation,
            const detail::stream_header_t &header,
            const zlink::message_t &payload) {
              auto encoded_header = stream_runtime.encode_header (header);
              if (!encoded_header) {
                  return result_t<void>::failure (
                    encoded_header.error_kind (),
                    encoded_header.error () ? encoded_header.error ()->what ()
                                            : "bound session header encode failed");
              }
              const auto payload_bytes = payload.to_bytes ();
              const auto header_size = encoded_header.value ().size ();
              std::vector<std::uint8_t> frame;
              frame.reserve (6 + header_size + payload_bytes.size ());
              frame.push_back (static_cast<std::uint8_t> ((header_size >> 8) & 0xff));
              frame.push_back (static_cast<std::uint8_t> (header_size & 0xff));
              frame.push_back (
                static_cast<std::uint8_t> ((payload_bytes.size () >> 24) & 0xff));
              frame.push_back (
                static_cast<std::uint8_t> ((payload_bytes.size () >> 16) & 0xff));
              frame.push_back (
                static_cast<std::uint8_t> ((payload_bytes.size () >> 8) & 0xff));
              frame.push_back (static_cast<std::uint8_t> (payload_bytes.size () & 0xff));
              frame.insert (frame.end (), encoded_header.value ().begin (),
                            encoded_header.value ().end ());
              frame.insert (frame.end (), payload_bytes.begin (), payload_bytes.end ());
              const std::vector<zlink::message_t> parts{
                zlink::message_t::from (frame)};
              const auto submitted =
                application_mesh->send_actor_bound_session (
                  actor, expected_binding_generation, parts);
              return one_way_native_submit_result (
                submitted, "Framework actor bound session send");
          });
    }
    if (!_state->services.contains (std::type_index (typeid (actor_client_t)))) {
        _state->services.add_factory<actor_client_t> (
          [mesh_nodes,
           actor_location_observer] (service_provider_t &provider) mutable {
              return runtime::make_actor_client (
                provider.get_required<runtime::live_location_reader_t> (),
                provider.get_required<serializer_registry_t> (), mesh_nodes,
                actor_location_observer);
          },
          service_lifetime_t::singleton);
    }
    /* endpoint_connections live attach (CONN-001): client-channel handles
     * mutate the runtime connection bundle from now on; disconnects apply to
     * the same set the requests iterate. */
    {
        auto channel_state = detail::channel_runtime_t::from (_state->zlink.message_bus ());
        for (auto &[connections_channel, connections] : options.client_endpoint_connections ()) {
            detail::endpoint_connections_runtime_t::attach (
              connections,
              [channel_state, connections_channel] (const std::string &endpoint) mutable {
                  channel_state.add_client_manual_connection (connections_channel, endpoint);
              },
              [channel_state, connections_channel] (const std::string &endpoint) mutable {
                  channel_state.remove_client_manual_connection (connections_channel, endpoint);
              });
        }
        for (auto &[connections_channel, connections] :
             options.subscriber_endpoint_connections ()) {
            detail::endpoint_connections_runtime_t::attach (
              connections,
              [channel_state, connections_channel] (const std::string &endpoint) mutable {
                  channel_state.add_subscriber_manual_connection (connections_channel, endpoint);
              },
              [channel_state, connections_channel] (const std::string &endpoint) mutable {
                  channel_state.remove_subscriber_manual_connection (connections_channel,
                                                                     endpoint);
              });
        }
    }
    if (!monitoring_state->location_sources.empty ()) {
        add_hosted_service (
          std::make_unique<runtime::location_monitoring_host_service_t> (monitoring_state));
    }
    const auto stream_snapshot = detail::stream_runtime_t::from (_state->zlink).snapshots ();
    detail::validate_monitoring_sources (_state->monitoring, channel_snapshot, {});
    add_hosted_service (std::make_unique<runtime::location_auto_connect_host_service_t> (
      _state->zlink.message_bus (), channel_snapshot, _state->handlers,
      _state->serializers, options.route_mesh_client_channels (), mesh_nodes));
    if (detail::has_inbound_channel (channel_snapshot)) {
        add_hosted_service (std::make_unique<runtime::channel_host_service_t> (
          _state->zlink.message_bus (), channel_snapshot, _state->handlers, _state->serializers));
    }
    if (!stream_snapshot.empty ()) {
        detail::configure_stream_dispatch_executor ();
        auto stream_service = std::make_unique<runtime::stream_host_service_t> (
          detail::stream_runtime_t::from (_state->zlink), stream_snapshot,
          options.stream_session_factories (),
          mesh_nodes.empty () ? nullptr : mesh_nodes.front ());
        stream_service->bind_drain_flag (_state->draining);
        stream_service->bind_monitoring (
          detail::monitoring_runtime_t::from (_state->monitoring).state ());
        add_hosted_service (std::move (stream_service));
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
    {
        std::lock_guard lock (_state->termination_teardown_mutex);
        if (_state->runtime_state.load (std::memory_order_acquire)
            == framework_runtime_state_t::stopped)
            return 0;
        _state->run_active = true;
        _state->teardown_complete = false;
    }
    std::vector<hosted_service_t *> started;
    try {
        _state->start_hosted_services (provider, started);
        auto expected = framework_runtime_state_t::preparing;
        (void) _state->runtime_state.compare_exchange_strong (
          expected, framework_runtime_state_t::serving,
          std::memory_order_acq_rel);
        try {
            runtime::runtime_metrics_t drain_metrics (
              detail::monitoring_runtime_t::from (_state->monitoring).state ());
            if (drain_metrics.enabled ()) {
                drain_metrics.observable ("zlink.drain.state", "{state}", 1,
                                          {{"state", "serving"}});
            }
        }
        catch (...) {
        }
        while (!_state->stop_requested.load (std::memory_order_acquire)) {
            if (g_stop_signal_requested != 0) {
                g_stop_signal_requested = 0;
                (void) shutdown ();
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
    }
    catch (...) {
        _state->runtime_state.store (
          framework_runtime_state_t::error, std::memory_order_release);
        _state->stop_hosted_services (started);
        detail::channel_runtime_t::from (_state->zlink.message_bus ()).shutdown ();
        detail::drain_zlink_builder_runtime (_state->zlink);
        runtime::shutdown_handler_coroutine_executor ();
        detail::shutdown_stream_dispatch_executor ();
        detail::shutdown_handler_invocation_executor ();
        provider.close ();
        {
            std::lock_guard lock (_state->termination_teardown_mutex);
            _state->run_active = false;
            _state->teardown_complete = true;
        }
        _state->termination_teardown_changed.notify_all ();
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
    {
        std::lock_guard lock (_state->termination_teardown_mutex);
        _state->run_active = false;
        _state->teardown_complete = true;
    }
    _state->termination_teardown_changed.notify_all ();
    _state->runtime_state.store (
      framework_runtime_state_t::stopped, std::memory_order_release);
    return _state->stop_requested.load (std::memory_order_acquire) ? 0 : _state->exit_code;
}

namespace
{

const char *drain_state_name (drain_state_t state) noexcept
{
    switch (state) {
        case drain_state_t::serving:
            return "serving";
        case drain_state_t::draining:
            return "draining";
        case drain_state_t::drained:
            return "drained";
        case drain_state_t::force_stopping:
            return "force_stopping";
    }
    return "unknown";
}

task_t<drain_result_t>
to_drain_task (task_t<termination_result_t> termination)
{
    auto source =
      std::make_shared<detail::task_completion_source_t<drain_result_t>> ();
    auto output = source->task ();
    auto observed =
      std::make_shared<task_t<termination_result_t>> (
        std::move (termination));
    detail::observe_task_completion (
      *observed,
      [source, observed] (const result_t<termination_result_t> &result) {
          if (!result) {
              source->complete (
                detail::propagate_failure<drain_result_t> (
                  result, "shutdown failed"));
              return;
          }
          const auto terminal = result.value ();
          if (terminal.outcome == termination_outcome_t::stopped) {
              source->complete (
                result_t<drain_result_t>::success (drained_t{}));
              return;
          }
          drain_force_reason_t reason =
            drain_force_reason_t::teardown_failed;
          if (terminal.reason == termination_reason_t::deadline_exceeded)
              reason = drain_force_reason_t::deadline_exceeded;
          else if (terminal.reason
                   == termination_reason_t::relocation_failed)
              reason = drain_force_reason_t::relocation_failed;
          source->complete (
            result_t<drain_result_t>::success (
              force_stopped_t{reason}));
      });
    return output;
}

} // namespace

bool app_t::is_ready () const noexcept
{
    return runtime_state () == framework_runtime_state_t::serving;
}

framework_runtime_state_t app_t::runtime_state () const noexcept
{
    return _state->runtime_state.load (std::memory_order_acquire);
}

task_t<drain_result_t> app_t::await_drained ()
{
    return to_drain_task (shutdown ());
}

task_t<drain_result_t> app_t::drain ()
{
    return to_drain_task (shutdown ());
}

task_t<drain_result_t> app_t::drain (std::chrono::milliseconds deadline)
{
    return to_drain_task (shutdown (deadline));
}

task_t<termination_result_t> app_t::retire (
  std::chrono::milliseconds deadline,
  std::stop_token wait_cancellation)
{
    return terminate (
      termination_intent_t::retire, deadline, wait_cancellation);
}

task_t<termination_result_t> app_t::shutdown (
  std::chrono::milliseconds deadline,
  std::stop_token wait_cancellation)
{
    return terminate (
      termination_intent_t::shutdown, deadline, wait_cancellation);
}

task_t<termination_result_t> app_t::terminate (
  termination_intent_t intent,
  std::chrono::milliseconds deadline,
  std::stop_token wait_cancellation)
{
    if (deadline <= std::chrono::milliseconds::zero ()) {
        throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                     "termination deadline must be greater than zero");
    }
    std::optional<termination_reason_t> blocker;
    if (intent == termination_intent_t::retire) {
        if (runtime_state () != framework_runtime_state_t::serving) {
            blocker = termination_reason_t::runtime_not_ready;
        } else if (
          !_state->services.contains (
            std::type_index (typeid (relocation_store_t)))
          || !_state->services.contains (
            std::type_index (typeid (authority_store_t)))
          || !_state->services.contains (
            std::type_index (typeid (object_creation_store_t)))) {
            blocker = termination_reason_t::store_unavailable;
        } else {
            blocker = termination_reason_t::target_unavailable;
        }
    }
    auto &operation = _state->termination_operation;
    std::shared_ptr<detail::app_state_t::termination_waiter_t> waiter;
    task_t<termination_result_t> task (
      result_t<termination_result_t>::success ({}));
    {
        std::lock_guard lock (operation.mutex);
        if (operation.terminal) {
            return task_t<termination_result_t> (
              result_t<termination_result_t>::success (
                operation.result));
        }
        if (!operation.started) {
            if (blocker) {
                return task_t<termination_result_t> (
                  result_t<termination_result_t>::success (
                    {termination_intent_t::retire,
                     termination_outcome_t::blocked,
                     *blocker}));
            }
            operation.started = true;
            operation.effective_intent = intent;
            operation.deadline = deadline;
            _state->draining->store (true, std::memory_order_release);
            _state->runtime_state.store (
              intent == termination_intent_t::retire
                ? framework_runtime_state_t::retiring
                : framework_runtime_state_t::draining,
              std::memory_order_release);
            auto *state = _state.get ();
            operation.worker =
              std::thread ([state] { run_shared_termination (*state); });
        }
        waiter =
          std::make_shared<detail::app_state_t::termination_waiter_t> ();
        task = waiter->task ();
        operation.waiters.push_back (waiter);
    }
    waiter->arm (wait_cancellation);
    return task;
}

void app_t::run_shared_termination (
  detail::app_state_t &state) noexcept
{
    const auto started_at = std::chrono::steady_clock::now ();
    const auto deadline_at =
      started_at + state.termination_operation.deadline;
    const auto effective_intent =
      state.termination_operation.effective_intent;
    auto publisher = state.monitoring.publisher ();
    auto emit_state = [&] (drain_state_t drain_state) {
        try {
            publisher.publish (drain_event_t{runtime_event_base_t{"drain"}, drain_state});
            runtime::runtime_metrics_t drain_metrics (
              detail::monitoring_runtime_t::from (state.monitoring).state ());
            if (drain_metrics.enabled ()) {
                drain_metrics.observable ("zlink.drain.state", "{state}", 1,
                                          {{"state", drain_state_name (drain_state)}});
            }
        }
        catch (...) {
        }
    };

    emit_state (drain_state_t::draining);

    drain_result_t result = drained_t{};
    bool force_state_emitted = false;
    auto force = [&] (drain_force_reason_t reason) {
        if (!std::holds_alternative<drained_t> (result))
            return;
        result = force_stopped_t{reason};
        if (!force_state_emitted) {
            force_state_emitted = true;
            emit_state (drain_state_t::force_stopping);
        }
    };

    std::vector<runtime::mesh_node_host_service_t *> mesh_services;
    for (const auto &service : state.hosted_services) {
        if (auto *mesh = dynamic_cast<runtime::mesh_node_host_service_t *> (service.get ())) {
            mesh->seal_application_dispatch ();
            mesh_services.push_back (mesh);
        }
    }

    bool marker_published = false;
    try {
        auto provider = state.services.build_provider ();
        if (auto location_runtime = provider.get<runtime::location_runtime_t> ()) {
            location_runtime->get ().set_draining (true);
            marker_published = location_runtime->get ().republish_peer_rows_draining ();
        } else {
            marker_published = true; // no location runtime: nothing to publish
        }
    }
    catch (...) {
        marker_published = false;
    }

    if (!marker_published) {
        while (std::chrono::steady_clock::now () < deadline_at && !marker_published) {
            std::this_thread::sleep_until (
              std::min (deadline_at, std::chrono::steady_clock::now ()
                                       + std::chrono::milliseconds (100)));
            try {
                auto provider = state.services.build_provider ();
                if (auto location_runtime = provider.get<runtime::location_runtime_t> ()) {
                    marker_published =
                      location_runtime->get ().republish_peer_rows_draining ();
                }
            }
            catch (...) {
            }
        }
        if (!marker_published)
            force (drain_force_reason_t::teardown_failed);
    }

    /* Keep the typed marker and owner lease observable until every polling
     * consumer has had one bounded opportunity to exclude this node from new
     * assignments. Existing auto-connect sockets remain established. */
    if (effective_intent == termination_intent_t::retire
        && std::holds_alternative<drained_t> (result)) {
        const bool has_auto_connect =
          std::any_of (state.hosted_services.begin (), state.hosted_services.end (),
                       [] (const auto &service) {
                           return dynamic_cast<runtime::location_auto_connect_host_service_t *> (
                                    service.get ())
                                  != nullptr;
                       });
        if (has_auto_connect) {
            try {
                auto provider = state.services.build_provider ();
                auto &location_runtime =
                  provider.get_required<runtime::location_runtime_t> ();
                const auto propagation_bound =
                  location_runtime.options ().polling_interval + std::chrono::seconds (5)
                  + std::chrono::milliseconds (100);
                std::cerr << "zlink drain propagation bound polling_ms="
                          << location_runtime.options ().polling_interval.count ()
                          << " store_read_timeout_ms=5000 scheduler_jitter_ms=100 total_ms="
                          << propagation_bound.count () << std::endl;
                if (std::chrono::steady_clock::now () + propagation_bound > deadline_at) {
                    std::this_thread::sleep_until (deadline_at);
                    force (drain_force_reason_t::deadline_exceeded);
                } else {
                    std::this_thread::sleep_for (propagation_bound);
                }
            }
            catch (...) {
                force (drain_force_reason_t::teardown_failed);
            }
        }
    }

    /* Admission is sealed before this barrier. Each callback accepted before
     * the seal owns a pending/active count until its terminal reply or send
     * completion, so a normal request completion cannot close its Spot. */
    if (std::holds_alternative<drained_t> (result)) {
        auto outbound_pending = [&state] () -> bool {
            try {
                return detail::channel_runtime_t::from (state.zlink.message_bus ())
                         .pending_count () > 0;
            }
            catch (...) {
            }
            return false;
        };
        while (outbound_pending () && std::chrono::steady_clock::now () < deadline_at)
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
        if (outbound_pending ()) {
            force (drain_force_reason_t::deadline_exceeded);
        } else {
            for (auto *mesh : mesh_services) {
                if (!mesh->wait_for_accepted_callbacks_until (deadline_at)) {
                    force (drain_force_reason_t::deadline_exceeded);
                    break;
                }
            }
        }
    }

    if (effective_intent == termination_intent_t::retire
        && std::holds_alternative<drained_t> (result)) {
        bool actors_completed = false;
        try {
            auto provider = state.services.build_provider ();
            while (std::chrono::steady_clock::now () < deadline_at) {
                bool pass_completed = true;
                auto peers = provider.get<peer_location_resolver_t> ();
                for (auto *mesh_service : mesh_services) {
                    for (const auto &node : mesh_service->nodes ()) {
                        auto spot_runtime = detail::spot_node_runtime_t::from (
                          state.zlink, node->mesh_name ());
                        if (!spot_runtime)
                            continue;
                        const auto actors = spot_runtime->local_actor_refs ();
                        if (actors.empty ())
                            continue;
                        if (!peers) {
                            pass_completed = false;
                            continue;
                        }
                        std::vector<peer_location_t> live;
                        try {
                            live = peers->get ()
                                     .list_live_peers (peer_location_filter_t{
                                       .auto_connect_type =
                                         location_auto_connect_type_t::spot_mesh,
                                       .mesh_name = node->mesh_name (),
                                       .role = location_role_t::spot})
                                     .result ()
                                     .value ();
                        }
                        catch (...) {
                            pass_completed = false;
                            continue;
                        }
                        const auto local_rid = node->routing_id ();
                        for (const auto &actor : actors) {
                            const auto capability =
                              "actor:" + std::string (actor.actor_type ());
                            const auto target = std::find_if (
                              live.begin (), live.end (), [&] (const auto &peer) {
                                  return !peer.draining && peer.node_rid
                                         && (!local_rid
                                             || peer.node_rid->to_hex ()
                                                  != local_rid->to_hex ())
                                         && std::find (peer.capabilities.begin (),
                                                       peer.capabilities.end (), capability)
                                              != peer.capabilities.end ();
                              });
                            if (target == live.end ()) {
                                pass_completed = false;
                                continue;
                            }
                            const auto now = std::chrono::steady_clock::now ();
                            if (now >= deadline_at) {
                                pass_completed = false;
                                break;
                            }
                            const auto remaining =
                              std::max (std::chrono::milliseconds (1),
                                        std::chrono::duration_cast<std::chrono::milliseconds> (
                                          deadline_at - now));
                            auto moved = node->join_application_actor_to_entry_spot (
                              actor,
                              node_rid_t::from_string (target->node_rid->to_string ()),
                              zlink::message_t{}, remaining);
                            if (!moved || moved.value ().result_code != 0)
                                pass_completed = false;
                        }
                    }
                }
                if (pass_completed) {
                    actors_completed = true;
                    break;
                }
                std::this_thread::sleep_until (
                  std::min (deadline_at, std::chrono::steady_clock::now ()
                                           + std::chrono::milliseconds (25)));
            }
        }
        catch (...) {
        }
        if (!actors_completed)
            force (std::chrono::steady_clock::now () >= deadline_at
                     ? drain_force_reason_t::deadline_exceeded
                     : drain_force_reason_t::relocation_failed);
    }

    if (std::holds_alternative<drained_t> (result)) {
        for (const auto &service : state.hosted_services) {
            if (auto *stream = dynamic_cast<runtime::stream_host_service_t *> (service.get ());
                stream && !stream->drain_sessions_until (deadline_at)) {
                force (std::chrono::steady_clock::now () >= deadline_at
                         ? drain_force_reason_t::deadline_exceeded
                         : drain_force_reason_t::teardown_failed);
                break;
            }
        }
    }

    if (std::holds_alternative<drained_t> (result)) {
        bool spots_closed = true;
        for (const auto &snapshot : detail::spot_node_runtime_t::snapshots (state.zlink)) {
            auto runtime = detail::spot_node_runtime_t::from (state.zlink, snapshot.name);
            if (runtime && !runtime->close_all_user_spots ()) {
                spots_closed = false;
                break;
            }
        }
        if (!spots_closed)
            force (std::chrono::steady_clock::now () >= deadline_at
                     ? drain_force_reason_t::deadline_exceeded
                     : drain_force_reason_t::teardown_failed);
    }

    if (std::holds_alternative<drained_t> (result)) {
        try {
            auto provider = state.services.build_provider ();
            if (auto location_runtime = provider.get<runtime::location_runtime_t> ()) {
                if (!location_runtime->get ().cleanup_owner ()) {
                    force (drain_force_reason_t::teardown_failed);
                }
            }
        }
        catch (...) {
            force (drain_force_reason_t::teardown_failed);
        }
    }

    const bool force_stopped = std::holds_alternative<force_stopped_t> (result);
    if (force_stopped) {
        /* graceful-drain-handoff §7: active sessions receive the reason code
         * before forced teardown; the notification is bounded and never
         * blocks the terminal result indefinitely. */
        try {
            for (const auto &service : state.hosted_services) {
                if (auto *stream_service =
                      dynamic_cast<runtime::stream_host_service_t *> (service.get ())) {
                    stream_service->force_close_sessions (
                      stream_close_reason_t::server_drain, "drain force stop");
                    runtime::runtime_metrics_t metrics (
                      detail::monitoring_runtime_t::from (state.monitoring).state ());
                    if (metrics.enabled ()) {
                        metrics.counter ("zlink.drain.forced", "{event}", 1,
                                         {{"kind", "session"}});
                    }
                }
            }
        }
        catch (...) {
        }
    }
    if (!force_stopped)
        emit_state (drain_state_t::drained);
    try {
        runtime::runtime_metrics_t drain_metrics (
          detail::monitoring_runtime_t::from (state.monitoring).state ());
        if (drain_metrics.enabled ()) {
            const auto elapsed = std::chrono::duration<double> (
                                   std::chrono::steady_clock::now () - started_at)
                                   .count ();
            drain_metrics.histogram ("zlink.drain.duration", "s", elapsed,
                                     {{"outcome", force_stopped ? "force_stopped" : "drained"}});
        }
    }
    catch (...) {
    }

    termination_reason_t terminal_reason = termination_reason_t::none;
    if (const auto *forced = std::get_if<force_stopped_t> (&result)) {
        switch (forced->reason) {
        case drain_force_reason_t::deadline_exceeded:
            terminal_reason = termination_reason_t::deadline_exceeded;
            break;
        case drain_force_reason_t::relocation_failed:
            terminal_reason = termination_reason_t::relocation_failed;
            break;
        case drain_force_reason_t::teardown_failed:
            terminal_reason = termination_reason_t::teardown_failed;
            break;
        }
    }
    termination_result_t terminal{
      effective_intent,
      force_stopped ? termination_outcome_t::force_stopped
                    : termination_outcome_t::stopped,
      terminal_reason};
    state.stop_requested.store (true, std::memory_order_release);
    {
        std::unique_lock lock (state.termination_teardown_mutex);
        if (state.run_active) {
            state.termination_teardown_changed.wait (
              lock, [&] { return state.teardown_complete; });
        }
    }
    if (terminal.outcome == termination_outcome_t::stopped
        && std::chrono::steady_clock::now () >= deadline_at) {
        terminal.outcome = termination_outcome_t::force_stopped;
        terminal.reason = termination_reason_t::deadline_exceeded;
    }
    std::vector<std::shared_ptr<detail::app_state_t::termination_waiter_t>>
      waiters;
    {
        std::lock_guard lock (state.termination_operation.mutex);
        if (!state.termination_operation.terminal) {
            state.termination_operation.terminal = true;
            state.termination_operation.result = terminal;
            waiters = std::move (state.termination_operation.waiters);
            state.termination_operation.waiters.clear ();
        }
    }
    for (auto &waiter : waiters) {
        waiter->complete (terminal);
    }
    state.runtime_state.store (
      framework_runtime_state_t::stopped, std::memory_order_release);
}

void app_t::stop () noexcept
{
    try {
        (void) shutdown ();
    }
    catch (...) {
        _state->stop_requested.store (true, std::memory_order_release);
    }
}

void app_t::request_stop () noexcept
{
    stop ();
}

} // namespace zlink::framework
