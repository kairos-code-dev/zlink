/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/connector_runtime.hpp"

namespace zlink::stream_connector::detail
{

class pending_requests_t
{
public:
  explicit pending_requests_t (connector_state_t &state) : _state (state) {}
  std::size_t size () const noexcept { return _state.pending_requests.size (); }

private:
  connector_state_t &_state;
};

} // namespace zlink::stream_connector::detail
