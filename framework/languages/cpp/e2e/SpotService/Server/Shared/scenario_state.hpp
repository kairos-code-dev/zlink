/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/spot_service_contracts.hpp"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

class scenario_state_t
{
  public:
    explicit scenario_state_t (std::string node_rid) : node_rid (std::move (node_rid)) {}

    void record (std::string marker,
                 std::string actor_id = {},
                 std::string spot_rid = {},
                 std::string value = {})
    {
        std::lock_guard lock (_mutex);
        entries.push_back ({std::move (marker), node_rid, std::move (actor_id),
                            std::move (spot_rid), std::move (value)});
    }

    zlink::framework::e2e::spot_service::evidence_snapshot_t snapshot () const
    {
        std::lock_guard lock (_mutex);
        return {.node_rid = node_rid, .entries = entries};
    }

    std::string node_rid;

  private:
    mutable std::mutex _mutex;
    std::vector<zlink::framework::e2e::spot_service::evidence_entry_t> entries;
};
