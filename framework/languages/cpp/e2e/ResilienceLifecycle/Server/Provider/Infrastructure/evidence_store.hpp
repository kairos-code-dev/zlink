/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/registry_messaging_contracts.hpp"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace zlink::framework::e2e::registry_messaging::provider
{

class evidence_store_t
{
  public:
    evidence_store_t (std::string provider_rid, std::string instance_id) :
        _provider_rid (std::move (provider_rid)), _instance_id (std::move (instance_id))
    {
    }

    void record (std::string marker, std::string value)
    {
        std::lock_guard lock (_mutex);
        _entries.push_back ({std::move (marker), _provider_rid, std::move (value)});
    }

    evidence_snapshot_t snapshot () const
    {
        std::lock_guard lock (_mutex);
        return {.provider_rid = _provider_rid, .entries = _entries};
    }

    const std::string &provider_rid () const
    {
        return _provider_rid;
    }

    const std::string &instance_id () const
    {
        return _instance_id;
    }

  private:
    mutable std::mutex _mutex;
    std::string _provider_rid;
    std::string _instance_id;
    std::vector<evidence_entry_t> _entries;
};

} // namespace zlink::framework::e2e::registry_messaging::provider
