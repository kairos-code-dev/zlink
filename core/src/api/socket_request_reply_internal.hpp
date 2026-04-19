/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SOCKET_REQUEST_REPLY_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SOCKET_REQUEST_REPLY_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "api/internal_pair_queue_internal.hpp"
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

struct socket_request_reply_state_t
{
    explicit socket_request_reply_state_t (zlink::socket_base_t *socket_,
                                           int socket_type_);

    zlink::socket_base_t *socket;
    int socket_type;
    std::mutex mutex;
    uint32_t default_timeout_ms;
    uint64_t next_request_seq;
    std::unordered_set<uint64_t> pending_sequences;
    std::unordered_map<pending_key_t, pending_request_t, pending_key_hash_t>
      pending_requests;
    std::unordered_map<uint64_t, pending_key_t> pending_request_keys_by_seq;
    bool internal_dispatch_installed;
    zlink::internal_pair_queue::queue_t recv_queue;
};

extern thread_local zlink_routing_id_t g_router_recv_source_rid;
extern thread_local zlink_routing_id_t g_router_recv_source_spot_rid;

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
void cleanup_request_reply_socket (socket_handle_t handle_);
}
}

#endif
