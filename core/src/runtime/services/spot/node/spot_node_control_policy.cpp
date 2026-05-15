/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node_control_policy.hpp"

#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/pubsub/spot_sub.hpp"

namespace zlink
{
namespace spot_node_control_policy
{
uint32_t resolve_effective_ready_count (uint32_t ready_count_,
                                        uint32_t active_peer_count_,
                                        uint32_t connected_ready_count_)
{
    const uint32_t effective_peer_count =
      active_peer_count_ > connected_ready_count_ ? active_peer_count_
                                                  : connected_ready_count_;
    if (effective_peer_count == 0)
        return ready_count_;
    return clamp_ready_peer_count (ready_count_, effective_peer_count);
}

unsigned int subscription_ready_holdoff_ticks (
  const std::set<std::string> &connected_endpoints_)
{
    for (std::set<std::string>::const_iterator it =
           connected_endpoints_.begin ();
         it != connected_endpoints_.end (); ++it) {
        if (it->compare (0, 6, "wss://") == 0)
            return 500;
        if (it->compare (0, 6, "tls://") == 0)
            return 150;
    }

    return 50;
}

unsigned int subscription_replay_attempt_count (
  const std::set<std::string> &connected_endpoints_)
{
    for (std::set<std::string>::const_iterator it =
           connected_endpoints_.begin ();
         it != connected_endpoints_.end (); ++it) {
        if (it->compare (0, 6, "wss://") == 0)
            return 40;
        if (it->compare (0, 6, "tls://") == 0)
            return 20;
        if (it->compare (0, 5, "ws://") == 0)
            return 10;
    }

    return 10;
}

unsigned int subscription_replay_holdoff_ticks (
  const std::set<std::string> &connected_endpoints_)
{
    for (std::set<std::string>::const_iterator it =
           connected_endpoints_.begin ();
         it != connected_endpoints_.end (); ++it) {
        if (it->compare (0, 6, "wss://") == 0)
            return 2;
        if (it->compare (0, 6, "tls://") == 0)
            return 2;
        if (it->compare (0, 5, "ws://") == 0)
            return 2;
    }

    return 5;
}

unsigned int pub_delivery_ready_holdoff_ticks (
  const std::set<std::string> &connected_endpoints_)
{
    for (std::set<std::string>::const_iterator it =
           connected_endpoints_.begin ();
         it != connected_endpoints_.end (); ++it) {
        if (it->compare (0, 6, "wss://") == 0)
            return 50;
        if (it->compare (0, 6, "tls://") == 0)
            return 15;
    }

    return 20;
}

std::string make_ready_ack_arg (const std::string &target_endpoint_,
                                const std::string &raw_filter_,
                                const std::string &ack_source_id_)
{
    return target_endpoint_ + "\n" + raw_filter_ + "\n" + ack_source_id_;
}

void collect_replay_raw_filters (const std::vector<spot_sub_t *> &subs_,
                                 std::set<std::string> *out_)
{
    if (!out_)
        return;

    out_->clear ();
    for (size_t i = 0; i < subs_.size (); ++i) {
        if (subs_[i])
            subs_[i]->append_replay_raw_filters (out_);
    }
}
}
}
