/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework/contracts/configuration/app.hpp>

#include "runtime/dispatch/coroutine_executor.hpp"

#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>
#include <utility>
#include <vector>

namespace zlink::framework::detail
{

class app_state_t
{
public:
  std::vector<hosted_service_t *> start_hosted_services (
    service_provider_t &provider)
  {
    std::vector<hosted_service_t *> started;
    for (const auto &service : hosted_services) {
      service->start (provider);
      started.push_back (service.get ());
    }
    return started;
  }

  void stop_hosted_services (
    const std::vector<hosted_service_t *> &started) noexcept
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
  zlink_builder_t zlink;
  serializer_registry_t serializers;
  std::vector<std::unique_ptr<hosted_service_t>> hosted_services;
  std::atomic_bool stop_requested = false;
  int exit_code = 0;
};

} // namespace zlink::framework::detail

namespace
{

std::atomic<zlink::framework::detail::app_state_t *> g_active_app { nullptr };

void
handle_process_signal (int) noexcept
{
  if (auto *state = g_active_app.load (std::memory_order_acquire)) {
    state->stop_requested.store (true, std::memory_order_release);
  }
}

} // namespace

namespace zlink::framework
{

app_t::app_t () : _state (std::make_unique<detail::app_state_t> ()) {}

app_t::~app_t () = default;

app_t::app_t (app_t &&) noexcept = default;

app_t &app_t::operator= (app_t &&) noexcept = default;

app_t
app_t::create ()
{
  return {};
}

service_collection_t &
app_t::services () noexcept
{
  return _state->services;
}

handler_registry_t &
app_t::handlers () noexcept
{
  return _state->handlers;
}

config_builder_t &
app_t::config () noexcept
{
  return _state->config;
}

logging_builder_t &
app_t::logging () noexcept
{
  return _state->logging;
}

monitoring_builder_t &
app_t::monitoring () noexcept
{
  return _state->monitoring;
}

zlink_builder_t &
app_t::_zlink_builder () noexcept
{
  return _state->zlink;
}

serializer_registry_t &
app_t::_serializers () noexcept
{
  return _state->serializers;
}

app_t &
app_t::use_zlink (std::function<void (zlink_builder_t &)> configure)
{
  configure (_state->zlink);
  return *this;
}

app_t &
app_t::add_zlink_framework (
  std::function<void (zlink_framework_options_t &)> configure)
{
  _state->services.add_singleton<channel_client_t> (
    std::make_unique<channel_client_t> (_state->zlink.message_bus ()));
  zlink_framework_options_t options (
    _state->services,
    _state->handlers,
    _state->serializers,
    _state->zlink,
    _state->monitoring);
  if (configure) {
    configure (options);
  }
  runtime::configure_handler_coroutine_executor (
    options.handler_coroutine_workers ());
  return *this;
}

app_t &
app_t::add_module (module_t &module)
{
  module.configure_services (_state->services);
  module.configure_zlink (_state->zlink);
  module.configure_handlers (_state->handlers);
  module.configure_monitoring (_state->monitoring);
  return *this;
}

app_t &
app_t::add_hosted_service (std::unique_ptr<hosted_service_t> service)
{
  if (!service) {
    throw framework_exception_t (
      framework_error_kind_t::request_protocol_error,
      "hosted service must not be null");
  }
  _state->hosted_services.push_back (std::move (service));
  return *this;
}

int
app_t::run (int argc, char **argv)
{
  _state->config.load_cli (argc, argv);
  _state->config.model ().set ("host.signal_handlers", "installed");
  g_active_app.store (_state.get (), std::memory_order_release);
  std::signal (SIGINT, handle_process_signal);
  std::signal (SIGTERM, handle_process_signal);

  auto provider = _state->services.build_provider ();
  std::vector<hosted_service_t *> started;
  try {
    started = _state->start_hosted_services (provider);
    while (!_state->stop_requested.load (std::memory_order_acquire)) {
      std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
  } catch (...) {
    _state->stop_hosted_services (started);
    provider.close ();
    auto *expected = _state.get ();
    g_active_app.compare_exchange_strong (expected,
                                          nullptr,
                                          std::memory_order_acq_rel);
    throw;
  }

  _state->stop_hosted_services (started);
  provider.close ();
  auto *expected = _state.get ();
  g_active_app.compare_exchange_strong (expected,
                                        nullptr,
                                        std::memory_order_acq_rel);
  return _state->stop_requested.load (std::memory_order_acquire)
           ? 0
           : _state->exit_code;
}

void
app_t::stop () noexcept
{
  _state->stop_requested.store (true, std::memory_order_release);
}

void
app_t::request_stop () noexcept
{
  stop ();
}

} // namespace zlink::framework
