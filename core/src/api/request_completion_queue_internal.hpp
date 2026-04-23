/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_REQUEST_COMPLETION_QUEUE_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_REQUEST_COMPLETION_QUEUE_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "api/internal_pair_queue_internal.hpp"
#include "core/ctx.hpp"

namespace zlink
{
namespace request_completion
{
struct queued_completion_t
{
    queued_completion_t ();
    ~queued_completion_t ();

    queued_completion_t (queued_completion_t &&other_) noexcept;
    queued_completion_t &operator= (queued_completion_t &&other_) noexcept;

    zlink_reply_handler_fn handler;
    void *userdata;
    int errnum;
    std::vector<zlink_msg_t> parts;

  private:
    queued_completion_t (const queued_completion_t &);
    queued_completion_t &operator= (const queued_completion_t &);
};

struct queue_state_t
{
    queue_state_t ();

    std::mutex mutex;
    std::deque<queued_completion_t> pending;
    zlink::internal_pair_queue::queue_t signal;
    std::thread::id owner_thread;
    bool owner_thread_valid;
    bool signal_pending;
};

int ensure_signal_ready (queue_state_t *state_,
                         zlink::ctx_t *ctx_,
                         const char *prefix_);
int enqueue (queue_state_t *state_,
             zlink::ctx_t *ctx_,
             const char *prefix_,
             zlink_reply_handler_fn handler_,
             void *userdata_,
             int errnum_,
             zlink_msg_t *parts_,
             size_t part_count_);
int drain (queue_state_t *state_, void *owner_handle_);
void close (queue_state_t *state_);
void claim_owner_thread (queue_state_t *state_);
bool current_thread_is_owner (queue_state_t *state_);
bool has_pending (queue_state_t *state_);
zlink::socket_base_t *signal_socket (queue_state_t *state_);
bool in_request_completion_callback (void *owner_handle_);
int wait_input_or_signal (zlink::socket_base_t *input_,
                          zlink::socket_base_t *signal_,
                          long timeout_ms_,
                          bool *input_ready_out_,
                          bool *signal_ready_out_);
}
}

#endif
