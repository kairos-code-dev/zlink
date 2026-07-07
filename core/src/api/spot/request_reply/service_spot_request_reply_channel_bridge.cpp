/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/spot/request_reply/service_spot_request_reply_channel_bridge_internal.hpp"

#include <memory>

#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "api/socket/socket_api_internal.hpp"
#include "api/socket/socket_request_reply_internal.hpp"
#include "api/message/request_result_internal.hpp"
#include "api/message/submit_result_internal.hpp"

namespace
{
namespace reqrep = zlink::socket_reqrep_internal;

using zlink::spot_reqrep_internal::find_or_create_spot_state;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;

struct channel_reply_bridge_ctx_t
{
    std::weak_ptr<spot_request_reply_state_t> state;
    void *dealer;
    zlink_reply_handler_fn handler;
    void *userdata;
};

void decrement_pending_channel_requests (const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    if (!state_)
        return;

    std::lock_guard<std::mutex> lock (state_->mutex);
    if (state_->completion_state.pending_channel_requests > 0)
        --state_->completion_state.pending_channel_requests;
}

void channel_reply_bridge_completion (zlink_request_result_t result_,
                                      zlink_msg_t *parts_,
                                      size_t part_count_,
                                      void *userdata_)
{
    std::unique_ptr<channel_reply_bridge_ctx_t> bridge (
      static_cast<channel_reply_bridge_ctx_t *> (userdata_));
    if (!bridge.get ())
        return;

    std::shared_ptr<spot_request_reply_state_t> state = bridge->state.lock ();
    if (!state)
        return;

    decrement_pending_channel_requests (state);

    const int errnum = zlink::request_result_internal::to_errno (result_);
    (void) zlink::spot_reqrep_internal::queue_spot_channel_reply_completion (
      state, bridge->dealer, bridge->handler, bridge->userdata, errnum, parts_, part_count_);
}
}

zlink_submit_result_t
zlink::spot_reqrep_internal::start_spot_channel_request_bridge (void *spot_,
                                                                socket_base_t *router_,
                                                                zlink_msg_t *parts_,
                                                                size_t part_count_,
                                                                zlink_reply_handler_fn handler_,
                                                                void *userdata_,
                                                                zlink_send_flags_t flags_,
                                                                uint32_t timeout_ms_)
{
    std::shared_ptr<spot_request_reply_state_t> state = find_or_create_spot_state (spot_);
    if (!state)
        return submit_result_internal::from_errno (errno);

    {
        std::lock_guard<std::mutex> lock (state->mutex);
        if (state->completion_state.channel_reply_sources.count (router_) == 0) {
            state->completion_state.channel_reply_sources[router_] =
              std::shared_ptr<spot_channel_reply_source_t> (
                new spot_channel_reply_source_t (router_));
        }
        ++state->completion_state.pending_channel_requests;
    }

    std::shared_ptr<reqrep::socket_request_reply_state_t> socket_state =
      reqrep::find_or_create_request_reply_state (make_socket_handle (router_));
    reqrep::register_spot_channel_dispatch_observer (socket_state, spot_);

    std::unique_ptr<channel_reply_bridge_ctx_t> bridge (new (std::nothrow)
                                                          channel_reply_bridge_ctx_t ());
    if (!bridge.get ()) {
        decrement_pending_channel_requests (state);
        errno = ENOMEM;
        return submit_result_internal::from_errno (errno);
    }
    bridge->state = state;
    bridge->dealer = router_;
    bridge->handler = handler_;
    bridge->userdata = userdata_;

    const int rc =
      reqrep::start_request (make_socket_handle (router_), NULL, parts_, part_count_, flags_,
                             timeout_ms_, &channel_reply_bridge_completion, bridge.release ());
    if (rc != 0)
        decrement_pending_channel_requests (state);

    return submit_result_internal::from_request_submit_rc (rc);
}
