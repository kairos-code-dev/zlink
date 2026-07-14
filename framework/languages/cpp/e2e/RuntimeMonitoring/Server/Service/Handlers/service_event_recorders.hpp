/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../Shared/monitoring_event_recorders.hpp"

#include <zlink/framework.hpp>

#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::runtime_monitoring::service
{

inline void record_throwing_socket_event (server::evidence_store_t &evidence,
                                          const zlink::framework::socket_event_payload_t &event)
{
    evidence.add ("monitor-throw|source=" + event.source_name
                  + "|kind=" + server::socket_kind_name (event.event));
    throw std::runtime_error ("monitoring dispatch failure for e2e");
}

} // namespace zlink::framework::e2e::runtime_monitoring::service
