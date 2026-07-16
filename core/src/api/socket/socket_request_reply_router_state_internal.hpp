/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SOCKET_REQUEST_REPLY_ROUTER_STATE_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SOCKET_REQUEST_REPLY_ROUTER_STATE_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <stdint.h>

#include "api/socket/request_completion_queue_internal.hpp"
#include "api/socket/request_reply_runtime_core.hpp"
#include "api/socket/request_timeout_scheduler_internal.hpp"

//  Per-socket asynchronous request/reply state for raw ROUTER sockets.
//  Owns the pending-reply table keyed by request sequence and the completion
//  queue that delivers zlink_reply_handler_fn callbacks. DEALER sockets keep
//  their equivalent state in socket_request_reply_internal.hpp.
namespace zlink
{
class socket_base_t;

namespace reqrep_internal
{
struct pending_reply_t
{
    zlink_reply_handler_fn handler;
    void *userdata;
    uint64_t deadline_ns;
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
};

struct router_request_reply_request_state_t : public zlink::request_reply_runtime::sequence_state_t
{
    router_request_reply_request_state_t ();
    std::unordered_map<uint64_t, pending_reply_t> pending_replies;
};

struct router_request_reply_state_t
{
    explicit router_request_reply_state_t (void *owner_);

    void *owner;
    std::string router_rid;
    std::mutex mutex;
    router_request_reply_request_state_t requests;
    zlink::request_completion::queue_state_t completion;
};

typedef std::unordered_map<std::string, std::weak_ptr<router_request_reply_state_t>>
  router_state_identity_index_t;

std::mutex &request_reply_index_mutex ();
router_state_identity_index_t &router_state_identity_index ();

//  Returns the socket's router request/reply state, creating it on first use.
//  Returns an empty pointer when the handle is not a live socket.
std::shared_ptr<router_request_reply_state_t> find_or_create_router_state (void *router_);

//  Registers a pending reply for a submitted request and schedules its
//  timeout. Returns 0 on success or -1 with errno set.
int register_router_pending_request (const std::shared_ptr<router_request_reply_state_t> &state_,
                                     uint64_t request_seq_,
                                     uint32_t timeout_ms_,
                                     zlink_reply_handler_fn handler_,
                                     void *userdata_);

int validate_request_parts (zlink_msg_t *parts_, size_t part_count_);
int init_buffer_frame (zlink_msg_t *msg_, const void *data_, size_t size_);

int ensure_router_completion_queue_ready (
  const std::shared_ptr<router_request_reply_state_t> &state_);
int queue_router_reply_completion (const std::shared_ptr<router_request_reply_state_t> &state_,
                                   zlink_reply_handler_fn handler_,
                                   void *userdata_,
                                   int errnum_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_);
int drain_router_reply_completions (const std::shared_ptr<router_request_reply_state_t> &state_,
                                    void *owner_handle_);
bool has_router_reply_completions (const std::shared_ptr<router_request_reply_state_t> &state_);
zlink::socket_base_t *
router_completion_signal_socket (const std::shared_ptr<router_request_reply_state_t> &state_);
void claim_router_completion_owner (const std::shared_ptr<router_request_reply_state_t> &state_);
bool current_thread_is_router_completion_owner (
  const std::shared_ptr<router_request_reply_state_t> &state_);

//  True while the calling thread runs a reply completion callback whose
//  owner handle is handle_. Close paths use this to defer destruction.
bool in_request_completion_callback (void *handle_);

bool has_pending_router_request_work (
  const std::shared_ptr<router_request_reply_state_t> &state_);

//  Fails outstanding requests with ETERM and drains their completions.
int drain_close_router_request_reply_state (void *router_);

//  Releases the socket's router request/reply state during close.
void cleanup_router_request_reply_state (void *router_);

//  Sends an already-assembled multipart record on the socket. Consumes the
//  parts on success and failure, matching the raw part send contract.
int send_combined_parts_on_socket (zlink::socket_base_t *socket_,
                                   std::vector<zlink_msg_t> *parts_,
                                   zlink_send_flags_t flags_);
}
}

#endif
