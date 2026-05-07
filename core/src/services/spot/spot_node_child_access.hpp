/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_NODE_CHILD_ACCESS_HPP_INCLUDED__
#define __ZLINK_SPOT_NODE_CHILD_ACCESS_HPP_INCLUDED__

#include <stdint.h>
#include <string>

#include <zlink.h>

namespace zlink
{
class spot_node_t;
class spot_pub_t;
class spot_sub_t;
struct spot_runtime_t;

struct spot_node_child_access_t
{
    static spot_runtime_t *runtime (spot_node_t *node_);
    static int set_pub_option (spot_node_t *node_,
                               int option_,
                               const void *optval_,
                               size_t optvallen_);
    static bool is_shutting_down (const spot_node_t *node_);
    static void remove_spot_pub (spot_node_t *node_, spot_pub_t *pub_);
    static void remove_spot_sub (spot_node_t *node_, spot_sub_t *sub_);
    static int destroy_attachment (spot_node_t *node_, uint64_t attachment_id_);
    static int destroy_attachment_async (spot_node_t *node_,
                                         uint64_t attachment_id_);
    static void submit_pub_summary (spot_node_t *node_,
                                    spot_pub_t *pub_,
                                    uint16_t state_,
                                    int error_code_);
    static void submit_sub_summary (spot_node_t *node_,
                                    spot_sub_t *sub_,
                                    uint16_t state_,
                                    int error_code_);
    static int send_subscription_update (spot_node_t *node_,
                                         const std::string &raw_filter_,
                                         bool subscribe_);
    static int send_ready_ack_update (spot_node_t *node_,
                                      const std::string &target_endpoint_,
                                      const std::string &raw_filter_,
                                      const std::string &ack_source_id_,
                                      bool subscribe_);
    static void schedule_subscription_replay (spot_node_t *node_);
    static void note_local_sub_filters_changed (spot_node_t *node_,
                                                bool had_filters_,
                                                bool has_filters_);
    static bool has_active_peers (const spot_node_t *node_);
    static void notify_subscription_forwarded (spot_node_t *node_,
                                               const std::string &raw_filter_);
    static void mark_subject_changed (spot_node_t *node_,
                                      const std::string &subject_,
                                      uint32_t subject_kind_);
};
}

#endif
