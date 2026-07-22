/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SOCKET_REQUEST_REPLY_WAIT_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SOCKET_REQUEST_REPLY_WAIT_INTERNAL_HPP_INCLUDED__

#include "api/socket/socket_request_reply_router_state_internal.hpp"
#include "api/socket/socket_request_reply_internal.hpp"

namespace zlink
{
namespace socket_reqrep_internal
{
inline void drain_router_completion_queues (
  void *router_,
  const std::shared_ptr<socket_request_reply_state_t> &socket_state_,
  const std::shared_ptr<zlink::reqrep_internal::router_request_reply_state_t>
    &router_reply_state_)
{
    if (socket_state_)
        (void) drain_reply_completions (socket_state_, router_);
    if (router_reply_state_) {
        (void) zlink::reqrep_internal::drain_router_reply_completions (router_reply_state_,
                                                                       router_);
    }
}

inline int wait_router_input_or_completion (zlink::socket_base_t *input_,
                                            zlink::socket_base_t *socket_signal_,
                                            zlink::socket_base_t *router_reply_signal_,
                                            long timeout_ms_,
                                            bool *input_ready_out_,
                                            bool *socket_signal_ready_out_,
                                            bool *router_reply_signal_ready_out_)
{
    if (!input_ || !input_ready_out_ || !socket_signal_ready_out_
        || !router_reply_signal_ready_out_) {
        errno = EFAULT;
        return -1;
    }

    *input_ready_out_ = false;
    *socket_signal_ready_out_ = false;
    *router_reply_signal_ready_out_ = false;

    zlink::request_completion::wait_signal_t signals[2];
    size_t signal_count = 0;
    if (socket_signal_) {
        signals[signal_count].socket = socket_signal_;
        signals[signal_count].ready_out = socket_signal_ready_out_;
        ++signal_count;
    }
    if (router_reply_signal_) {
        signals[signal_count].socket = router_reply_signal_;
        signals[signal_count].ready_out = router_reply_signal_ready_out_;
        ++signal_count;
    }

    return zlink::request_completion::wait_input_or_signals (
      input_, signal_count > 0 ? signals : NULL, signal_count, timeout_ms_, input_ready_out_);
}
}
}

#endif
