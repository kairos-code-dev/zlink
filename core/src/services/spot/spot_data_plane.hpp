/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DATA_PLANE_HPP_INCLUDED__
#define __ZLINK_SPOT_DATA_PLANE_HPP_INCLUDED__

#include <stdint.h>

#include <string>

namespace zlink
{
class spot_node_t;
class socket_base_t;
struct spot_runtime_t;
struct spot_data_plane_protocol_state_t;
struct spot_data_plane_runtime_state_t;

class spot_data_plane_t
{
  public:
    static void thread_entry (void *arg_);
    static void close_socket_ptr (spot_node_t *node_, socket_base_t *&socket_);
    static void clear_runtime_socket_refs (spot_runtime_t *runtime_);
    static void reset_mesh_pub_budget_state (spot_runtime_t *runtime_);
    static int resolve_mesh_pub_sndhwm_default (const spot_runtime_t *runtime_);
    static void refresh_mesh_pub_sndhwm (
      spot_runtime_t *runtime_,
      socket_base_t *mesh_pub_,
      int *current_hwm_,
      uint64_t *last_budget_version_,
      std::string *last_bound_endpoint_);
    static int initialize_runtime (spot_node_t *node_,
                                   spot_runtime_t *runtime_,
                                   spot_data_plane_runtime_state_t *state_out_);
    static void teardown_runtime (
      spot_node_t *node_,
      spot_runtime_t *runtime_,
      spot_data_plane_runtime_state_t *state_,
      spot_data_plane_protocol_state_t *protocol_state_);

  private:
    static void run (spot_node_t *node_);
};
}

#endif
