/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/stream_connector/contracts/zlink_stream_connector_options.hpp>

namespace zlink::stream_connector::detail
{

class heartbeat_monitor_t
{
public:
  explicit heartbeat_monitor_t (heartbeat_options_t options);
  bool enabled () const noexcept;

private:
  heartbeat_options_t _options;
};

} // namespace zlink::stream_connector::detail
