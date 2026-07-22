/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SOCKET_REQUEST_REPLY_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SOCKET_REQUEST_REPLY_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

#include <memory>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "api/socket/internal_pair_queue_internal.hpp"
#include "api/socket/request_completion_queue_internal.hpp"
#include "api/socket/request_reply_runtime_core.hpp"
#include "api/socket/request_timeout_scheduler_internal.hpp"
#include "api/socket/socket_api_internal.hpp"

namespace zlink
{
class pipe_t;

namespace socket_reqrep_internal
{
struct pending_key_t
{
    std::string peer_rid;
    uint64_t request_seq;

    bool operator== (const pending_key_t &other_) const;
    bool operator< (const pending_key_t &other_) const;
};

struct pending_key_hash_t
{
    size_t operator() (const pending_key_t &key_) const;
};

struct pending_request_t
{
    pending_key_t key;
    zlink_reply_handler_fn handler;
    void *userdata;
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
};

struct dealer_reply_target_t
{
    dealer_reply_target_t ();

    zlink::pipe_t *pipe;
    uint64_t request_seq;
};

struct socket_request_reply_state_t : public zlink::request_reply_runtime::sequence_state_t
{
    explicit socket_request_reply_state_t (zlink::socket_base_t *socket_, int socket_type_);

    zlink::socket_base_t *socket;
    int socket_type;
    std::mutex mutex;
    std::unordered_map<pending_key_t, pending_request_t, pending_key_hash_t> pending_requests;
    std::unordered_map<uint64_t, pending_key_t> pending_request_keys_by_seq;
    std::unordered_map<uint64_t, dealer_reply_target_t> dealer_reply_targets;
    uint64_t dealer_next_reply_token;
    bool internal_dispatch_installed;
    zlink::internal_pair_queue::queue_t recv_queue;
    zlink::request_completion::queue_state_t completion;
};

struct router_recv_metadata_tls_t
{
    zlink_routing_id_t source_rid;
};

router_recv_metadata_tls_t &router_recv_metadata_tls ();

int validate_request_parts (zlink_msg_t *parts_, size_t part_count_);
int dispatch_router_message (socket_request_reply_state_t *state_,
                             const zlink_routing_id_t *source_node_rid_,
                             uint64_t request_seq_,
                             zlink_msg_t *parts_,
                             size_t part_count_);
int dispatch_dealer_message (socket_request_reply_state_t *state_,
                             uint8_t message_type_,
                             uint64_t request_seq_,
                             zlink::pipe_t *source_pipe_,
                             zlink_msg_t *parts_,
                             size_t part_count_);
uint64_t allocate_dealer_reply_token (socket_request_reply_state_t *state_);
int recv_internal_router_queue (zlink::internal_pair_queue::queue_t *queue_,
                                const zlink_routing_id_t **source_node_rid_out_,
                                uint64_t *request_seq_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                int flags_,
                                int timeout_ms_);
int recv_router_message_direct (socket_handle_t handle_,
                                const zlink_routing_id_t **source_node_rid_out_,
                                uint64_t *request_seq_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                int flags_);
int recv_internal_dealer_queue (zlink::internal_pair_queue::queue_t *queue_,
                                uint8_t *message_type_out_,
                                uint64_t *request_seq_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                int flags_,
                                int timeout_ms_);
int take_dealer_reply_target (const std::shared_ptr<socket_request_reply_state_t> &state_,
                              uint64_t request_token_,
                              dealer_reply_target_t *target_out_);
void restore_dealer_reply_target (const std::shared_ptr<socket_request_reply_state_t> &state_,
                                  uint64_t request_token_,
                                  const dealer_reply_target_t &target_);
int send_request_reply_message (void *socket_handle_,
                                const zlink_routing_id_t *peer_rid_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                zlink_send_flags_t flags_,
                                uint8_t message_type_,
                                uint64_t request_seq_);
std::shared_ptr<socket_request_reply_state_t>
find_or_create_request_reply_state (socket_handle_t handle_);
std::shared_ptr<socket_request_reply_state_t> find_request_reply_state (socket_handle_t handle_);
int ensure_completion_queue_ready (const std::shared_ptr<socket_request_reply_state_t> &state_);
int queue_reply_completion (const std::shared_ptr<socket_request_reply_state_t> &state_,
                            zlink_reply_handler_fn handler_,
                            void *userdata_,
                            int errnum_,
                            zlink_msg_t *parts_,
                            size_t part_count_);
int drain_reply_completions (const std::shared_ptr<socket_request_reply_state_t> &state_,
                             void *owner_handle_);
bool has_pending_reply_completions (const std::shared_ptr<socket_request_reply_state_t> &state_);
zlink::socket_base_t *
completion_signal_socket (const std::shared_ptr<socket_request_reply_state_t> &state_);
void claim_completion_owner (const std::shared_ptr<socket_request_reply_state_t> &state_);
bool current_thread_is_completion_owner (
  const std::shared_ptr<socket_request_reply_state_t> &state_);
bool in_socket_request_completion_callback (void *socket_);
inline void add_socket_pending_request_locked (socket_request_reply_state_t *state_,
                                               const pending_key_t &key_,
                                               const pending_request_t &pending_)
{
    if (!state_)
        return;

    state_->pending_sequences.insert (key_.request_seq);
    state_->pending_requests[key_] = pending_;
    state_->pending_request_keys_by_seq[key_.request_seq] = key_;
}

inline bool remove_socket_pending_request_locked (socket_request_reply_state_t *state_,
                                                  const pending_key_t &key_,
                                                  bool allow_sequence_fallback_,
                                                  pending_request_t *pending_out_)
{
    if (!state_)
        return false;

    std::unordered_map<pending_key_t, pending_request_t, pending_key_hash_t>::iterator it =
      state_->pending_requests.find (key_);
    if (it == state_->pending_requests.end () && allow_sequence_fallback_) {
        std::unordered_map<uint64_t, pending_key_t>::iterator seq_it =
          state_->pending_request_keys_by_seq.find (key_.request_seq);
        if (seq_it != state_->pending_request_keys_by_seq.end ())
            it = state_->pending_requests.find (seq_it->second);
    }

    if (it == state_->pending_requests.end ())
        return false;

    if (pending_out_)
        *pending_out_ = it->second;
    state_->pending_sequences.erase (it->first.request_seq);
    state_->pending_request_keys_by_seq.erase (it->first.request_seq);
    state_->pending_requests.erase (it);
    return true;
}

bool remove_socket_pending_request (const std::shared_ptr<socket_request_reply_state_t> &state_,
                                    const pending_key_t &key_,
                                    bool allow_sequence_fallback_,
                                    pending_request_t *pending_out_);
int schedule_socket_pending_timeout (
  const std::shared_ptr<socket_request_reply_state_t> &state_,
  const pending_key_t &key_,
  uint32_t timeout_ms_,
  std::shared_ptr<zlink::request_timeout::task_t> *task_out_);
void queue_socket_pending_timeout_completion (
  const std::shared_ptr<socket_request_reply_state_t> &state_, const pending_request_t &pending_);
int ensure_recv_queue_ready (const std::shared_ptr<socket_request_reply_state_t> &state_);
int ensure_internal_dispatch_installed (
  const std::shared_ptr<socket_request_reply_state_t> &state_);
int start_request (socket_handle_t handle_,
                   const zlink_routing_id_t *peer_rid_,
                   zlink_msg_t *parts_,
                   size_t part_count_,
                   zlink_send_flags_t flags_,
                   uint32_t timeout_ms_,
                   zlink_reply_handler_fn handler_,
                   void *userdata_);
bool has_pending_request_work (const std::shared_ptr<socket_request_reply_state_t> &state_);
int drain_close_request_reply_socket (socket_handle_t handle_);
void cleanup_request_reply_socket (socket_handle_t handle_);
}
}

#endif
