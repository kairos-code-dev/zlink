/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/configuration/configuration.hpp>
#include <zlink/framework/contracts/configuration/framework_options.hpp>
#include <zlink/framework/contracts/configuration/logging.hpp>
#include <zlink/framework/contracts/configuration/module.hpp>
#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/configuration/zlink_builder.hpp>
#include <zlink/framework/contracts/eventing/events.hpp>
#include <zlink/framework/contracts/handlers/handler_registry.hpp>

#include <functional>
#include <memory>
#include <utility>

namespace zlink::framework
{

namespace detail
{
class app_state_t;
} // namespace detail

class app_t
{
public:
  app_t ();
  ~app_t ();

  app_t (app_t &&) noexcept;
  app_t &operator= (app_t &&) noexcept;
  app_t (const app_t &) = delete;
  app_t &operator= (const app_t &) = delete;

  static app_t create ();

  service_collection_t &services () noexcept;
  handler_registry_t &handlers () noexcept;
  config_builder_t &config () noexcept;
  logging_builder_t &logging () noexcept;
  monitoring_builder_t &monitoring () noexcept;

  app_t &use_zlink (std::function<void (zlink_builder_t &)> configure);
  app_t &add_module (module_t &module);
  app_t &add_zlink_framework (
    std::function<void (zlink_framework_options_t &)> configure);
  template<typename TModule, typename... TArgs>
    requires framework_module_contract_t<TModule>
  app_t &add_zlink_framework (TArgs &&...args)
  {
    TModule module (std::forward<TArgs> (args)...);
    module.configure_services (services ());
    module.configure_zlink (_zlink_builder ());
    module.configure_handlers (handlers ());
    module.configure_monitoring (monitoring ());
    return *this;
  }
  app_t &add_hosted_service (std::unique_ptr<hosted_service_t> service);

  int run (int argc, char **argv);

  void stop () noexcept;
  void request_stop () noexcept;

private:
  zlink_builder_t &_zlink_builder () noexcept;
  serializer_registry_t &_serializers () noexcept;

  std::unique_ptr<detail::app_state_t> _state;
};

} // namespace zlink::framework
