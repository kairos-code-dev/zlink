/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/heartbeat_monitor.hpp"

namespace zlink::stream_connector::detail
{

heartbeat_monitor_t::heartbeat_monitor_t (heartbeat_options_t options) : _options (options)
{
}

bool heartbeat_monitor_t::enabled () const noexcept
{
    return _options.enabled;
}

bool heartbeat_monitor_t::due (std::chrono::steady_clock::time_point last_sent,
                               std::chrono::steady_clock::time_point now) const noexcept
{
    if (!_options.enabled) {
        return false;
    }
    return last_sent == std::chrono::steady_clock::time_point{}
           || now - last_sent >= _options.interval;
}

bool heartbeat_monitor_t::timed_out (std::chrono::steady_clock::time_point last_inbound,
                                     std::chrono::steady_clock::time_point now) const noexcept
{
    if (!_options.enabled || last_inbound == std::chrono::steady_clock::time_point{}) {
        return false;
    }
    return now - last_inbound >= _options.timeout;
}

} // namespace zlink::stream_connector::detail
