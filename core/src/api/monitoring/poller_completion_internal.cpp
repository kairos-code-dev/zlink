/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/monitoring/poller_completion_internal.hpp"

#include "api/socket/socket_request_reply_router_state_internal.hpp"
#include "api/socket/socket_request_reply_internal.hpp"

bool poller_subject_is_completion (poller_subject_kind_t subject_kind_)
{
    return subject_kind_ == poller_subject_socket_request_completion
           || subject_kind_ == poller_subject_router_request_completion;
}

bool poller_completion_is_hidden (const poller_registration_t *registration_)
{
    return registration_ && poller_subject_is_completion (registration_->subject_kind);
}

int poller_completion_drain_hidden (const poller_registration_t *registration_)
{
    if (!registration_) {
        errno = EFAULT;
        return -1;
    }

    switch (registration_->subject_kind) {
        case poller_subject_socket_request_completion: {
            socket_handle_t handle = as_socket_handle (registration_->subject);
            std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t> state =
              registration_->state_ref
                ? std::static_pointer_cast<
                    zlink::socket_reqrep_internal::socket_request_reply_state_t> (
                    registration_->state_ref)
                : zlink::socket_reqrep_internal::find_request_reply_state (handle);
            return state ? zlink::socket_reqrep_internal::drain_reply_completions (
                             state, registration_->subject)
                         : 0;
        }
        case poller_subject_router_request_completion: {
            socket_handle_t handle = as_socket_handle (registration_->subject);
            if (!handle.socket && !registration_->state_ref)
                return -1;
            std::shared_ptr<zlink::reqrep_internal::router_request_reply_state_t> state =
              registration_->state_ref
                ? std::static_pointer_cast<zlink::reqrep_internal::router_request_reply_state_t> (
                    registration_->state_ref)
                : handle.socket->router_request_reply_state ();
            return state ? zlink::reqrep_internal::drain_router_reply_completions (
                             state, registration_->subject)
                         : 0;
        }
        default:
            return 0;
    }
}
