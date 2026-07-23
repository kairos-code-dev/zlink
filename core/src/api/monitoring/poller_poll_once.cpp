/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <vector>

#include "api/core/config_result_internal.hpp"
#include "api/monitoring/poller_api_internal.hpp"
#include "api/socket/socket_request_reply_internal.hpp"

int zlink_poll (zlink_pollitem_t *items_,
                int nitems_,
                long timeout_,
                zlink_config_result_t *error_out_)
{
    if (nitems_ < 0 || (nitems_ > 0 && !items_)) {
        errno = EINVAL;
        if (error_out_)
            *error_out_ = ZLINK_CONFIG_INVALID_ARGUMENT;
        return -1;
    }
    if (nitems_ == 0) {
        if (error_out_)
            *error_out_ = ZLINK_CONFIG_OK;
        return 0;
    }

    zlink::socket_poller_t poller;
    std::vector<std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t>>
      receive_states;
    int native_capacity = nitems_;
    for (int i = 0; i < nitems_; ++i) {
        items_[i].revents = 0;
        void *index_user_data = poller_index_user_data (static_cast<size_t> (i));
        if (items_[i].socket) {
            socket_handle_t handle = as_socket_handle (items_[i].socket);
            if (!handle.socket) {
                if (error_out_)
                    *error_out_ = ZLINK_CONFIG_INVALID_HANDLE;
                return -1;
            }
            if (validate_socket_callback_poller_events (handle, items_[i].events) != 0) {
                if (error_out_)
                    *error_out_ = ZLINK_CONFIG_INVALID_ARGUMENT;
                return -1;
            }
            short physical_events = items_[i].events;
            std::shared_ptr<zlink::socket_reqrep_internal::socket_request_reply_state_t>
              request_state;
            bool dispatch_installed = false;
            if (socket_type (handle) == ZLINK_CORE_SOCKET_DEALER) {
                request_state =
                  (items_[i].events & ZLINK_POLLIN) != 0
                    ? zlink::socket_reqrep_internal::find_or_create_request_reply_state (handle)
                    : zlink::socket_reqrep_internal::find_request_reply_state (handle);
                if ((items_[i].events & ZLINK_POLLIN) != 0
                    && (!request_state
                        || zlink::socket_reqrep_internal::ensure_internal_dispatch_installed (
                             request_state)
                             != 0)) {
                    if (error_out_)
                        *error_out_ = zlink::config_result_internal::from_errno (errno);
                    return -1;
                }
                if (request_state) {
                    std::lock_guard<std::mutex> lock (request_state->mutex);
                    dispatch_installed = request_state->internal_dispatch_installed;
                }
                if (dispatch_installed)
                    physical_events = static_cast<short> (physical_events & ~ZLINK_POLLIN);
            }
            if (poller.add (handle.socket, index_user_data, physical_events) != 0) {
                if (error_out_)
                    *error_out_ = zlink::config_result_internal::from_errno (errno);
                return -1;
            }
            if (socket_type (handle) == ZLINK_CORE_SOCKET_DEALER
                && (items_[i].events & ZLINK_POLLIN) != 0) {
                std::shared_ptr<
                  zlink::socket_reqrep_internal::socket_request_reply_state_t> state =
                  request_state;
                if (dispatch_installed
                    && poller.add (state->recv_queue.rx_socket (), index_user_data,
                                   ZLINK_POLLIN)
                         != 0) {
                    if (error_out_)
                        *error_out_ = zlink::config_result_internal::from_errno (errno);
                    return -1;
                }
                if (dispatch_installed) {
                    receive_states.push_back (state);
                    ++native_capacity;
                }
            }
        } else if (poller.add_fd (items_[i].fd, index_user_data, items_[i].events) != 0) {
            if (error_out_)
                *error_out_ = zlink::config_result_internal::from_errno (errno);
            return -1;
        }
    }

    std::vector<zlink::socket_poller_t::event_t> events (
      static_cast<size_t> (native_capacity));
    const int rc = poller.wait (events.data (), native_capacity, timeout_);
    if (rc < 0) {
        if (error_out_)
            *error_out_ = zlink::config_result_internal::from_errno (errno);
        return rc;
    }
    if (rc == 0) {
        if (error_out_)
            *error_out_ = ZLINK_CONFIG_OK;
        return 0;
    }
    for (int i = 0; i < rc; ++i) {
        size_t index = 0;
        if (poller_index_from_user_data (events[i].user_data, static_cast<size_t> (nitems_),
                                         &index)) {
            items_[static_cast<int> (index)].revents =
              static_cast<short> (items_[static_cast<int> (index)].revents | events[i].events);
            continue;
        }
        poller_set_pollitem_revents_by_identity (items_, nitems_, events[i]);
    }
    int public_count = 0;
    for (int i = 0; i < nitems_; ++i) {
        if (items_[i].revents != 0) {
            if (items_[i].socket) {
                socket_handle_t handle = as_socket_handle (items_[i].socket);
                const short other_events =
                  static_cast<short> (items_[i].events & ~ZLINK_POLLIN);
                uint32_t ready_events = 0;
                if (handle.socket && other_events != 0
                    && handle.socket->get_events (other_events, &ready_events) == 0) {
                    items_[i].revents =
                      static_cast<short> (items_[i].revents | ready_events);
                }
            }
            ++public_count;
        }
    }
    if (error_out_)
        *error_out_ = ZLINK_CONFIG_OK;
    return public_count;
}
