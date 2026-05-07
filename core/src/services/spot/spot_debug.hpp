/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_SPOT_DEBUG_HPP_INCLUDED__
#define __ZLINK_SERVICES_SPOT_DEBUG_HPP_INCLUDED__

#include "utils/debug_log.hpp"

namespace zlink
{
namespace spot_debug
{
static const char ready_ack_log_path[] = "/tmp/zlink_spot_ready_ack.log";
static const char ctrl_log_path[] = "/tmp/zlink_spot_ctrl.log";
static const char sub_diag_log_path[] = "/tmp/zlink_spot_sub_diag.log";

inline void route_publish_log_path (int process_id_, char *out_, size_t size_)
{
    if (!out_ || size_ == 0)
        return;
    std::snprintf (out_, size_, "/tmp/zlink_spot_route_publish_%d.log",
                   process_id_);
}

inline bool enabled (const char *env_)
{
    return debug_env_enabled (env_);
}

inline bool shutdown_enabled ()
{
    return enabled ("ZLINK_DEBUG_SPOT_SHUTDOWN");
}

inline void control (const char *fmt_, ...)
{
    va_list args;
    va_start (args, fmt_);
    debug_vfprintf ("ZLINK_DEBUG_SPOT_CONTROL", "[spot-control] ", fmt_,
                    args);
    va_end (args);
}

inline void ready_ack (const char *prefix_, const char *fmt_, ...)
{
    va_list args;
    va_start (args, fmt_);
    debug_vfprintf_with_file ("ZLINK_DEBUG_SPOT_READY_ACK", prefix_,
                              ready_ack_log_path, fmt_, args);
    va_end (args);
}

inline void ctrl (const char *fmt_, ...)
{
    va_list args;
    va_start (args, fmt_);
    debug_vfprintf_with_file ("ZLINK_SPOT_CTRL_DEBUG", "[spot-ctrl] ",
                              ctrl_log_path, fmt_, args);
    va_end (args);
}
}
}

#endif
