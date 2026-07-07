/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DATA_PLANE_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_DATA_PLANE_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

#include "services/spot/data_plane/spot_data_plane_runtime_state.hpp"

namespace zlink
{
struct spot_runtime_t;

int spot_data_plane_configure_runtime_sockets (spot_runtime_t *runtime_,
                                               spot_data_plane_runtime_state_t *state_);
}

#endif
