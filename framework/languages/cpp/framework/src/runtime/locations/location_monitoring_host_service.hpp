/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/diagnostics/monitoring_runtime.hpp"

#include <zlink/framework/contracts/configuration/module.hpp>
#include <zlink/framework/contracts/locations/runtime_query.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace zlink::framework::runtime
{

class location_monitoring_host_service_t final : public hosted_service_t
{
  public:
    explicit location_monitoring_host_service_t (
      std::shared_ptr<detail::monitoring_runtime_state_t> monitoring) :
        _monitoring (std::move (monitoring))
    {
    }

    void start (service_provider_t &services) override
    {
        if (!_monitoring || _monitoring->location_sources.empty ()) {
            return;
        }
        _query = &services.get_required<location_runtime_query_t> ();
        _stop.store (false, std::memory_order_release);
        _thread = std::thread ([this] { run (); });
    }

    void stop () noexcept override
    {
        _stop.store (true, std::memory_order_release);
        _wake.notify_all ();
        if (_thread.joinable ()) {
            _thread.join ();
        }
        _query = nullptr;
    }

  private:
    void run ()
    {
        const auto interval = polling_interval ();
        while (!_stop.load (std::memory_order_acquire)) {
            publish_once ();
            std::unique_lock lock (_gate);
            _wake.wait_for (lock, interval, [this] {
                return _stop.load (std::memory_order_acquire);
            });
        }
    }

    std::chrono::milliseconds polling_interval () const
    {
        auto interval = _monitoring->location_sources.front ().interval;
        for (const auto &source : _monitoring->location_sources) {
            if (source.interval < interval) {
                interval = source.interval;
            }
        }
        return interval;
    }

    void publish_once () noexcept
    {
        if (_query == nullptr) {
            return;
        }
        try {
            auto status = _query->get_status ().result ().value ();
            auto topology =
              _query->list_topology (location_topology_filter_t{}).result ().value ().items;
            auto summaries =
              _query->list_service_summaries (location_service_summary_filter_t{})
                .result ()
                .value ();
            detail::monitoring_runtime_t runtime (_monitoring);
            for (const auto &source : _monitoring->location_sources) {
                runtime.publish_location_snapshot (source.source_name, status, topology, summaries);
            }
        }
        catch (...) {
            try {
                auto status = _query->get_status ().result ().value ();
                detail::monitoring_runtime_t runtime (_monitoring);
                for (const auto &source : _monitoring->location_sources) {
                    runtime.publish_location_snapshot (source.source_name, status, {}, {});
                }
            }
            catch (...) {
            }
        }
    }

    std::shared_ptr<detail::monitoring_runtime_state_t> _monitoring;
    location_runtime_query_t *_query = nullptr;
    std::atomic_bool _stop = false;
    std::thread _thread;
    std::mutex _gate;
    std::condition_variable _wake;
};

} // namespace zlink::framework::runtime
