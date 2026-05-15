/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_PUB_HPP_INCLUDED__
#define __ZLINK_SPOT_PUB_HPP_INCLUDED__

#include "core/msg.hpp"
#include "utils/macros.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/mutex.hpp"

#include <atomic>
#include <string>

namespace zlink
{
class socket_base_t;
class spot_node_t;
struct spot_runtime_t;

class spot_pub_t
{
  public:
    spot_pub_t (spot_node_t *node_,
                socket_base_t *socket_,
                uint64_t attachment_id_,
                bool node_owned_default_ = false);
    ~spot_pub_t ();

    bool check_tag () const;
    bool is_node_owned_default () const;

    int publish (const char *topic_,
                 zlink_msg_t *parts_,
                 size_t part_count_,
                 int flags_);
    int set_option (int option_, const void *optval_, size_t optvallen_);
    int set_routing_id (const void *data_, size_t size_);
    int set_send_ready_handler (zlink_send_ready_handler_fn handler_,
                                void *subject_,
                                void *userdata_);
    int routing_id (zlink_routing_id_t *out_) const;
    int fill_monitor_snapshot (zlink_monitor_snapshot_t *out_) const;
    socket_base_t *poller_socket () const { return socket (); }
    socket_base_t *snapshot_socket () const { return socket (); }
    bool owns_socket (const socket_base_t *socket_) const;
    void invoke_send_ready_handler ();
    spot_node_t *node () const { return _node; }

    void emit_ready_event ();
    void dispatch_send_ready ();
    int destroy ();
    int destroy_from_node ();
    int abort_create ();

  private:
    int destroy_internal (bool allow_embedded_default_, bool notify_node_);
    void submit_error_summary (int error_code_);
    void lock_routing_id ();
    static int initialize_routing_id (zlink_routing_id_t *out_);
    socket_base_t *socket () const;

    spot_node_t *_node;
    socket_base_t *_socket;
    spot_runtime_t *_runtime;
    uint64_t _attachment_id;
    uint32_t _tag;
    bool _node_owned_default;
    mutable mutex_t _sync;
    mutex_t _publish_sync;
    zlink_routing_id_t _routing_id;
    std::atomic<bool> _routing_id_locked;
    std::atomic<zlink_send_ready_handler_fn> _send_ready_handler;
    std::atomic<void *> _send_ready_subject;
    std::atomic<void *> _send_ready_userdata;
    std::atomic<bool> _destroying;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (spot_pub_t)
};
}

#endif
