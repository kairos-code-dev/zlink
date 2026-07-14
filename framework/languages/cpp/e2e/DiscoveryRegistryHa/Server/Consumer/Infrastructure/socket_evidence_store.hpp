/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../../Shared/store_failure_contracts.hpp"

#include <zlink/framework.hpp>

#include <mutex>
#include <string>
#include <vector>

namespace zlink::framework::e2e::store_failure::consumer
{

class socket_evidence_store_t
{
  public:
    void record (const zlink::framework::socket_event_payload_t &event)
    {
        std::string kind;
        switch (event.event) {
            case zlink::framework::socket_event_kind_t::connected:
                kind = "Connected";
                break;
            case zlink::framework::socket_event_kind_t::connection_ready:
                kind = "ConnectionReady";
                break;
            case zlink::framework::socket_event_kind_t::disconnected:
                kind = "Disconnected";
                break;
            default:
                return;
        }
        std::lock_guard lock (_gate);
        _entries.push_back ({std::move (kind), event.remote_address});
    }

    std::vector<socket_evidence_entry_t> snapshot () const
    {
        std::lock_guard lock (_gate);
        return _entries;
    }

  private:
    mutable std::mutex _gate;
    std::vector<socket_evidence_entry_t> _entries;
};

} // namespace zlink::framework::e2e::store_failure::consumer
