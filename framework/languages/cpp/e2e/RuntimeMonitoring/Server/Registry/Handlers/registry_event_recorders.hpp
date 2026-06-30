/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/evidence_store.hpp"

#include <zlink/framework.hpp>

#include <string>

namespace zlink::framework::e2e::runtime_monitoring::registry
{

inline std::string registry_kind_name (zlink::framework::registry_event_kind_t kind)
{
    switch (kind) {
        case zlink::framework::registry_event_kind_t::status_changed:
            return "StatusChanged";
        case zlink::framework::registry_event_kind_t::topology_changed:
            return "TopologyChanged";
        case zlink::framework::registry_event_kind_t::service_summary_changed:
            return "ServiceSummaryChanged";
    }
    return "Unknown";
}

inline void record_registry_event (server::evidence_store_t &evidence,
                                   const zlink::framework::registry_event_payload_t &event)
{
    evidence.add ("monitor-registry|source=" + event.source_name
                  + "|kind=" + registry_kind_name (event.event)
                  + "|topology=" + std::to_string (event.topology.size ())
                  + "|summary=" + std::to_string (event.service_summary.size ()));
}

} // namespace zlink::framework::e2e::runtime_monitoring::registry
