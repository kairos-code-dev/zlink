/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/configuration/detail/framework_options_state.hpp>
#include <zlink/framework/contracts/configuration/module.hpp>
#include <zlink/framework/contracts/streams/stream.hpp>

#include "runtime/streams/stream_runtime.hpp"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace zlink::framework::runtime
{

class stream_host_service_t final : public hosted_service_t
{
  public:
    stream_host_service_t (
      detail::stream_runtime_t runtime,
      std::vector<stream_snapshot_t> streams,
      std::map<std::string, detail::stream_session_factory_t> session_factories);
    ~stream_host_service_t () override;

    void start (service_provider_t &services) override;
    void stop () noexcept override;

  private:
    class listener_t;

    detail::stream_runtime_t _runtime;
    std::vector<stream_snapshot_t> _streams;
    std::map<std::string, detail::stream_session_factory_t> _session_factories;
    service_provider_t *_services = nullptr;
    std::atomic_bool _stop{false};
    std::vector<std::unique_ptr<listener_t>> _listeners;
    std::vector<std::thread> _threads;
};

} // namespace zlink::framework::runtime
