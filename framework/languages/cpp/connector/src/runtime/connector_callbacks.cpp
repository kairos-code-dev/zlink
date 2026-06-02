/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/connector_callbacks.hpp"

namespace zlink::stream_connector::detail
{

void
connector_callbacks_t::publish_error (const error_t &error) const
{
  for (const auto &handler : _state.error_handlers) {
    handler (error);
  }
}

} // namespace zlink::stream_connector::detail
