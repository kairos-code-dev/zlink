/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_RUNTIME_EXECUTION_HPP_INCLUDED__
#define __ZLINK_SPOT_RUNTIME_EXECUTION_HPP_INCLUDED__

#include "services/spot/data_plane/spot_data_plane_internal.hpp"

namespace zlink
{
struct spot_control_runtime_state_t
{
    spot_control_runtime_state_t () :
        task_id (0),
        connected_peer_version_seen (0)
    {
    }

    uint64_t task_id;
    uint64_t connected_peer_version_seen;
};

struct spot_runtime_execution_state_t
{
    spot_runtime_execution_state_t () :
        data_plane_running (false),
        next_bootstrap_ms (0),
        last_bootstrap_peer_version (UINT64_MAX)
    {
    }

    bool data_plane_running;
    uint64_t next_bootstrap_ms;
    uint64_t last_bootstrap_peer_version;
    spot_data_plane_runtime_state_t data_plane_state;
    spot_data_plane_protocol_state_t data_plane_protocol_state;
    spot_control_runtime_state_t control_state;
    spot_mesh_peer_state_t mesh_peer_state;
};
}

#endif
