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
#include "runtime/mesh/mesh_metadata_codec.hpp"
#include "runtime/mesh/route_mesh_runtime_service.hpp"
#include "runtime/mesh/route_mesh_runtime_options_service.hpp"
#include "runtime/locations/location_runtime.hpp"
#include "runtime/locations/actor_authority_payload.hpp"
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
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <variant>
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
        auto read = _store.read_authority (authority_key_t{"1:" + actor_id}).result ();
        if (!read) {
            return task_t<std::optional<actor_ref_t>> (
              result_t<std::optional<actor_ref_t>>::failure (
                read.error_kind (),
                read.error () ? read.error ()->what () : "actor authority lookup failed",
                read.error () && read.error ()->is_retriable ()));
        }
        const auto *snapshot = std::get_if<authority_snapshot_t> (&read.value ());
        const auto projection = snapshot
          ? runtime::decode_actor_authority_payload (snapshot->payload)
          : std::nullopt;
        if (!snapshot || snapshot->allocation.state != placement_allocation_state_t::active
            || snapshot->allocation.object_kind != placement_object_kind_t::actor
            || !projection || projection->actor.actor_id () != actor_id) {
            return task_t<std::optional<actor_ref_t>> (
              result_t<std::optional<actor_ref_t>>::success (std::nullopt));
        }
        return task_t<std::optional<actor_ref_t>> (
          result_t<std::optional<actor_ref_t>>::success (projection->actor));
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
                                  const std::vector<channel_snapshot_t> &channels)
{
    const auto state = monitoring_runtime_t::from (monitoring).state ();
    for (const auto &source : state->socket_sources) {
        if (!monitoring_socket_source_exists (channels, source.source_name)) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "socket monitoring source '" + source.source_name
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

    template <typename TResult>
    struct lifecycle_waiter_t :
        public std::enable_shared_from_this<lifecycle_waiter_t<TResult>>
    {
        task_completion_source_t<TResult> completion;
        std::atomic_bool completed = false;
        std::optional<std::stop_callback<std::function<void ()>>>
          cancellation;

        task_t<TResult> task ()
        {
            return completion.task ();
        }

        void arm (std::stop_token token)
        {
            if (!token.stop_possible ())
                return;
            std::weak_ptr<lifecycle_waiter_t<TResult>> weak =
              this->shared_from_this ();
            cancellation.emplace (
              token, [weak] {
                  if (auto waiter = weak.lock ())
                      waiter->cancel ();
              });
        }

        void complete (TResult result)
        {
            if (completed.exchange (true, std::memory_order_acq_rel))
                return;
            completion.complete (
              result_t<TResult>::success (std::move (result)));
        }

        void cancel ()
        {
            if (completed.exchange (true, std::memory_order_acq_rel))
                return;
            completion.complete (
              detail::boundary_failure<TResult> (
                detail::boundary_error_t::cancelled,
                "lifecycle waiter was cancelled"));
        }
    };

    using relocation_waiter_t = lifecycle_waiter_t<relocation_result_t>;
    using termination_waiter_t = lifecycle_waiter_t<termination_result_t>;

    struct relocation_operation_t
    {
        std::mutex mutex;
        bool started = false;
        bool terminal = false;
        bool shutdown_requested = false;
        relocation_options_t options{};
        relocation_result_t result{};
        std::chrono::milliseconds deadline{30000};
        std::vector<std::shared_ptr<relocation_waiter_t>> waiters;
        std::thread worker;

        ~relocation_operation_t ()
        {
            if (worker.joinable ())
                worker.join ();
        }
    };

    struct termination_operation_t
    {
        std::mutex mutex;
        bool started = false;
        bool terminal = false;
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
    relocation_operation_t relocation_operation;
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
    std::vector<std::shared_ptr<mesh_node_runtime_t>> route_mesh_nodes;
    std::function<bool ()> has_manual_service_topology;
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
          std::type_index (typeid (location_store_t)))
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
                  provider.get_required<location_store_t> ()));
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
    _state->has_manual_service_topology =
      [options, mesh_node_registrations,
       channel_runtime_manager] () mutable {
          if (!options.client_endpoint_connections ().empty ()
              || !options.subscriber_endpoint_connections ().empty ()) {
              return true;
          }
          for (const auto &registration : mesh_node_registrations) {
              std::lock_guard lock (registration->mutex);
              if (!registration->peer_connections.empty ())
                  return true;
          }
          for (const auto &route_id :
               channel_runtime_manager.route_channel_ids ()) {
              if (!channel_runtime_manager
                     .get_route_channel (route_id)
                     .manual_connections ()
                     .empty ()) {
                  return true;
              }
          }
          return false;
      };
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
        channel_runtime.bind_spot_address_resolver (
          provider.get_required<runtime::spot_address_resolver_t> ());
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
            spot_runtime.set_message_follow_duration (
              options.location_options ().message_follow_duration);
            if (_state->services.contains (
                  std::type_index (typeid (
                    runtime::stateful::relocation_store_port_t)))) {
                auto &relocations = provider.get_required<
                  runtime::stateful::relocation_store_port_t> ();
                spot_runtime.bind_relocation_store (
                  std::shared_ptr<
                    runtime::stateful::relocation_store_port_t> (
                    &relocations, [] (auto *) noexcept {}));
            }
            if (_state->services.contains (
                  std::type_index (typeid (
                    runtime::stateful::authority_relocation_port_t)))) {
                auto &authority = provider.get_required<
                  runtime::stateful::authority_relocation_port_t> ();
                spot_runtime.bind_relocation_authority (
                  std::shared_ptr<
                    runtime::stateful::authority_relocation_port_t> (
                    &authority, [] (auto *) noexcept {}));
            }
            spot_runtime.bind_location_lifecycle (location_lifecycle);
            spot_runtime.bind_spot_location_resolver (spot_resolver);
            spot_runtime.bind_drain_flag (_state->draining);
            spot_runtime.set_route_client (route_client);
            if (application_mesh_registration != mesh_node_registrations.end ()) {
                auto application_mesh = detail::mesh_node_runtime_t::from (
                  _state->zlink, (*application_mesh_registration)->mesh_name);
                spot_runtime.on_actor_message_follow (
                  [application_mesh] (
                    const actor_ref_t &actor,
                    const runtime::messaging::envelope_header_t &header,
                    const zlink::message_t &payload,
                    std::chrono::milliseconds timeout) {
                      return application_mesh->relay_application_actor (
                        actor, header, payload, timeout);
                  });
            }
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
        _state->route_mesh_nodes = mesh_nodes;
        auto provider = _state->services.build_provider ();
        auto &location_runtime =
          provider.get_required<runtime::location_runtime_t> ();
        for (const auto &mesh_node : mesh_nodes) {
            mesh_node->configure_session_route_owner (
              [&location_runtime] {
                  return location_runtime.current_owner_token ();
              });
        }
        if (!_state->services.contains (
              std::type_index (typeid (actor_manager_t))))
            _state->services.add_singleton<actor_manager_t> (
              std::make_unique<actor_manager_t> (
                mesh_service->actor_manager ()));
        add_hosted_service (std::move (mesh_service));
    }
    if (!mesh_nodes.empty ()
        && !_state->services.contains (
          std::type_index (typeid (route_mesh_runtime_t)))) {
        auto provider = _state->services.build_provider ();
        auto location_runtime = provider.get<location_runtime_query_t> ();
        auto location_store = provider.get<location_store_t> ();
        auto mesh_runtime =
          std::make_shared<runtime::route_mesh_runtime_service_t> (
            mesh_nodes,
            location_runtime ? &location_runtime->get () : nullptr,
            location_store ? &location_store->get () : nullptr);
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
    if (!mesh_nodes.empty ()) {
        auto provider = _state->services.build_provider ();
        auto &location_store = provider.get_required<location_store_t> ();
        auto operation_sequence =
          std::make_shared<std::atomic<std::uint64_t>> (1);
        struct selected_instance_target_t
        {
            std::shared_ptr<detail::mesh_node_runtime_t> source;
            mesh_node_descriptor_t target;
            std::string stable_type;
        };
        auto select_instance_target =
          [mesh_nodes, &location_store] (
            const spot_id_t &spot_id,
            const detail::spot_activation_intent_t &intent)
            -> result_t<selected_instance_target_t> {
              std::vector<std::shared_ptr<detail::mesh_node_runtime_t>>
                sources;
              for (const auto &mesh : mesh_nodes) {
                  if (!intent.mesh_name
                      || mesh->mesh_name () == *intent.mesh_name)
                      sources.push_back (mesh);
              }
              if (sources.empty ())
                  return result_t<selected_instance_target_t>::failure (
                    intent.mesh_name
                      ? framework_error_kind_t::mesh_not_found
                      : framework_error_kind_t::object_client_not_configured,
                    "No Instance Spot source Mesh is configured");
              if (!intent.mesh_name && sources.size () != 1)
                  return result_t<selected_instance_target_t>::failure (
                    framework_error_kind_t::mesh_selection_required,
                    "More than one object Mesh is configured; select one with in_mesh");
              const auto source = sources.front ();
              std::vector<mesh_node_descriptor_t> candidates;
              location_page_request_t page;
              do {
                  auto listed = location_store
                    .list_mesh_nodes (source->mesh_name (), page)
                    .result ().value ();
                  for (auto &descriptor : listed.items) {
                      if (descriptor.state
                            != framework_runtime_state_t::serving
                          || descriptor.object_role
                               != object_role_t::server
                          || descriptor.placement_weight <= 0)
                          continue;
                      const auto capable = std::any_of (
                        descriptor.object_capabilities.begin (),
                        descriptor.object_capabilities.end (),
                        [&] (const auto &capability) {
                            return capability.object_kind
                                     == placement_object_kind_t::instance_spot
                              && (!intent.stable_type
                                  || capability.stable_type
                                       == *intent.stable_type);
                        });
                      const auto spot_capacity =
                        descriptor.capacity.spots;
                      const auto typed_capacity = std::find_if (
                        descriptor.capacity.spot_types.begin (),
                        descriptor.capacity.spot_types.end (),
                        [&] (const auto &typed) {
                            return typed.object_kind
                                     == placement_object_kind_t::instance_spot
                              && intent.stable_type
                              && typed.stable_type
                                   == *intent.stable_type;
                        });
                      const auto typed_available =
                        !intent.stable_type
                        ||
                        typed_capacity
                          == descriptor.capacity.spot_types.end ()
                        || typed_capacity->usage.limit == 0
                        || typed_capacity->usage.active
                             + typed_capacity->usage.reserved
                             < static_cast<std::uint64_t> (
                               typed_capacity->usage.limit);
                      if (capable && typed_available
                          && (spot_capacity.limit == 0
                              || spot_capacity.active
                                   + spot_capacity.reserved
                                   < static_cast<std::uint64_t> (
                                     spot_capacity.limit)))
                          candidates.push_back (std::move (descriptor));
                  }
                  page.continuation_token =
                    std::move (listed.continuation_token);
              } while (page.continuation_token);
              if (candidates.empty ())
                  return result_t<selected_instance_target_t>::failure (
                    framework_error_kind_t::spot_route_not_found,
                    "No eligible Instance Spot target is Ready");
              std::set<std::string> stable_types;
              for (const auto &candidate : candidates)
                  for (const auto &capability :
                       candidate.object_capabilities)
                      if (capability.object_kind
                            == placement_object_kind_t::instance_spot
                          && (!intent.stable_type
                              || capability.stable_type
                                   == *intent.stable_type))
                          stable_types.insert (capability.stable_type);
              if (!intent.stable_type && stable_types.size () != 1)
                  return result_t<selected_instance_target_t>::failure (
                    framework_error_kind_t::invalid_configuration,
                    "Instance Spot stable type is required when the Mesh publishes multiple types");
              const auto stable_type = intent.stable_type
                ? *intent.stable_type
                : *stable_types.begin ();
              candidates.erase (
                std::remove_if (
                  candidates.begin (), candidates.end (),
                  [&] (const auto &candidate) {
                      const auto capable = std::any_of (
                        candidate.object_capabilities.begin (),
                        candidate.object_capabilities.end (),
                        [&] (const auto &capability) {
                            return capability.object_kind
                                     == placement_object_kind_t::instance_spot
                              && capability.stable_type == stable_type;
                        });
                      const auto typed = std::find_if (
                        candidate.capacity.spot_types.begin (),
                        candidate.capacity.spot_types.end (),
                        [&] (const auto &capacity) {
                            return capacity.object_kind
                                     == placement_object_kind_t::instance_spot
                              && capacity.stable_type == stable_type;
                        });
                      return !capable
                        || (typed != candidate.capacity.spot_types.end ()
                            && typed->usage.limit > 0
                            && typed->usage.active
                                 + typed->usage.reserved
                                 >= static_cast<std::uint64_t> (
                                   typed->usage.limit));
                  }),
                candidates.end ());
              if (candidates.empty ())
                  return result_t<selected_instance_target_t>::failure (
                    framework_error_kind_t::spot_route_not_found,
                    "No eligible Instance Spot target has capacity");
              const auto index = std::hash<std::string>{} (
                std::string (spot_id)) % candidates.size ();
              return result_t<selected_instance_target_t>::success (
                {source, candidates[index], stable_type});
          };
        auto make_activation =
          [operation_sequence] (
            const selected_instance_target_t &selected,
            const spot_id_t &spot_id,
            bool request,
            bool has_metadata,
            std::chrono::milliseconds timeout) {
              const auto source_status = selected.source->status ();
              const auto operation =
                operation_sequence->fetch_add (
                  1, std::memory_order_relaxed);
              return runtime::protocol::instance_spot_activation_header_t{
                {selected.target.rid.to_bytes (),
                 selected.target.lifecycle_generation,
                 std::string (spot_id), selected.target.mesh_name,
                 selected.stable_type,
                 std::to_string (
                   selected.target.descriptor_revision),
                 static_cast<std::uint64_t> (
                   std::chrono::duration_cast<std::chrono::milliseconds> (
                     std::chrono::system_clock::now ().time_since_epoch ()
                     + timeout)
                     .count ())},
                source_status.lifecycle_generation (),
                source_status.routing_id ().to_bytes (),
                std::nullopt, request, {0, operation}, 0,
                has_metadata};
          };
        channel_runtime.bind_instance_spot_activator (
          [select_instance_target, make_activation,
           serializers = &_state->serializers] (
            const spot_id_t &spot_id,
            const detail::spot_activation_intent_t &intent,
            const std::string &packet_name,
            std::type_index message_type,
            std::function<encoded_payload_t (serializer_registry_t &)>
              encode_payload,
            const std::map<std::string, std::string> &metadata) {
              auto selected = select_instance_target (spot_id, intent);
              if (!selected)
                  return detail::propagate_failure<void> (
                    selected, "Instance Spot target selection failed");
              auto metadata_frame =
                detail::mesh_metadata_codec_t::encode (metadata);
              auto header = make_activation (
                selected.value (), spot_id, false,
                !metadata_frame.empty (), std::chrono::seconds (30));
              const auto payload = encode_payload (*serializers);
              const auto submitted = selected.value ().source
                ->send_instance_spot_activation_remote (
                  selected.value ().target.rid, std::move (header),
                  metadata_frame.empty ()
                    ? std::optional<std::vector<std::uint8_t>>{}
                    : std::make_optional (std::move (metadata_frame)),
                  {packet_name,
                   serializers->content_type (message_type),
                   payload.to_bytes ()});
              return submitted
                ? result_t<void>::success ()
                : result_t<void>::failure (
                    framework_error_kind_t::route_not_connected,
                    "Instance Spot activation was not admitted");
          },
          [select_instance_target, make_activation,
           serializers = &_state->serializers] (
            const spot_id_t &spot_id,
            const detail::spot_activation_intent_t &intent,
            std::string packet_name,
            std::type_index request_type,
            std::function<encoded_payload_t (serializer_registry_t &)>
              encode_payload,
            std::chrono::milliseconds timeout,
            std::map<std::string, std::string> metadata)
            -> task_t<zlink::message_t> {
              auto selected = select_instance_target (spot_id, intent);
              if (!selected)
                  return task_t<zlink::message_t> (
                    detail::propagate_failure<zlink::message_t> (
                      selected,
                      "Instance Spot target selection failed"));
              auto metadata_frame =
                detail::mesh_metadata_codec_t::encode (metadata);
              auto header = make_activation (
                selected.value (), spot_id, true,
                !metadata_frame.empty (), timeout);
              const auto payload = encode_payload (*serializers);
              auto completion = std::make_shared<
                detail::task_completion_source_t<zlink::message_t>> ();
              auto output = completion->task ();
              const auto submitted = selected.value ().source
                ->activate_instance_spot_remote (
                  selected.value ().target.rid, std::move (header),
                  metadata_frame.empty ()
                    ? std::optional<std::vector<std::uint8_t>>{}
                    : std::make_optional (std::move (metadata_frame)),
                  {std::move (packet_name),
                   serializers->content_type (request_type),
                   payload.to_bytes ()}, timeout,
                  [completion] (
                    runtime::foundation::operation_terminal_t terminal,
                    runtime::protocol::reply_header_t reply,
                    std::optional<runtime::protocol::application_payload_t>
                      application_reply) {
                      if (terminal
                            != runtime::foundation::operation_terminal_t::completed
                          || reply.terminal_result != 0
                          || !application_reply) {
                          completion->complete (
                            result_t<zlink::message_t>::failure (
                              framework_error_kind_t::request_failed,
                              "Instance Spot activation request failed"));
                          return;
                      }
                      completion->complete (
                        result_t<zlink::message_t>::success (
                          zlink::message_t::from (
                            std::move (
                              application_reply->payload))));
                  });
              if (!submitted)
                  completion->complete (
                    result_t<zlink::message_t>::failure (
                      framework_error_kind_t::route_not_connected,
                      "Instance Spot activation was not admitted"));
              return output;
          });
    }
    const auto spot_router_channels = options.location_options ().spot_router_channels;
    for (const auto &mesh : mesh_nodes) {
        const auto source_spot_id = detail::new_user_spot_id ();
        auto send_to_spot = [mesh, source_spot_id] (
            const zlink::routing_id_t &target_node,
            const std::string &target_spot,
            std::uint64_t target_spot_generation,
            runtime::messaging::message_parts_t parts) {
              const auto submitted = mesh->send_to_spot (
                source_spot_id, target_node, target_spot,
                target_spot_generation, parts.items ());
              return one_way_native_submit_result (submitted, "MeshNode Spot send");
          };
        auto request_to_spot = [mesh, source_spot_id] (
            const zlink::routing_id_t &target_node,
            const std::string &target_spot,
            std::uint64_t target_spot_generation,
            runtime::messaging::message_parts_t parts,
            std::chrono::milliseconds timeout) {
              detail::host::operation_id_t operation;
              const auto submitted = mesh->request_to_spot (
                source_spot_id, target_node, target_spot, target_spot_generation,
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
                  if (submitted == zlink::submit_result_t::not_found
                      || submitted == zlink::submit_result_t::not_admitted) {
                      return result_t<runtime::messaging::message_parts_t>::failure (
                        framework_error_kind_t::request_target_not_found,
                        "MeshNode request target was not found");
                  }
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
          [application_mesh] (
            const actor_ref_t &actor,
            const zlink::message_t &request,
            std::chrono::milliseconds timeout) {
              const auto routing_id = application_mesh->routing_id ();
              const auto target_node = routing_id
                ? node_rid_t::from_string (routing_id->to_string ())
                : actor.node_rid ();
              return application_mesh->join_application_actor_to_entry_spot (
                actor, target_node, request, timeout);
          });
        actor_gateway_runtime.on_join_spot (
          [application_mesh, actor_gateway_runtime, spot_locations =
             &_state->services.build_provider ()
                .get_required<runtime::spot_address_resolver_t> ()] (
            const actor_ref_t &actor,
            spot_id_t target_spot,
            const zlink::message_t &request,
            std::chrono::milliseconds timeout) {
              const auto deadline =
                std::chrono::steady_clock::now () + timeout;
              result_t<std::optional<runtime::spot_address_t>> located =
                result_t<std::optional<runtime::spot_address_t>>::success (std::nullopt);
              do {
                  located = spot_locations
                    ->resolve_spot_address ({}, target_spot)
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
              if (target.object_generation == 0) {
                  return result_t<detail::actor_join_reply_t>::failure (
                    framework_error_kind_t::spot_route_not_found,
                    "target Spot lifecycle generation was not published");
              }
              const auto bound_session =
                actor_gateway_runtime.bound_session_route (actor);
              return application_mesh->join_application_actor_to_spot (
                actor, node_rid_t::from_string (target.node_rid.to_string ()),
                target_spot, target.object_generation,
                request, timeout,
                bound_session
                  ? std::make_optional (bound_session->node_rid)
                  : std::nullopt,
                bound_session ? bound_session->session_rid : std::nullopt);
          });
        actor_gateway_runtime.on_relay (
          [application_mesh, request_timeout] (
            const actor_ref_t &actor,
            actor_context_t,
            const detail::stream_header_t &header,
            const zlink::message_t &payload) {
              return application_mesh->relay_application_actor (
                actor, header, payload, request_timeout);
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
          [mesh_nodes, actor_location_observer,
           location_options = options.location_options ()] (service_provider_t &provider) mutable {
              return runtime::make_actor_client (
                provider.get_required<runtime::live_location_reader_t> (),
                provider.get_required<serializer_registry_t> (), mesh_nodes,
                actor_location_observer, location_options);
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
    detail::validate_monitoring_sources (_state->monitoring, channel_snapshot);
    add_hosted_service (std::make_unique<runtime::location_auto_connect_host_service_t> (
      _state->zlink.message_bus (), channel_snapshot, _state->handlers,
      _state->serializers,
      options.client_server_server_advertise_hosts (),
      options.route_mesh_client_channels (), mesh_nodes,
      [mesh_node_service] {
          return mesh_node_service == nullptr
                 || mesh_node_service->republish_after_store_recovery ();
      }));
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

enum class shutdown_force_reason_t
{
    deadline_exceeded,
    teardown_failed
};

struct shutdown_completed_t
{
};

struct shutdown_forced_t
{
    shutdown_force_reason_t reason;
};

using shutdown_progress_t =
  std::variant<shutdown_completed_t, shutdown_forced_t>;

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

bool capacity_available_for_relocation (const capacity_usage_t &usage)
{
    return usage.limit == 0
           || usage.active + usage.reserved
                < static_cast<std::uint64_t> (usage.limit);
}

bool supports_relocation_source (
  const mesh_node_descriptor_t &source,
  const mesh_node_descriptor_t &candidate,
  std::int64_t target_application_version)
{
    if (candidate.state != framework_runtime_state_t::serving
        || candidate.object_role != object_role_t::server
        || candidate.placement_weight <= 0
        || candidate.application_version != target_application_version
        || (source.maintenance_wave
            && candidate.maintenance_wave == source.maintenance_wave)) {
        return false;
    }
    for (const auto &required : source.object_capabilities) {
        const auto supported = std::find_if (
          candidate.object_capabilities.begin (),
          candidate.object_capabilities.end (),
          [&] (const object_capability_t &value) {
              return value.object_kind == required.object_kind
                     && value.stable_type == required.stable_type
                     && (required.policy != maintenance_policy_kind_t::snapshot
                         || (value.policy == maintenance_policy_kind_t::snapshot
                             && value.has_snapshot_adapter));
          });
        if (supported == candidate.object_capabilities.end ())
            return false;
        if (required.object_kind == placement_object_kind_t::actor) {
            if (!capacity_available_for_relocation (candidate.capacity.actors))
                return false;
        } else {
            if (!capacity_available_for_relocation (candidate.capacity.spots))
                return false;
            const auto typed = std::find_if (
              candidate.capacity.spot_types.begin (),
              candidate.capacity.spot_types.end (),
              [&] (const spot_type_capacity_t &value) {
                  return value.object_kind == required.object_kind
                         && value.stable_type == required.stable_type;
              });
            if (typed != candidate.capacity.spot_types.end ()
                && !capacity_available_for_relocation (typed->usage)) {
                return false;
            }
        }
    }
    return candidate.activation_concurrency.limit <= 0
           || candidate.activation_concurrency.active
                < static_cast<std::uint32_t> (
                    candidate.activation_concurrency.limit);
}

struct relocation_preflight_t
{
    std::optional<relocation_reason_t> blocker;
    std::int64_t effective_target_application_version = 0;
};

relocation_preflight_t
relocation_topology_preflight (
  detail::app_state_t &state,
  const relocation_options_t &options)
{
    if (state.has_manual_service_topology
        && state.has_manual_service_topology ())
        return {relocation_reason_t::manual_topology_unsupported, 0};
    if (state.route_mesh_nodes.empty ())
        return {relocation_reason_t::target_unavailable, 0};

    try {
        auto provider = state.services.build_provider ();
        auto &store = provider.get_required<location_store_t> ();
        auto &live = provider.get_required<runtime::live_location_reader_t> ();

        std::set<std::string> local_rids;
        std::optional<std::int64_t> source_application_version;
        for (const auto &node : state.route_mesh_nodes) {
            if (node) {
                if (const auto rid = node->routing_id ())
                    local_rids.insert (rid->to_hex ());
            }
        }

        for (const auto &node : state.route_mesh_nodes) {
            if (!node)
                continue;
            const auto local_rid = node->routing_id ();
            const auto status = node->status ();
            if (!local_rid || status.lifecycle_generation () == 0)
                return {relocation_reason_t::runtime_not_ready, 0};

            std::vector<mesh_node_descriptor_t> descriptors;
            location_page_request_t page;
            do {
                auto listed = store
                                .list_mesh_nodes (node->mesh_name (), page)
                                .result ()
                                .value ();
                descriptors.insert (
                  descriptors.end (),
                  std::make_move_iterator (listed.items.begin ()),
                  std::make_move_iterator (listed.items.end ()));
                page.continuation_token =
                  std::move (listed.continuation_token);
            } while (page.continuation_token);

            const auto source = std::find_if (
              descriptors.begin (), descriptors.end (),
              [&] (const mesh_node_descriptor_t &descriptor) {
                  return descriptor.rid.to_hex () == local_rid->to_hex ()
                         && descriptor.lifecycle_generation
                              == status.lifecycle_generation ();
              });
            if (source == descriptors.end ()
                || !live.owner_admission_lifetime (source->owner_id)) {
                return {relocation_reason_t::store_unavailable, 0};
            }
            if (source_application_version
                && *source_application_version
                     != source->application_version) {
                return {relocation_reason_t::state_incompatible, 0};
            }
            source_application_version = source->application_version;
            if (options.mode == relocation_mode_t::rolling_update
                && *options.target_application_version
                     <= source->application_version) {
                throw std::invalid_argument (
                  "rolling update target application version must be greater "
                  "than the source version");
            }
            const auto target_application_version =
              options.mode == relocation_mode_t::planned_maintenance
                ? source->application_version
                : *options.target_application_version;

            const auto replacement = std::any_of (
              descriptors.begin (), descriptors.end (),
              [&] (const mesh_node_descriptor_t &candidate) {
                  return !local_rids.contains (candidate.rid.to_hex ())
                         && candidate.lifecycle_generation != 0
                         && live.owner_admission_lifetime (
                              candidate.owner_id)
                         && supports_relocation_source (
                              *source, candidate,
                              target_application_version)
                         && node->has_admitted_peer (
                              candidate.rid,
                              candidate.lifecycle_generation);
              });
            if (!replacement)
                return {
                  relocation_reason_t::target_unavailable,
                  target_application_version};
        }
        const auto effective =
          options.mode == relocation_mode_t::planned_maintenance
            ? *source_application_version
            : *options.target_application_version;
        return {std::nullopt, effective};
    }
    catch (const std::invalid_argument &) {
        throw;
    }
    catch (...) {
        return {relocation_reason_t::store_unavailable, 0};
    }
}

bool publish_mesh_descriptor_state (
  detail::app_state_t &state,
  framework_runtime_state_t desired) noexcept
{
    bool published = true;
    for (const auto &service : state.hosted_services) {
        if (auto *mesh =
              dynamic_cast<runtime::mesh_node_host_service_t *> (
                service.get ());
            mesh && !mesh->publish_descriptor_state (desired)) {
            published = false;
        }
    }
    return published;
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

task_t<relocation_result_t> app_t::relocate (
  relocation_options_t options,
  std::stop_token wait_cancellation)
{
    if (options.mode == relocation_mode_t::planned_maintenance
        && options.target_application_version) {
        throw std::invalid_argument (
          "planned maintenance does not accept a target application version");
    }
    if (options.mode == relocation_mode_t::rolling_update
        && !options.target_application_version) {
        throw std::invalid_argument (
          "rolling update requires a target application version");
    }
    const auto deadline =
      options.deadline.value_or (std::chrono::seconds (30));
    if (deadline <= std::chrono::milliseconds::zero ())
        throw std::invalid_argument ("relocation deadline must be greater than zero");
    options.deadline = deadline;

    relocation_preflight_t preflight;
    if (runtime_state () != framework_runtime_state_t::serving) {
        preflight.blocker = relocation_reason_t::runtime_not_ready;
    } else {
        preflight = relocation_topology_preflight (*_state, options);
        if (!preflight.blocker
            && (!_state->services.contains (
                  std::type_index (typeid (relocation_store_t)))
                || !_state->services.contains (
                  std::type_index (typeid (location_store_t))))) {
            preflight.blocker = relocation_reason_t::store_unavailable;
        }
    }

    auto &operation = _state->relocation_operation;
    std::shared_ptr<detail::app_state_t::relocation_waiter_t> waiter;
    task_t<relocation_result_t> task (
      result_t<relocation_result_t>::success ({}));
    {
        std::lock_guard lock (operation.mutex);
        if (operation.started && !operation.terminal) {
            if (operation.options != options) {
                return task_t<relocation_result_t> (
                  result_t<relocation_result_t>::success (
                    {options.mode,
                     preflight.effective_target_application_version != 0
                       ? preflight.effective_target_application_version
                       : operation.result
                           .effective_target_application_version,
                     relocation_outcome_t::blocked,
                     relocation_reason_t::operation_in_progress}));
            }
        } else if (operation.terminal) {
            return task_t<relocation_result_t> (
              result_t<relocation_result_t>::success (operation.result));
        } else {
            if (preflight.blocker) {
                return task_t<relocation_result_t> (
                  result_t<relocation_result_t>::success (
                    {options.mode,
                     preflight.effective_target_application_version,
                     relocation_outcome_t::blocked,
                     *preflight.blocker}));
            }
            if (!publish_mesh_descriptor_state (
                  *_state, framework_runtime_state_t::relocating)) {
                (void) publish_mesh_descriptor_state (
                  *_state, framework_runtime_state_t::serving);
                return task_t<relocation_result_t> (
                  result_t<relocation_result_t>::success (
                    {options.mode,
                     preflight.effective_target_application_version,
                     relocation_outcome_t::blocked,
                     relocation_reason_t::store_unavailable}));
            }
            operation.started = true;
            operation.options = options;
            operation.deadline = deadline;
            operation.result.mode = options.mode;
            operation.result.effective_target_application_version =
              preflight.effective_target_application_version;
            _state->runtime_state.store (
              framework_runtime_state_t::relocating,
              std::memory_order_release);
            auto *state = _state.get ();
            operation.worker =
              std::thread ([state] { run_shared_relocation (*state); });
        }
        waiter =
          std::make_shared<detail::app_state_t::relocation_waiter_t> ();
        task = waiter->task ();
        operation.waiters.push_back (waiter);
    }
    waiter->arm (wait_cancellation);
    return task;
}

void app_t::run_shared_relocation (
  detail::app_state_t &state) noexcept
{
    auto &operation = state.relocation_operation;
    const auto deadline_at =
      std::chrono::steady_clock::now () + operation.deadline;
    relocation_result_t terminal{
      operation.options.mode,
      operation.result.effective_target_application_version,
      relocation_outcome_t::blocked,
      relocation_reason_t::relocation_failed};

    auto shutdown_requested = [&] {
        std::lock_guard lock (operation.mutex);
        return operation.shutdown_requested;
    };
    auto complete = [&] (relocation_result_t result) {
        std::unique_lock lock (operation.mutex);
        const bool interrupted = operation.shutdown_requested;
        if (interrupted) {
            result.outcome = relocation_outcome_t::blocked;
            result.reason = relocation_reason_t::shutdown_requested;
        }
        if (result.outcome == relocation_outcome_t::relocated
            && !interrupted) {
            if (!publish_mesh_descriptor_state (
                  state, framework_runtime_state_t::relocated)) {
                result.outcome = relocation_outcome_t::blocked;
                result.reason = relocation_reason_t::store_unavailable;
            }
        }
        if (result.outcome == relocation_outcome_t::relocated) {
            state.runtime_state.store (
              framework_runtime_state_t::relocated,
              std::memory_order_release);
        } else if (!interrupted) {
            (void) publish_mesh_descriptor_state (
              state, framework_runtime_state_t::serving);
            state.runtime_state.store (
              framework_runtime_state_t::serving,
              std::memory_order_release);
        }

        std::vector<std::shared_ptr<detail::app_state_t::relocation_waiter_t>>
          waiters;
        operation.terminal = true;
        operation.result = result;
        waiters = std::move (operation.waiters);
        operation.waiters.clear ();
        lock.unlock ();
        for (auto &waiter : waiters)
            waiter->complete (result);
    };

    try {
        auto provider = state.services.build_provider ();
        auto peers =
          provider.get<runtime::store_location_resolvers_t> ();
        if (!peers) {
            terminal.reason = relocation_reason_t::store_unavailable;
            complete (terminal);
            return;
        }

        std::vector<runtime::mesh_node_host_service_t *> mesh_services;
        for (const auto &service : state.hosted_services) {
            if (auto *mesh =
                  dynamic_cast<runtime::mesh_node_host_service_t *> (
                    service.get ())) {
                mesh_services.push_back (mesh);
            }
        }

        for (auto *mesh_service : mesh_services) {
            for (const auto &node : mesh_service->nodes ()) {
                auto spot_runtime = detail::spot_node_runtime_t::from (
                  state.zlink, node->mesh_name ());
                if (!spot_runtime)
                    continue;
                const auto actors = spot_runtime->local_actor_refs ();
                if (actors.empty ())
                    continue;

                auto live = peers->get ()
                              .list_live_mesh_nodes (node->mesh_name ())
                              .result ()
                              .value ();
                const auto local_rid = node->routing_id ();
                for (const auto &actor : actors) {
                    if (shutdown_requested ()) {
                        terminal.reason =
                          relocation_reason_t::shutdown_requested;
                        complete (terminal);
                        return;
                    }
                    const auto now = std::chrono::steady_clock::now ();
                    if (now >= deadline_at) {
                        terminal.reason =
                          relocation_reason_t::deadline_exceeded;
                        complete (terminal);
                        return;
                    }
                    const auto target = std::find_if (
                      live.begin (), live.end (), [&] (const auto &peer) {
                          return peer.state
                                   == framework_runtime_state_t::serving
                                 && peer.application_version
                                      == terminal.effective_target_application_version
                                 && (!local_rid
                                     || peer.rid.to_hex ()
                                          != local_rid->to_hex ())
                                 && std::any_of (
                                   peer.object_capabilities.begin (),
                                   peer.object_capabilities.end (),
                                   [&] (const auto &capability) {
                                       return capability.object_kind
                                                == placement_object_kind_t::actor
                                              && capability.stable_type
                                                   == actor.actor_type ();
                                   });
                      });
                    if (target == live.end ()) {
                        terminal.reason =
                          relocation_reason_t::target_unavailable;
                        complete (terminal);
                        return;
                    }
                    const auto remaining =
                      std::max (
                        std::chrono::milliseconds (1),
                        std::chrono::duration_cast<std::chrono::milliseconds> (
                          deadline_at - now));
                    auto moved =
                      node->join_application_actor_to_entry_spot (
                        actor,
                        node_rid_t::from_string (
                          target->rid.to_string ()),
                        zlink::message_t{}, remaining);
                    if (!moved || moved.value ().result_code != 0) {
                        terminal.reason =
                          relocation_reason_t::relocation_failed;
                        complete (terminal);
                        return;
                    }
                }
            }
        }
        terminal.outcome = relocation_outcome_t::relocated;
        terminal.reason = relocation_reason_t::none;
    }
    catch (...) {
        terminal.reason = relocation_reason_t::store_unavailable;
    }
    complete (terminal);
}

task_t<termination_result_t> app_t::shutdown (
  std::chrono::milliseconds deadline,
  std::stop_token wait_cancellation)
{
    if (deadline <= std::chrono::milliseconds::zero ())
        throw std::invalid_argument ("shutdown deadline must be greater than zero");
    {
        std::lock_guard relocation_lock (
          _state->relocation_operation.mutex);
        if (_state->relocation_operation.started
            && !_state->relocation_operation.terminal) {
            _state->relocation_operation.shutdown_requested = true;
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
            operation.started = true;
            operation.deadline = deadline;
            _state->draining->store (true, std::memory_order_release);
            _state->runtime_state.store (
              framework_runtime_state_t::draining,
              std::memory_order_release);
            auto *state = _state.get ();
            operation.worker =
              std::thread ([state] { run_shared_shutdown (*state); });
        }
        waiter =
          std::make_shared<detail::app_state_t::termination_waiter_t> ();
        task = waiter->task ();
        operation.waiters.push_back (waiter);
    }
    waiter->arm (wait_cancellation);
    return task;
}

void app_t::run_shared_shutdown (
  detail::app_state_t &state) noexcept
{
    const auto started_at = std::chrono::steady_clock::now ();
    const auto deadline_at =
      started_at + state.termination_operation.deadline;
    auto monitoring = detail::monitoring_runtime_t::from (state.monitoring);
    auto emit_state = [&] (drain_state_t drain_state) {
        try {
            monitoring.publish_drain (
              drain_event_t{runtime_event_base_t{"drain"}, drain_state});
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

    shutdown_progress_t result = shutdown_completed_t{};
    bool force_state_emitted = false;
    auto force = [&] (shutdown_force_reason_t reason) {
        if (!std::holds_alternative<shutdown_completed_t> (result))
            return;
        result = shutdown_forced_t{reason};
        if (!force_state_emitted) {
            force_state_emitted = true;
            emit_state (drain_state_t::force_stopping);
        }
    };

    while (std::chrono::steady_clock::now () < deadline_at) {
        bool relocation_active = false;
        {
            std::lock_guard lock (state.relocation_operation.mutex);
            relocation_active =
              state.relocation_operation.started
              && !state.relocation_operation.terminal;
        }
        if (!relocation_active)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    {
        std::lock_guard lock (state.relocation_operation.mutex);
        if (state.relocation_operation.started
            && !state.relocation_operation.terminal) {
            force (shutdown_force_reason_t::deadline_exceeded);
        }
    }

    std::vector<runtime::mesh_node_host_service_t *> mesh_services;
    for (const auto &service : state.hosted_services) {
        if (auto *mesh = dynamic_cast<runtime::mesh_node_host_service_t *> (service.get ())) {
            mesh->seal_application_dispatch ();
            mesh_services.push_back (mesh);
        }
    }

    auto publish_draining_markers = [&] (bool wait_for_propagation) {
        bool marker_published = false;
        try {
            auto provider = state.services.build_provider ();
            if (auto location_runtime =
                  provider.get<runtime::location_runtime_t> ()) {
                location_runtime->get ().set_draining (true);
                marker_published =
                  location_runtime->get ().republish_peer_rows_draining ();
            } else {
                marker_published = true;
            }
        }
        catch (...) {
            marker_published = false;
        }
        while (std::chrono::steady_clock::now () < deadline_at
               && !marker_published) {
            std::this_thread::sleep_until (
              std::min (deadline_at, std::chrono::steady_clock::now ()
                                       + std::chrono::milliseconds (100)));
            try {
                auto provider = state.services.build_provider ();
                if (auto location_runtime =
                      provider.get<runtime::location_runtime_t> ()) {
                    marker_published =
                      location_runtime->get ().republish_peer_rows_draining ();
                }
            }
            catch (...) {
            }
        }
        if (!marker_published) {
            force (shutdown_force_reason_t::teardown_failed);
            return;
        }

        if (!wait_for_propagation)
            return;
        const bool has_auto_connect =
          std::any_of (state.hosted_services.begin (),
                       state.hosted_services.end (),
                       [] (const auto &service) {
                           return dynamic_cast<
                                    runtime::location_auto_connect_host_service_t *> (
                                    service.get ())
                                  != nullptr;
                       });
        if (!has_auto_connect)
            return;
        try {
            auto provider = state.services.build_provider ();
            auto &location_runtime =
              provider.get_required<runtime::location_runtime_t> ();
            const auto propagation_bound =
              location_runtime.options ().polling_interval
              + std::chrono::seconds (5)
              + std::chrono::milliseconds (100);
            std::cerr << "zlink drain propagation bound polling_ms="
                      << location_runtime.options ().polling_interval.count ()
                      << " store_read_timeout_ms=5000 scheduler_jitter_ms=100 total_ms="
                      << propagation_bound.count () << std::endl;
            if (std::chrono::steady_clock::now () + propagation_bound
                > deadline_at) {
                std::this_thread::sleep_until (deadline_at);
                force (shutdown_force_reason_t::deadline_exceeded);
            } else {
                std::this_thread::sleep_for (propagation_bound);
            }
        }
        catch (...) {
            force (shutdown_force_reason_t::teardown_failed);
        }
    };

    /* Admission is sealed before this barrier. Each callback accepted before
     * the seal owns a pending/active count until its terminal reply or send
     * completion, so a normal request completion cannot close its Spot. */
    if (std::holds_alternative<shutdown_completed_t> (result)) {
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
            force (shutdown_force_reason_t::deadline_exceeded);
        } else {
            for (auto *mesh : mesh_services) {
                if (!mesh->wait_for_accepted_callbacks_until (deadline_at)) {
                    force (shutdown_force_reason_t::deadline_exceeded);
                    break;
                }
            }
        }
    }

    if (std::holds_alternative<shutdown_completed_t> (result)
        && !publish_mesh_descriptor_state (
          state, framework_runtime_state_t::draining)) {
        force (shutdown_force_reason_t::teardown_failed);
    }
    if (std::holds_alternative<shutdown_completed_t> (result)) {
        state.runtime_state.store (
          framework_runtime_state_t::draining,
          std::memory_order_release);
        publish_draining_markers (false);
    }
    if (std::holds_alternative<shutdown_completed_t> (result)) {
        for (auto *mesh : mesh_services) {
            if (!mesh->wait_for_accepted_callbacks_until (deadline_at)) {
                force (shutdown_force_reason_t::deadline_exceeded);
                break;
            }
        }
    }

    if (std::holds_alternative<shutdown_completed_t> (result)) {
        for (const auto &service : state.hosted_services) {
            if (auto *stream = dynamic_cast<runtime::stream_host_service_t *> (service.get ());
                stream && !stream->drain_sessions_until (deadline_at)) {
                force (std::chrono::steady_clock::now () >= deadline_at
                         ? shutdown_force_reason_t::deadline_exceeded
                         : shutdown_force_reason_t::teardown_failed);
                break;
            }
        }
    }

    if (std::holds_alternative<shutdown_completed_t> (result)) {
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
                     ? shutdown_force_reason_t::deadline_exceeded
                     : shutdown_force_reason_t::teardown_failed);
    }

    if (std::holds_alternative<shutdown_completed_t> (result)) {
        try {
            auto provider = state.services.build_provider ();
            if (auto location_runtime = provider.get<runtime::location_runtime_t> ()) {
                if (!location_runtime->get ().cleanup_owner ()) {
                    force (shutdown_force_reason_t::teardown_failed);
                }
            }
        }
        catch (...) {
            force (shutdown_force_reason_t::teardown_failed);
        }
    }

    const bool force_stopped = std::holds_alternative<shutdown_forced_t> (result);
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
    if (const auto *forced = std::get_if<shutdown_forced_t> (&result)) {
        switch (forced->reason) {
        case shutdown_force_reason_t::deadline_exceeded:
            terminal_reason = termination_reason_t::deadline_exceeded;
            break;
        case shutdown_force_reason_t::teardown_failed:
            terminal_reason = termination_reason_t::teardown_failed;
            break;
        }
    }
    termination_result_t terminal{
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
