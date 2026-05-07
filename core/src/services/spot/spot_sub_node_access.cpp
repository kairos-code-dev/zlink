/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_sub_node_access.hpp"

#include "services/spot/spot_sub.hpp"

int zlink::spot_sub_node_access_t::apply_aggregate_subscription (
  spot_sub_t *sub_,
  const std::string &raw_filter_,
  bool pattern_,
  bool subscribe_)
{
    if (!sub_) {
        errno = EFAULT;
        return -1;
    }
    return sub_->apply_aggregate_subscription (
      raw_filter_, pattern_, subscribe_);
}
