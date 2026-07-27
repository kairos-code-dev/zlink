/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/diagnostics/monitoring_runtime.hpp"

#include <zlink/framework/contracts/configuration/module.hpp>
#include <zlink/framework/contracts/locations/runtime_query.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
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
        _last_status.reset ();
        _last_topology.clear ();
        _last_summaries.clear ();
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
                .value ()
                .items;
            const auto status_changed = !_last_status || !same_status (*_last_status, status);
            const auto topology_changed = !_last_status || !same_topology (_last_topology, topology);
            const auto summaries_changed =
              !_last_status || !same_summaries (_last_summaries, summaries);
            detail::monitoring_runtime_t runtime (_monitoring);
            for (const auto &source : _monitoring->location_sources) {
                runtime.publish_location_changes (
                  source.source_name, status, status_changed,
                  topology_changed
                    ? std::optional<std::vector<location_topology_entry_t>> (topology)
                    : std::nullopt,
                  summaries_changed
                    ? std::optional<std::vector<location_service_summary_t>> (summaries)
                    : std::nullopt);
            }
            _last_status = std::move (status);
            _last_topology = std::move (topology);
            _last_summaries = std::move (summaries);
        }
        catch (...) {
            try {
                auto status = _query->get_status ().result ().value ();
                detail::monitoring_runtime_t runtime (_monitoring);
                const auto changed = !_last_status || !same_status (*_last_status, status);
                for (const auto &source : _monitoring->location_sources) {
                    runtime.publish_location_changes (source.source_name, status, changed,
                                                      std::nullopt, std::nullopt);
                }
                _last_status = std::move (status);
            }
            catch (...) {
            }
        }
    }

    static bool same_status (const location_runtime_status_t &left,
                             const location_runtime_status_t &right)
    {
        return left.store_healthy == right.store_healthy
               && left.watch_enabled == right.watch_enabled
               && left.polling_interval == right.polling_interval
               && left.last_error == right.last_error
               && left.owner_lease_healthy == right.owner_lease_healthy;
    }

    static bool same_topology_entry (const location_topology_entry_t &left,
                                     const location_topology_entry_t &right)
    {
        return left.mesh_name == right.mesh_name && left.node_rid == right.node_rid
               && left.endpoint == right.endpoint && left.draining == right.draining
               && left.state == right.state;
    }

    static bool same_topology (const std::vector<location_topology_entry_t> &left,
                               const std::vector<location_topology_entry_t> &right)
    {
        return left.size () == right.size ()
               && std::is_permutation (left.begin (), left.end (), right.begin (),
                                       same_topology_entry);
    }

    static bool same_summary (const location_service_summary_t &left,
                              const location_service_summary_t &right)
    {
        return left.mesh_name == right.mesh_name && left.total_count == right.total_count
               && left.ready_count == right.ready_count
               && left.error_count == right.error_count
               && left.stopped_count == right.stopped_count;
    }

    static bool same_summaries (const std::vector<location_service_summary_t> &left,
                                const std::vector<location_service_summary_t> &right)
    {
        return left.size () == right.size ()
               && std::is_permutation (left.begin (), left.end (), right.begin (), same_summary);
    }

    std::shared_ptr<detail::monitoring_runtime_state_t> _monitoring;
    location_runtime_query_t *_query = nullptr;
    std::atomic_bool _stop = false;
    std::thread _thread;
    std::mutex _gate;
    std::condition_variable _wake;
    std::optional<location_runtime_status_t> _last_status;
    std::vector<location_topology_entry_t> _last_topology;
    std::vector<location_service_summary_t> _last_summaries;
};

} // namespace zlink::framework::runtime
