/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_SUB_NODE_ACCESS_HPP_INCLUDED__
#define __ZLINK_SPOT_SUB_NODE_ACCESS_HPP_INCLUDED__

#include <string>

namespace zlink
{
class spot_sub_t;

struct spot_sub_node_access_t
{
    static int apply_aggregate_subscription (spot_sub_t *sub_,
                                             const std::string &raw_filter_,
                                             bool pattern_,
                                             bool subscribe_);
};
}

#endif
