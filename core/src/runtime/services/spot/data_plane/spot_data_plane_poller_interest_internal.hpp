/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DATA_PLANE_POLLER_INTEREST_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_DATA_PLANE_POLLER_INTEREST_INTERNAL_HPP_INCLUDED__

#include "services/spot/data_plane/spot_data_plane_runtime_state.hpp"

namespace zlink
{
struct spot_data_plane_poller_interest_t
{
    static void refresh_fixed (spot_data_plane_runtime_state_t *state_);
    static void
    refresh_local_target (spot_data_plane_runtime_state_t *state_,
                          spot_data_plane_runtime_state_t::local_target_state_t *target_);
    static void
    refresh_remote_target (spot_data_plane_runtime_state_t *state_,
                           spot_data_plane_runtime_state_t::remote_target_state_t *target_);
    static void refresh_all (spot_data_plane_runtime_state_t *state_);
};
}

#endif
