/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/diagnostics/monitoring_runtime.hpp"

#include <zlink/framework/contracts/eventing/events.hpp>

#include <map>
#include <memory>
#include <string>
#include <typeindex>
#include <utility>

namespace zlink::framework::runtime
{

/* Emits the runtime-metrics §4 instrument catalog through the existing
 * metric_event_payload_t monitoring surface (cpp-monitoring §8: same concept,
 * same mechanism — no per-instrument API). Emission folds to a single lookup
 * when no metric handler is subscribed, so hook points gate any tag/string
 * building (and latency clock reads) behind enabled(). */
class runtime_metrics_t
{
  public:
    explicit runtime_metrics_t (
      std::shared_ptr<framework::detail::monitoring_runtime_state_t> state) :
        _state (std::move (state))
    {
    }

    bool enabled () const noexcept
    {
        if (!_state) {
            return false;
        }
        const auto found = _state->handlers.find (std::type_index (typeid (metric_event_payload_t)));
        return found != _state->handlers.end () && !found->second.empty ();
    }

    void counter (std::string name,
                  std::string unit,
                  double delta,
                  std::map<std::string, std::string> tags = {}) const
    {
        emit (std::move (name), std::move (unit), delta, metric_instrument_kind_t::counter,
              metric_temporality_t::delta, std::move (tags));
    }

    void updown (std::string name,
                 std::string unit,
                 double delta,
                 std::map<std::string, std::string> tags = {}) const
    {
        emit (std::move (name), std::move (unit), delta, metric_instrument_kind_t::updown,
              metric_temporality_t::delta, std::move (tags));
    }

    void observable (std::string name,
                     std::string unit,
                     double current,
                     std::map<std::string, std::string> tags = {}) const
    {
        emit (std::move (name), std::move (unit), current, metric_instrument_kind_t::observable,
              metric_temporality_t::current, std::move (tags));
    }

    void histogram (std::string name,
                    std::string unit,
                    double sample,
                    std::map<std::string, std::string> tags = {}) const
    {
        emit (std::move (name), std::move (unit), sample, metric_instrument_kind_t::histogram,
              metric_temporality_t::sample, std::move (tags));
    }

  private:
    void emit (std::string name,
               std::string unit,
               double value,
               metric_instrument_kind_t instrument_kind,
               metric_temporality_t temporality,
               std::map<std::string, std::string> tags) const
    {
        if (!enabled ()) {
            return;
        }
        framework::detail::monitoring_runtime_t (_state).publish_metric (
          metric_event_payload_t{runtime_event_base_t{"zlink.framework"}, std::move (name), value,
                                 std::move (unit), instrument_kind, temporality,
                                 std::move (tags)});
    }

    std::shared_ptr<framework::detail::monitoring_runtime_state_t> _state;
};

} // namespace zlink::framework::runtime
