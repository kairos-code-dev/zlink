/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_PUB_HPP_INCLUDED__
#define __ZLINK_SPOT_PUB_HPP_INCLUDED__

#include "core/msg.hpp"
#include "utils/macros.hpp"

namespace zlink
{
class spot_node_t;

class spot_pub_t
{
  public:
    explicit spot_pub_t (spot_node_t *node_);
    ~spot_pub_t ();

    bool check_tag () const;

    int publish (const char *topic_,
                 zlink_msg_t *parts_,
                 size_t part_count_,
                 int flags_);

    int set_socket_option (int option_,
                           const void *optval_,
                           size_t optvallen_);

    int destroy ();

  private:
    friend class spot_node_t;

    spot_node_t *_node;
    uint32_t _tag;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (spot_pub_t)
};
}

#endif
