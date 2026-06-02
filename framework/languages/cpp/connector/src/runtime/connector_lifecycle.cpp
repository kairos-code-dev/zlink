/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/connector_lifecycle.hpp"

#include <utility>

namespace zlink::stream_connector::detail
{

connector_lifecycle_t::connector_lifecycle_t (
  std::shared_ptr<connector_state_t> state)
  : _state (std::move (state))
{
}

void
connector_lifecycle_t::close ()
{
  change_state (_state, connection_state_t::closed);
}

} // namespace zlink::stream_connector::detail
