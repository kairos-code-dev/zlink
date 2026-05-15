/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DATA_PLANE_LOOP_HPP_INCLUDED__
#define __ZLINK_SPOT_DATA_PLANE_LOOP_HPP_INCLUDED__

namespace zlink
{
class spot_node_t;
struct spot_runtime_t;
struct spot_data_plane_protocol_state_t;
struct spot_data_plane_runtime_state_t;

struct spot_data_plane_loop_t
{
    static int run_until_shutdown (spot_node_t *node_,
                                   spot_runtime_t *runtime_,
                                   spot_data_plane_runtime_state_t *state_,
                                   spot_data_plane_protocol_state_t *protocol_state_out_);
    static int run_once (spot_node_t *node_,
                         spot_runtime_t *runtime_,
                         spot_data_plane_runtime_state_t *state_,
                         spot_data_plane_protocol_state_t *protocol_state_,
                         uint64_t *next_bootstrap_ms_,
                         uint64_t *last_bootstrap_peer_version_,
                         bool *running_out_);
};
}

#endif
