/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_COMMON_SERVICE_MODE_STATE_HPP_INCLUDED__
#define __ZLINK_SERVICES_COMMON_SERVICE_MODE_STATE_HPP_INCLUDED__

#include "utils/mutex.hpp"

namespace zlink
{
struct service_mode_state_t
{
    service_mode_state_t () :
        receive_callback_active (false),
        send_ready_active (false),
        pollin_refs (0),
        pollout_refs (0)
    {
    }

    mutex_t sync;
    bool receive_callback_active;
    bool send_ready_active;
    int pollin_refs;
    int pollout_refs;
};
}

#endif
