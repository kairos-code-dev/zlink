/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_SUB_HPP_INCLUDED__
#define __ZLINK_SPOT_SUB_HPP_INCLUDED__

#include "core/msg.hpp"
#include "core/thread.hpp"
#include "services/common/service_monitor.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/condition_variable.hpp"
#include "utils/macros.hpp"
#include "utils/mutex.hpp"

#include <set>
#include <string>

namespace zlink
{
class socket_base_t;
class spot_node_t;

class spot_sub_t
{
  public:
    spot_sub_t (spot_node_t *node_, socket_base_t *socket_);
    ~spot_sub_t ();

    bool check_tag () const;

    int subscribe (const char *topic_);
    int subscribe_pattern (const char *pattern_);
    int unsubscribe (const char *topic_or_pattern_);
    int set_option (int option_, const void *optval_, size_t optvallen_);
    int set_routing_id (const void *data_, size_t size_);
    int routing_id (zlink_routing_id_t *out_) const;
    int peers (zlink_peer_info_t *peers_, size_t *count_) const;
    void *monitor_open (int events_);
    void *poller_socket ();
    int configured_rcvhwm () const;
    int set_handler (zlink_spot_sub_handler_fn handler_, void *userdata_);
    int recv (zlink_msg_t **parts_,
              size_t *part_count_,
              int flags_,
              char *topic_out_,
              size_t *topic_len_);
    bool has_filters () const;

    void emit_ready_event ();
    int destroy ();

  private:
    enum handler_state_t
    {
        handler_none = 0,
        handler_active,
        handler_clearing
    };

    static bool is_valid_topic (const char *topic_, std::string *out_);
    static bool is_valid_pattern (const char *pattern_, std::string *prefix_out_);
    static int initialize_routing_id (zlink_routing_id_t *out_);
    static void dispatch_from_io (const char *topic_,
                                  size_t topic_len_,
                                  const zlink_msg_t *parts_,
                                  size_t part_count_,
                                  void *userdata_);
    static void monitor_thread_main (void *arg_);
    void monitor_loop ();
    int ensure_monitor_bridge_started ();
    void stop_monitor_bridge ();
    void lock_routing_id ();

    spot_node_t *_node;
    socket_base_t *_socket;
    uint32_t _tag;

    mutable mutex_t _sync;
    zlink_routing_id_t _routing_id;
    bool _routing_id_locked;
    int _configured_rcvhwm;

    std::set<std::string> _topics;
    std::set<std::string> _patterns;

    zlink_spot_sub_handler_fn _handler;
    void *_handler_userdata;
    handler_state_t _handler_state;
    atomic_counter_t _callback_inflight;
    atomic_counter_t _recv_in_progress;
    condition_variable_t _callback_cv;
    service_monitor_hub_t _monitor;
    void *_raw_monitor_socket;
    thread_t _monitor_thread;
    atomic_counter_t _monitor_stop;
    bool _monitor_thread_started;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (spot_sub_t)
};
}

#endif
