/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/stream_connector/contracts/zlink_stream_connector_options.hpp>

#include <chrono>

namespace zlink::stream_connector::detail
{

class heartbeat_monitor_t
{
  public:
    explicit heartbeat_monitor_t (heartbeat_options_t options);
    bool enabled () const noexcept;
    bool due (std::chrono::steady_clock::time_point last_sent,
              std::chrono::steady_clock::time_point now) const noexcept;
    bool timed_out (std::chrono::steady_clock::time_point last_inbound,
                    std::chrono::steady_clock::time_point now) const noexcept;

  private:
    heartbeat_options_t _options;
};

} // namespace zlink::stream_connector::detail
