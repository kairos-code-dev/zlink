/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

namespace zlink
{
namespace spot_actor_api_internal
{

struct queued_join_request_t
{
    queued_join_request_t () :
        actor (NULL),
        spot (NULL),
        target_node (NULL),
        remote (false),
        handler (NULL),
        userdata (NULL),
        replied (false),
        join_result_code (0),
        join_epoch (0),
        pending_target (NULL),
        indexed (false)
    {
        memset (&target_node_rid, 0, sizeof (target_node_rid));
        memset (&source_spot_rid, 0, sizeof (source_spot_rid));
        memset (&target_actor_ref, 0, sizeof (target_actor_ref));
    }

    ~queued_join_request_t ()
    {
        zlink::spot_clear_msg_parts (&message_parts);
        zlink::spot_clear_msg_parts (&reply_parts);
    }

    actor_handle_t *actor;
    spot_handle_t *spot;
    std::shared_ptr<spot_logical_state_t> spot_state;
    zlink::spot_node_t *target_node;
    bool remote;
    zlink_routing_id_t target_node_rid;
    zlink_routing_id_t source_spot_rid;
    zlink_actor_ref_t target_actor_ref;
    zlink_actor_join_spot_handler_fn handler;
    void *userdata;
    bool replied;
    int32_t join_result_code;
    uint64_t join_epoch;
    actor_handle_t *pending_target;
    zlink::spot_owned_msg_parts_t message_parts;
    zlink::spot_owned_msg_parts_t reply_parts;
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
    bool indexed;
};

spot_logical_state_t *join_queue_key (
  const std::shared_ptr<spot_logical_state_t> &state_);
spot_logical_state_t *join_queue_key (const queued_join_request_t *request_);

struct actor_join_state_t
{
    typedef std::map<spot_logical_state_t *,
                     std::deque<queued_join_request_t *> >
      queue_map_t;

    std::deque<queued_join_request_t *> &queue_for (
      spot_logical_state_t *key_);
    bool find_queue (spot_logical_state_t *key_, queue_map_t::iterator *it_);
    void erase_queue_if_empty (spot_logical_state_t *key_);
    void enqueue (queued_join_request_t *request_);
    bool peek_for_spot (spot_handle_t *spot_,
                        queued_join_request_t **request_out_);
    void remove_queued (queued_join_request_t *request_);
    void take_queue (spot_logical_state_t *key_,
                     std::deque<queued_join_request_t *> *pending_);
    void replace_live_spot (spot_handle_t *from_, spot_handle_t *to_);
    void collect_live_for_state (
      const std::shared_ptr<spot_logical_state_t> &state_,
      std::deque<queued_join_request_t *> *pending_) const;
    void drain_queued_for_stream (
      void *stream_, std::deque<queued_join_request_t *> *aborted_);
    void collect_live_for_stream (
      void *stream_,
      const std::deque<queued_join_request_t *> &already_aborted_,
      std::vector<queued_join_request_t *> *received_aborts_) const;
    bool spot_has_pending (spot_logical_state_t *key_) const;
    uint32_t pending_count_for_spot (spot_logical_state_t *key_) const;
    bool is_live (queued_join_request_t *request_) const;
    void mark_live (queued_join_request_t *request_);
    void unmark_live (queued_join_request_t *request_);
    bool actor_has_pending (const actor_handle_t *actor_) const;
    void increment_actor_pending (actor_handle_t *actor_);
    void decrement_actor_pending (actor_handle_t *actor_);
    void increment_spot_pending (spot_logical_state_t *key_);
    void decrement_spot_pending (spot_logical_state_t *key_);
    bool has_pending_remote_actor (zlink::spot_node_t *node_,
                                   const char *actor_id_) const;
    void track_pending_remote_actor (queued_join_request_t *request_);
    void untrack_pending_remote_actor (queued_join_request_t *request_);

    queue_map_t queues;
    std::set<queued_join_request_t *> live_requests;
    std::map<actor_handle_t *, size_t> pending_count_by_actor;
    std::set<std::pair<zlink::spot_node_t *, std::string> >
      pending_remote_actor_keys;
    std::map<spot_logical_state_t *, size_t> pending_count_by_spot;
};

}
}
