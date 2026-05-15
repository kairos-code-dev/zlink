/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_API_SOCKET_REQUEST_REPLY_WAIT_INTERNAL_HPP_INCLUDED__
#define __ZLINK_API_SOCKET_REQUEST_REPLY_WAIT_INTERNAL_HPP_INCLUDED__

#include "api/spot/service_spot_request_reply_internal.hpp"
#include "api/socket/socket_request_reply_internal.hpp"
#include "core/socket_poller.hpp"

namespace zlink
{
namespace socket_reqrep_internal
{
inline void drain_router_completion_queues (
  void *router_,
  const std::shared_ptr<socket_request_reply_state_t> &socket_state_,
  const std::shared_ptr<zlink::spot_reqrep_internal::router_spot_request_reply_state_t>
    &router_spot_state_)
{
    if (socket_state_)
        (void) drain_reply_completions (socket_state_, router_);
    if (router_spot_state_) {
        (void) zlink::spot_reqrep_internal::drain_router_reply_completions (
          router_spot_state_, router_);
    }
}

inline int wait_router_input_or_completion (
  zlink::socket_base_t *input_,
  zlink::socket_base_t *socket_signal_,
  zlink::socket_base_t *router_spot_signal_,
  long timeout_ms_,
  bool *input_ready_out_,
  bool *socket_signal_ready_out_,
  bool *router_spot_signal_ready_out_)
{
    if (!input_ || !input_ready_out_ || !socket_signal_ready_out_
        || !router_spot_signal_ready_out_) {
        errno = EFAULT;
        return -1;
    }

    *input_ready_out_ = false;
    *socket_signal_ready_out_ = false;
    *router_spot_signal_ready_out_ = false;

    zlink::socket_poller_t poller;
    if (poller.add (input_, NULL, ZLINK_POLLIN) != 0)
        return -1;
    if (socket_signal_ && poller.add (socket_signal_, NULL, ZLINK_POLLIN) != 0)
        return -1;
    if (router_spot_signal_
        && poller.add (router_spot_signal_, NULL, ZLINK_POLLIN) != 0) {
        return -1;
    }

    zlink::socket_poller_t::event_t events[3];
    const int rc = poller.wait (events, 3, timeout_ms_);
    if (rc <= 0)
        return rc;

    for (int i = 0; i < rc; ++i) {
        if (events[i].socket == input_)
            *input_ready_out_ = true;
        else if (events[i].socket == socket_signal_)
            *socket_signal_ready_out_ = true;
        else if (events[i].socket == router_spot_signal_)
            *router_spot_signal_ready_out_ = true;
    }
    return rc;
}
}
}

#endif
