/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../Shared/Endpoints/evidence_endpoint.hpp"
#include "../../Shared/Handlers/channel_control_ping_handler.hpp"

#include <zlink/framework.hpp>

inline void map_operational_endpoints (zlink::framework::http_options_builder_t &http)
{
    http.map_health ("/health")
      .map_get<evidence_handler_t> ("/evidence")
      .map_post<evidence_wait_handler_t> ("/evidence/wait")
      .map_post<channel_control_ping_route_handler_t> ("/channel/control-ping")
      .map_post<shutdown_handler_t> ("/shutdown")
      .map_post<crash_handler_t> ("/crash");
}
