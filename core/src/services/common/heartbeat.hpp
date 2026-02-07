/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_COMMON_HEARTBEAT_HPP_INCLUDED__
#define __ZLINK_SERVICES_COMMON_HEARTBEAT_HPP_INCLUDED__

namespace zlink
{
namespace services
{
struct heartbeat_config_t
{
    unsigned int interval_ms;
    unsigned int timeout_ms;
};
}
}

#endif
