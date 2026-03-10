/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DATA_PLANE_HPP_INCLUDED__
#define __ZLINK_SPOT_DATA_PLANE_HPP_INCLUDED__

namespace zlink
{
class spot_node_t;

class spot_data_plane_t
{
  public:
    static void thread_entry (void *arg_);

  private:
    static void run (spot_node_t *node_);
};
}

#endif
