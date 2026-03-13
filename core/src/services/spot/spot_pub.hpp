/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_PUB_HPP_INCLUDED__
#define __ZLINK_SPOT_PUB_HPP_INCLUDED__

#include "core/msg.hpp"
#include "utils/macros.hpp"
#include "core/thread.hpp"
#include "services/common/service_monitor.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/mutex.hpp"
#include "../../../external/moodycamel/concurrentqueue.h"

#include <atomic>
#include <string>

namespace zlink
{
class socket_base_t;
class spot_node_t;

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
    int set_send_ready_handler (zlink_send_ready_handler_fn handler_,
                                void *subject_);
    int routing_id (zlink_routing_id_t *out_) const;
    int fill_monitor_snapshot (zlink_monitor_snapshot_t *out_) const;
    void *monitor_open (int events_);
    void invoke_send_ready_for_testing ();
    void emit_delivery_ready_changed_event (const char *subject_,
                                            bool include_subject_kind_,
                                            uint32_t subject_kind_,
                                            uint32_t ready_count_);
    void emit_first_delivery_ready_changed_event (const char *subject_,
                                                  bool include_subject_kind_,
                                                  uint32_t subject_kind_,
                                                  uint32_t ready_count_);

    void emit_ready_event ();
    void dispatch_send_ready ();
    int destroy ();
    int destroy_from_node ();
    int abort_create ();

  private:
    static void monitor_thread_main (void *arg_);
    void monitor_loop ();
    int destroy_internal (bool allow_embedded_default_, bool notify_node_);
    void submit_error_summary (int error_code_);
    int ensure_monitor_bridge_started ();
    int stop_monitor_bridge ();
    void emit_monitor_event (const zlink_service_event_t &event_);
    void lock_routing_id ();
    static int initialize_routing_id (zlink_routing_id_t *out_);
    socket_base_t *socket () const;

    spot_node_t *_node;
    socket_base_t *_socket;
    uint64_t _attachment_id;
    uint32_t _tag;
    bool _node_owned_default;
    mutable mutex_t _sync;
    zlink_routing_id_t _routing_id;
    bool _routing_id_locked;
    std::atomic<zlink_send_ready_handler_fn> _send_ready_handler;
    std::atomic<void *> _send_ready_subject;
    service_monitor_hub_t _monitor;
    moodycamel::ConcurrentQueue<zlink_service_event_t> _monitor_event_queue;
    std::atomic<bool> _monitor_event_draining;
    std::atomic<uint32_t> _monitor_event_pending;
    void *_raw_monitor_socket;
    thread_t _monitor_thread;
    atomic_counter_t _monitor_stop;
    bool _monitor_thread_started;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (spot_pub_t)
};
}

#endif
