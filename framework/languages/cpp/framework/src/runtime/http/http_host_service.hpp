/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/configuration/module.hpp>
#include <zlink/framework/contracts/eventing/health.hpp>
#include <zlink/framework/contracts/http/http.hpp>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace zlink::framework::runtime
{

class http_host_service_t final : public hosted_service_t
{
public:
  http_host_service_t (http_options_snapshot_t options,
                       health_builder_t &health);
  ~http_host_service_t () override;

  void start (service_provider_t &services) override;
  void stop () noexcept override;

private:
  class listener_t;

  http_options_snapshot_t _options;
  health_builder_t *_health;
  std::atomic_bool _stop { false };
  std::vector<std::unique_ptr<listener_t>> _listeners;
  std::vector<std::thread> _threads;
};

} // namespace zlink::framework::runtime
