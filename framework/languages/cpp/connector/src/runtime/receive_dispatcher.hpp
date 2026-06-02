/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/connector_runtime.hpp"

namespace zlink::stream_connector::detail
{

class receive_dispatcher_t
{
public:
  explicit receive_dispatcher_t (connector_state_t &state) : _state (state) {}
  void enqueue (packet_t packet);

private:
  connector_state_t &_state;
};

} // namespace zlink::stream_connector::detail
