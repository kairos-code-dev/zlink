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
#include "../../../external/moodycamel/concurrentqueue.h"

#include <atomic>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace zlink
{
class socket_base_t;
class spot_node_t;
struct spot_runtime_t;

typedef void (*spot_sub_direct_handler_fn) (
  const zlink_routing_id_t *source_rid_,
  const char *topic_,
  size_t topic_len_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_);

class spot_sub_t
{
  public:
    struct subject_descriptor_t
    {
        subject_descriptor_t ();

        std::string subject;
        uint32_t subject_kind;
    };

    struct direct_handler_binding_t
    {
        direct_handler_binding_t () : handler (NULL), userdata (NULL) {}

        spot_sub_direct_handler_fn handler;
        void *userdata;
    };

    spot_sub_t (spot_node_t *node_,
                socket_base_t *socket_,
                uint64_t attachment_id_,
                bool node_owned_default_ = false);
    ~spot_sub_t ();

    bool check_tag () const;
    bool is_node_owned_default () const;

    int subscribe (const char *topic_);
    int subscribe_pattern (const char *pattern_);
    int unsubscribe (const char *topic_or_pattern_);
    int set_option (int option_, const void *optval_, size_t optvallen_);
    int routing_id (zlink_routing_id_t *out_) const;
    int fill_monitor_snapshot (zlink_monitor_snapshot_t *out_) const;
    void *monitor_open (int events_);
    socket_base_t *poller_socket () const { return socket (); }
    int set_direct_handler (spot_sub_direct_handler_fn handler_,
                            void *userdata_);
    int recv (zlink_routing_id_t *source_rid_out_,
              zlink_msg_t **parts_,
              size_t *part_count_,
              int flags_,
              char *topic_out_,
              size_t *topic_len_);
    bool has_filters () const;
    void append_raw_filters (std::set<std::string> *out_) const;
    void append_replay_raw_filters (std::set<std::string> *out_) const;
    void append_all_subjects (std::vector<subject_descriptor_t> *out_) const;
    void append_subjects_for_raw_filter (
      const std::string &raw_filter_,
      std::vector<subject_descriptor_t> *out_) const;
    void emit_filter_applied_event (const char *subject_,
                                    uint32_t subject_kind_);
    void emit_subscription_ready_event (const char *endpoint_,
                                        const char *subject_,
                                        uint32_t subject_kind_);
    void mark_subject_subscription_ready (const subject_descriptor_t &subject_,
                                          const char *endpoint_);
    void emit_delivery_ready_changed_event (const char *subject_,
                                            uint32_t subject_kind_,
                                            uint32_t ready_,
                                            const char *endpoint_);
    void mark_subject_ready (const subject_descriptor_t &subject_,
                             const char *endpoint_);
    void backfill_subject_ready_endpoint (const subject_descriptor_t &subject_,
                                          const char *endpoint_);
    void mark_subject_lost (const subject_descriptor_t &subject_,
                            const char *endpoint_);
    void mark_all_subjects_lost (const char *endpoint_);
    std::string first_ready_peer_endpoint () const;
    void send_ready_ack_lost_for_endpoint (const char *endpoint_);

    void emit_ready_event ();
    int destroy ();
    int destroy_from_node ();
    int abort_create ();

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
    static void dispatch_from_io (const zlink_routing_id_t *source_rid_,
                                  const char *topic_,
                                  size_t topic_len_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  void *userdata_);
    static void monitor_thread_main (void *arg_);
    void monitor_loop ();
    int destroy_internal (bool allow_embedded_default_, bool notify_node_);
    int ensure_monitor_bridge_started ();
    int stop_monitor_bridge ();
    void handle_ready_probe (const std::string &raw_filter_,
                             const std::string &peer_endpoint_);
    std::string ready_ack_source_id () const;
    void release_ready_ack_endpoints (const std::string &raw_filter_,
                                      std::vector<std::string> *out_);
    void release_all_ready_ack_endpoints (
      std::vector<std::pair<std::string, std::string> > *out_);
    void emit_monitor_event (const zlink_service_event_t &event_);
    void lock_routing_id ();
    socket_base_t *socket () const;

    spot_node_t *_node;
    socket_base_t *_socket;
    spot_runtime_t *_runtime;
    uint64_t _attachment_id;
    uint32_t _tag;
    bool _node_owned_default;

    mutable mutex_t _sync;
    zlink_routing_id_t _routing_id;
    bool _routing_id_locked;

    std::set<std::string> _topics;
    std::set<std::string> _patterns;
    std::set<std::string> _delivery_ready_raw_filters;
    std::set<std::string> _ready_peer_endpoints;
    std::map<std::string, std::string> _ready_subject_endpoints;
    std::map<std::string, std::set<std::string> > _ready_ack_endpoints;

    direct_handler_binding_t _direct_handler_bindings[2];
    unsigned int _direct_handler_binding_index;
    std::atomic<direct_handler_binding_t *> _active_direct_handler;
    std::atomic<handler_state_t> _handler_state;
    atomic_counter_t _callback_inflight;
    condition_variable_t _callback_cv;
    std::atomic<bool> _destroying;
    service_monitor_hub_t _monitor;
    moodycamel::ConcurrentQueue<zlink_service_event_t> _monitor_event_queue;
    std::atomic<bool> _monitor_event_draining;
    std::atomic<uint32_t> _monitor_event_pending;
    void *_raw_monitor_socket;
    thread_t _monitor_thread;
    atomic_counter_t _monitor_stop;
    bool _monitor_thread_started;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (spot_sub_t)
};
}

#endif
