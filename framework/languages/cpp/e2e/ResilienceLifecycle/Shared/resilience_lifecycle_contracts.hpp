/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "registry_messaging_contracts.hpp"

namespace zlink::framework::e2e::resilience_lifecycle
{

inline constexpr const char *api_channel = registry_messaging::api_channel;
inline constexpr const char *workflow_channel = registry_messaging::workflow_channel;
inline constexpr const char *route_channel = registry_messaging::route_channel;
inline constexpr const char *dealer_channel = registry_messaging::dealer_channel;
inline constexpr const char *handler_group = registry_messaging::handler_group;

using profile_req_t = registry_messaging::profile_req_t;
using profile_res_t = registry_messaging::profile_res_t;
using profile_msg_t = registry_messaging::profile_msg_t;
using payload_req_t = registry_messaging::payload_req_t;
using payload_res_t = registry_messaging::payload_res_t;
using request_failure_res_t = registry_messaging::request_failure_res_t;
using operation_status_t = registry_messaging::operation_status_t;
using workflow_req_t = registry_messaging::workflow_req_t;
using workflow_res_t = registry_messaging::workflow_res_t;
using scenario_route_req_t = registry_messaging::scenario_route_req_t;
using scenario_route_res_t = registry_messaging::scenario_route_res_t;
using evidence_entry_t = registry_messaging::evidence_entry_t;
using evidence_snapshot_t = registry_messaging::evidence_snapshot_t;

} // namespace zlink::framework::e2e::resilience_lifecycle
