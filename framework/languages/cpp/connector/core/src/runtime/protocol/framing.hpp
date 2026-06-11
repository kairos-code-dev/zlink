/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/connector_runtime.hpp"

namespace zlink::stream_connector::detail
{

void dispatch_packet (connector_state_t &state, const packet_t &packet);
void drain_available_pushes (connector_state_t &state);

} // namespace zlink::stream_connector::detail
