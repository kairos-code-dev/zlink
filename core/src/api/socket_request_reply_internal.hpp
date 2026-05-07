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

#include "api/internal_pair_queue_internal.hpp"
#include "api/request_completion_queue_internal.hpp"
#include "api/request_reply_runtime_core.hpp"
#include "api/request_timeout_scheduler_internal.hpp"
#include "api/socket_api_internal.hpp"

namespace zlink
{
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

struct socket_request_reply_state_t :
    public zlink::request_reply_runtime::sequence_state_t
{
    explicit socket_request_reply_state_t (zlink::socket_base_t *socket_,
                                           int socket_type_);

    zlink::socket_base_t *socket;
    int socket_type;
    std::mutex mutex;
    std::unordered_map<pending_key_t, pending_request_t, pending_key_hash_t>
      pending_requests;
    std::unordered_map<uint64_t, pending_key_t> pending_request_keys_by_seq;
    std::set<void *> spot_channel_dispatch_observers;
    bool internal_dispatch_installed;
    zlink::internal_pair_queue::queue_t recv_queue;
    zlink::request_completion::queue_state_t completion;
};

struct router_recv_metadata_tls_t
{
    zlink_routing_id_t source_rid;
    zlink_routing_id_t source_spot_rid;
};

router_recv_metadata_tls_t &router_recv_metadata_tls ();

bool has_valid_routing_id (const zlink_routing_id_t *peer_rid_);
std::string routing_id_key (const zlink_routing_id_t *peer_rid_);
int validate_request_parts (zlink_msg_t *parts_, size_t part_count_);
int dispatch_router_message (socket_request_reply_state_t *state_,
                             const zlink_routing_id_t *source_node_rid_,
                             const zlink_routing_id_t *source_spot_rid_,
                             uint64_t request_seq_,
                             zlink_msg_t *parts_,
                             size_t part_count_);
int recv_internal_router_queue (zlink::internal_pair_queue::queue_t *queue_,
                                const zlink_routing_id_t **source_node_rid_out_,
                                const zlink_routing_id_t **source_spot_rid_out_,
                                uint64_t *request_seq_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                int flags_,
                                int timeout_ms_);
int recv_router_message_direct (socket_handle_t handle_,
                                const zlink_routing_id_t **source_node_rid_out_,
                                const zlink_routing_id_t **source_spot_rid_out_,
                                uint64_t *request_seq_out_,
                                zlink_msg_t **parts_out_,
                                size_t *part_count_out_,
                                int flags_);
int send_request_reply_message (void *socket_handle_,
                                const zlink_routing_id_t *peer_rid_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                zlink_send_flags_t flags_,
                                uint8_t message_type_,
                                uint64_t request_seq_);
std::shared_ptr<socket_request_reply_state_t>
find_or_create_request_reply_state (socket_handle_t handle_);
std::shared_ptr<socket_request_reply_state_t>
find_request_reply_state (socket_handle_t handle_);
int ensure_completion_queue_ready (
  const std::shared_ptr<socket_request_reply_state_t> &state_);
int queue_reply_completion (
  const std::shared_ptr<socket_request_reply_state_t> &state_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  int errnum_,
  zlink_msg_t *parts_,
  size_t part_count_);
int drain_reply_completions (
  const std::shared_ptr<socket_request_reply_state_t> &state_,
  void *owner_handle_);
bool has_pending_reply_completions (
  const std::shared_ptr<socket_request_reply_state_t> &state_);
zlink::socket_base_t *completion_signal_socket (
  const std::shared_ptr<socket_request_reply_state_t> &state_);
void claim_completion_owner (
  const std::shared_ptr<socket_request_reply_state_t> &state_);
bool current_thread_is_completion_owner (
  const std::shared_ptr<socket_request_reply_state_t> &state_);
bool in_socket_request_completion_callback (void *socket_);
void register_spot_channel_dispatch_observer (
  const std::shared_ptr<socket_request_reply_state_t> &state_,
  void *spot_);
void unregister_spot_channel_dispatch_observer (
  const std::shared_ptr<socket_request_reply_state_t> &state_,
  void *spot_);
int ensure_recv_queue_ready (
  const std::shared_ptr<socket_request_reply_state_t> &state_);
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
bool has_pending_request_work (
  const std::shared_ptr<socket_request_reply_state_t> &state_);
int drain_close_request_reply_socket (socket_handle_t handle_);
void cleanup_request_reply_socket (socket_handle_t handle_);
}
}

#endif
