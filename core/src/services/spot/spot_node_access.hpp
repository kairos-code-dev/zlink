/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_NODE_ACCESS_HPP_INCLUDED__
#define __ZLINK_SPOT_NODE_ACCESS_HPP_INCLUDED__

#include <zlink.h>

#include <vector>

namespace zlink
{
class ctx_t;
class discovery_t;
struct spot_runtime_t;
class spot_internal_receiver_t;
class spot_node_t;

enum spot_node_monitor_subject_t
{
    spot_node_monitor_subject_none = 0,
    spot_node_monitor_subject_pub,
    spot_node_monitor_subject_internal_receiver
};

struct spot_node_access_t
{
    static void *create (ctx_t *ctx_);
    static spot_node_t *from_handle (void *node_);
    static int bind (spot_node_t *node_, const char *endpoint_);
    static int connect_peer (spot_node_t *node_, const char *peer_endpoint_);
    static int disconnect_peer (spot_node_t *node_,
                                const char *peer_endpoint_);
    static int begin_close_or_fail_busy (spot_node_t *node_);
    static void cancel_close (spot_node_t *node_);
    static int destroy (spot_node_t *node_);
    static void delete_handle (spot_node_t *node_);
    static int status_snapshot (spot_node_t *node_,
                                zlink_spot_node_status_t *out_);
    static int peers_snapshot (
      spot_node_t *node_,
      const zlink_spot_node_peer_filter_t *filter_,
      std::vector<zlink_spot_node_peer_entry_t> *out_);
    static int subjects_snapshot (
      spot_node_t *node_,
      const zlink_spot_node_subject_filter_t *filter_,
      std::vector<zlink_spot_node_subject_entry_t> *out_);
    static int attach_discovery (spot_node_t *node_, void *discovery_);
    static void *monitor_open (spot_node_t *node_,
                               zlink_spot_role_t role_,
                               int events_,
                               void **snapshot_subject_out_,
                               spot_node_monitor_subject_t *subject_kind_out_);
    static spot_runtime_t *runtime (spot_node_t *node_);
    static spot_internal_receiver_t *ensure_internal_receiver (spot_node_t *node_);
    static spot_internal_receiver_t *internal_receiver (spot_node_t *node_);
    static void wake_control_task (spot_node_t *node_);
    static void schedule_subscription_replay (spot_node_t *node_);
};
}

#endif
