/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/connector_runtime.hpp"

namespace zlink::stream_connector::detail
{

class typed_handler_registry_t
{
public:
  explicit typed_handler_registry_t (connector_state_t &state) : _state (state) {}
  std::size_t packet_name_count () const noexcept
  {
    return _state.packet_handlers.size ();
  }

private:
  connector_state_t &_state;
};

} // namespace zlink::stream_connector::detail
