/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/monitoring/poller_completion_internal.hpp"

#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "api/socket/socket_request_reply_internal.hpp"

bool poller_completion_is_hidden (const poller_registration_t *registration_)
{
    if (!registration_)
        return false;
    return registration_->subject_kind == poller_subject_socket_request_completion
           || registration_->subject_kind
                == poller_subject_router_spot_request_completion
           || registration_->subject_kind == poller_subject_spot_request_completion;
}

int poller_completion_drain_hidden (
  const poller_registration_t *registration_)
{
    if (!registration_) {
        errno = EFAULT;
        return -1;
    }

    switch (registration_->subject_kind) {
        case poller_subject_socket_request_completion: {
            socket_handle_t handle = as_socket_handle (registration_->subject);
            std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t>
              state =
                registration_->state_ref
                  ? std::static_pointer_cast<
                      zlink::socket_reqrep_internal::socket_request_reply_state_t> (
                      registration_->state_ref)
                  : zlink::socket_reqrep_internal::find_request_reply_state (
                      handle);
            return state
                     ? zlink::socket_reqrep_internal::drain_reply_completions (
                         state, registration_->subject)
                     : 0;
        }
        case poller_subject_router_spot_request_completion: {
            socket_handle_t handle = as_socket_handle (registration_->subject);
            if (!handle.socket && !registration_->state_ref)
                return -1;
            std::shared_ptr<zlink::spot_reqrep_internal::router_spot_request_reply_state_t>
              state =
                registration_->state_ref
                  ? std::static_pointer_cast<
                      zlink::spot_reqrep_internal::router_spot_request_reply_state_t> (
                      registration_->state_ref)
                  : handle.socket->router_spot_request_reply_state ();
            return state
                     ? zlink::spot_reqrep_internal::drain_router_reply_completions (
                         state, registration_->subject)
                     : 0;
        }
        case poller_subject_spot_request_completion: {
            std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t>
              state =
                registration_->state_ref
                  ? std::static_pointer_cast<
                      zlink::spot_reqrep_internal::spot_request_reply_state_t> (
                      registration_->state_ref)
                  : zlink::spot_reqrep_internal::try_find_spot_state (
                      registration_->subject);
            if (!state)
                return 0;
            int drained =
              zlink::spot_reqrep_internal::drain_attached_channel_reply_bridge_progress (
                state);
            if (drained < 0)
                return -1;
            const int direct_rc =
              zlink::spot_reqrep_internal::drain_spot_reply_completions (
                state, registration_->subject);
            if (direct_rc < 0)
                return -1;
            drained += direct_rc;
            std::vector<void *> dealers;
            zlink::spot_reqrep_internal::snapshot_spot_channel_reply_dealers (
              state, &dealers);
            for (size_t i = 0; i < dealers.size (); ++i) {
                const int rc =
                  zlink::spot_reqrep_internal::drain_spot_channel_reply_completions_from (
                    state, registration_->subject, dealers[i]);
                if (rc < 0 && errno != ENOENT)
                    return -1;
                if (rc > 0)
                    drained += rc;
            }
            zlink::spot_reqrep_internal::run_spot_dispatch_worker_once (
              registration_->subject);
            errno = 0;
            return drained;
        }
        default:
            return 0;
    }
}
