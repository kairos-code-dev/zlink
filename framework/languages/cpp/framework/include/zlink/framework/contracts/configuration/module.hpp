/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/configuration/zlink_builder.hpp>
#include <zlink/framework/contracts/eventing/events.hpp>
#include <zlink/framework/contracts/handlers/handler_registry.hpp>

namespace zlink::framework
{

class hosted_service_t
{
public:
  virtual ~hosted_service_t () = default;

  virtual void start (service_provider_t &services) = 0;
  virtual void stop () noexcept = 0;
};

class module_t
{
public:
  virtual ~module_t () = default;

  virtual void configure_services (service_collection_t &services)
  {
    (void) services;
  }

  virtual void configure_zlink (zlink_builder_t &zlink)
  {
    (void) zlink;
  }

  virtual void configure_handlers (handler_registry_t &handlers)
  {
    (void) handlers;
  }

  virtual void configure_monitoring (monitoring_builder_t &monitoring)
  {
    (void) monitoring;
  }
};

} // namespace zlink::framework
