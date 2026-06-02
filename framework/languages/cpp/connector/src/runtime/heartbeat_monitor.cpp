/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/heartbeat_monitor.hpp"

namespace zlink::stream_connector::detail
{

heartbeat_monitor_t::heartbeat_monitor_t (heartbeat_options_t options)
  : _options (options)
{
}

bool
heartbeat_monitor_t::enabled () const noexcept
{
  return _options.enabled;
}

} // namespace zlink::stream_connector::detail
