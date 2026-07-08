/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DATA_PLANE_PENDING_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_DATA_PLANE_PENDING_INTERNAL_HPP_INCLUDED__

#include "services/spot/common/spot_message_parts_internal.hpp"
#include "services/spot/data_plane/spot_data_plane_runtime_state.hpp"

#include <deque>
#include <string>

namespace zlink
{
class socket_base_t;
struct spot_runtime_t;

struct spot_data_plane_pending_t
{
    static bool
    queue_has_room (size_t current_bytes_, size_t hard_limit_bytes_, size_t message_bytes_);
    static int copy_msg_parts_to_owned (const spot_owned_msg_parts_t &src_,
                                        spot_owned_msg_parts_t *dst_);
    static int resolve_fanout_hwm (spot_runtime_t *runtime_);
    static void release_local_pending_ref (spot_data_plane_runtime_state_t *state_,
                                           uint64_t message_id_);
    static void drop_local_target_state (spot_data_plane_runtime_state_t *state_,
                                         uint64_t attachment_id_);
    static void release_mesh_pending_ref (spot_data_plane_runtime_state_t *state_,
                                          uint64_t message_id_);
    static void close_remote_target_socket (spot_runtime_t *runtime_,
                                            const std::string &route_endpoint_,
                                            socket_base_t *&socket_);
    static void drop_remote_target_state (spot_runtime_t *runtime_,
                                          spot_data_plane_runtime_state_t *state_,
                                          const std::string &endpoint_);
    static bool
    enqueue_local_target_message (spot_data_plane_runtime_state_t *state_,
                                  spot_data_plane_pending_state_t::local_target_state_t *target_,
                                  const std::string &topic_,
                                  const spot_owned_msg_parts_t &parts_,
                                  uint64_t *message_id_inout_,
                                  size_t precomputed_encoded_bytes_ = 0);
    static bool
    enqueue_mesh_pending (spot_data_plane_runtime_state_t *state_,
                          spot_data_plane_pending_state_t::remote_target_state_t *target_,
                          const std::string &topic_,
                          const spot_owned_msg_parts_t &parts_,
                          uint64_t *message_id_inout_);
    static bool enqueue_mesh_broadcast_pending (spot_data_plane_runtime_state_t *state_,
                                                const std::string &topic_,
                                                const spot_owned_msg_parts_t &parts_,
                                                uint64_t *message_id_inout_,
                                                size_t precomputed_encoded_bytes_ = 0);
    static bool stage_publish_message (
      std::deque<spot_data_plane_pending_state_t::staged_publish_entry_t> *queue_,
      const std::string &topic_,
      const spot_owned_msg_parts_t &parts_,
      bool need_local_,
      bool need_mesh_);
};
}

#endif
