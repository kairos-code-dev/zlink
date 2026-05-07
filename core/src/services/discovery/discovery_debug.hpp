/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_DISCOVERY_DEBUG_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_DISCOVERY_DEBUG_HPP_INCLUDED__

#include "utils/debug_log.hpp"

namespace zlink
{
inline void discovery_debugf (const char *fmt_, ...)
{
    va_list args;
    va_start (args, fmt_);
    debug_vfprintf ("ZLINK_DISCOVERY_DEBUG", "[discovery] ", fmt_, args);
    va_end (args);
}
}

#endif
