/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace zlink::framework::runtime::client_server
{

struct weighted_candidate_t
{
    std::string key;
    std::uint32_t weight = 0;
};

class smooth_weighted_selector_t
{
  public:
    std::optional<std::string> select (
      std::span<const weighted_candidate_t> candidates)
    {
        std::int64_t total = 0;
        std::map<std::string, std::uint32_t> active;
        for (const auto &candidate : candidates) {
            if (candidate.weight == 0)
                continue;
            if (total
                > std::numeric_limits<std::int64_t>::max ()
                    - candidate.weight) {
                throw std::overflow_error (
                  "ClientServer selection weight total exceeds int64");
            }
            total += candidate.weight;
            active.insert_or_assign (
              candidate.key, candidate.weight);
        }
        for (auto it = _credits.begin (); it != _credits.end ();) {
            if (!active.contains (it->first))
                it = _credits.erase (it);
            else
                ++it;
        }
        if (active.empty ())
            return std::nullopt;

        auto selected = active.end ();
        for (auto it = active.begin (); it != active.end (); ++it) {
            auto &credit = _credits[it->first];
            credit = std::clamp (credit, -total, total);
            credit += static_cast<std::int64_t> (it->second);
            if (selected == active.end ()
                || credit > _credits[selected->first])
                selected = it;
        }
        _credits[selected->first] -= total;
        for (auto &[_, credit] : _credits)
            credit = std::clamp (credit, -total, total);
        return selected->first;
    }

    std::size_t state_size () const noexcept { return _credits.size (); }

    std::int64_t maximum_absolute_credit () const noexcept
    {
        std::int64_t maximum = 0;
        for (const auto &[_, credit] : _credits)
            maximum = std::max (
              maximum, credit < 0 ? -credit : credit);
        return maximum;
    }

  private:
    std::map<std::string, std::int64_t> _credits;
};

} // namespace zlink::framework::runtime::client_server
