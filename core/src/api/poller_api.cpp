/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <new>
#include <vector>

#include "api/close_result_internal.hpp"
#include "api/config_result_internal.hpp"
#include "api/poller_api_internal.hpp"
#include "api/timer_api_internal.hpp"

namespace
{
int fill_public_poller_event (poller_handle_t *poller_,
                              const zlink::socket_poller_t::event_t &native_,
                              zlink_poller_event_t *event_out_)
{
    if (!poller_ || !event_out_) {
        errno = EFAULT;
        return -1;
    }

    memset (event_out_, 0, sizeof (*event_out_));
    event_out_->fd = 0;

    for (size_t i = 0; i < poller_->registrations.size (); ++i) {
        const poller_registration_t &registration = poller_->registrations[i];
        if (registration.socket && registration.socket == native_.socket) {
            event_out_->source_kind = ZLINK_POLLER_SOURCE_SOCKET;
            event_out_->socket = native_.socket;
            event_out_->fd = native_.fd;
            event_out_->timer = NULL;
            event_out_->user_data = native_.user_data;
            event_out_->events = native_.events;
            return 0;
        }
        if (!registration.socket && registration.fd == native_.fd) {
            if (registration.subject_kind == poller_subject_timer) {
                event_out_->source_kind = ZLINK_POLLER_SOURCE_TIMER;
                event_out_->socket = NULL;
                event_out_->fd = native_.fd;
                event_out_->timer = registration.subject;
                event_out_->user_data = native_.user_data;
                event_out_->events = native_.events;
                return 0;
            }

            event_out_->source_kind = ZLINK_POLLER_SOURCE_FD;
            event_out_->socket = NULL;
            event_out_->fd = native_.fd;
            event_out_->timer = NULL;
            event_out_->user_data = native_.user_data;
            event_out_->events = native_.events;
            return 0;
        }
    }

    event_out_->source_kind =
      native_.socket ? ZLINK_POLLER_SOURCE_SOCKET : ZLINK_POLLER_SOURCE_FD;
    event_out_->socket = native_.socket;
    event_out_->fd = native_.fd;
    event_out_->timer = NULL;
    event_out_->user_data = native_.user_data;
    event_out_->events = native_.events;
    return 0;
}
}

int poller_add_registration (poller_handle_t *poller_,
                             zlink::socket_base_t *socket_,
                             void *user_data_,
                             short events_,
                             void *subject_,
                             poller_subject_kind_t subject_kind_)
{
    if (!poller_ || !socket_) {
        errno = EFAULT;
        return -1;
    }
    if (poller_->poller.add (socket_, user_data_, events_) != 0)
        return -1;

    poller_registration_t registration;
    registration.socket = static_cast<void *> (socket_);
    registration.fd = zlink::retired_fd;
    registration.subject = subject_;
    registration.subject_kind = subject_kind_;
    registration.user_data = user_data_;
    registration.events = events_;
    poller_->registrations.push_back (registration);
    return 0;
}

int poller_add_fd_registration (poller_handle_t *poller_,
                                zlink_fd_t fd_,
                                void *user_data_,
                                short events_,
                                void *subject_,
                                poller_subject_kind_t subject_kind_)
{
    if (!poller_) {
        errno = EFAULT;
        return -1;
    }
    if (poller_->poller.add_fd (fd_, user_data_, events_) != 0)
        return -1;

    poller_registration_t registration;
    registration.socket = NULL;
    registration.fd = fd_;
    registration.subject = subject_;
    registration.subject_kind = subject_kind_;
    registration.user_data = user_data_;
    registration.events = events_;
    poller_->registrations.push_back (registration);
    return 0;
}

int poller_find_registration_index (poller_handle_t *poller_, void *subject_)
{
    if (!poller_)
        return -1;
    for (size_t i = 0; i < poller_->registrations.size (); ++i) {
        if (poller_->registrations[i].subject == subject_)
            return static_cast<int> (i);
    }
    return -1;
}

int poller_find_registration_index (poller_handle_t *poller_,
                                    void *subject_,
                                    poller_subject_kind_t subject_kind_)
{
    if (!poller_)
        return -1;
    for (size_t i = 0; i < poller_->registrations.size (); ++i) {
        if (poller_->registrations[i].subject == subject_
            && poller_->registrations[i].subject_kind == subject_kind_) {
            return static_cast<int> (i);
        }
    }
    return -1;
}

int poller_find_fd_registration_index (poller_handle_t *poller_,
                                       zlink_fd_t fd_,
                                       poller_subject_kind_t subject_kind_)
{
    if (!poller_)
        return -1;
    for (size_t i = 0; i < poller_->registrations.size (); ++i) {
        if (!poller_->registrations[i].socket
            && poller_->registrations[i].fd == fd_
            && poller_->registrations[i].subject_kind == subject_kind_) {
            return static_cast<int> (i);
        }
    }
    return -1;
}

int poller_remove_registration_at (poller_handle_t *poller_, int index_)
{
    if (!poller_ || index_ < 0
        || static_cast<size_t> (index_) >= poller_->registrations.size ()) {
        errno = EINVAL;
        return -1;
    }

    const poller_registration_t registration =
      poller_->registrations[static_cast<size_t> (index_)];
    const int rc = registration.socket
                     ? poller_->poller.remove (
                         static_cast<zlink::socket_base_t *> (
                           registration.socket))
                     : poller_->poller.remove_fd (registration.fd);
    if (rc == 0) {
        release_poller_registration (registration);
        poller_->registrations.erase (poller_->registrations.begin () + index_);
    }
    return rc;
}

int poller_remove_all_registrations_for_subject (poller_handle_t *poller_,
                                                 void *subject_)
{
    if (!poller_) {
        errno = EFAULT;
        return -1;
    }

    bool removed = false;
    while (true) {
        const int index = poller_find_registration_index (poller_, subject_);
        if (index < 0)
            break;
        if (poller_remove_registration_at (poller_, index) != 0)
            return -1;
        removed = true;
    }

    if (!removed) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

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
    for (int i = 0; i < nitems_; ++i) {
        items_[i].revents = 0;
        if (items_[i].socket) {
            socket_handle_t handle = as_socket_handle (items_[i].socket);
            if (!handle.socket) {
                if (error_out_)
                    *error_out_ = ZLINK_CONFIG_INVALID_HANDLE;
                return -1;
            }
            if (validate_socket_callback_poller_events (handle,
                                                        items_[i].events)
                != 0) {
                if (error_out_)
                    *error_out_ = ZLINK_CONFIG_INVALID_ARGUMENT;
                return -1;
            }
            if (poller.add (handle.socket, NULL, items_[i].events) != 0) {
                if (error_out_)
                    *error_out_ = zlink::config_result_internal::from_errno (errno);
                return -1;
            }
        } else if (poller.add_fd (items_[i].fd, NULL, items_[i].events) != 0) {
            if (error_out_)
                *error_out_ = zlink::config_result_internal::from_errno (errno);
            return -1;
        }
    }

    std::vector<zlink::socket_poller_t::event_t> events (
      static_cast<size_t> (nitems_));
    const int rc = poller.wait (events.data (), nitems_, timeout_);
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
        for (int j = 0; j < nitems_; ++j) {
            if ((items_[j].socket && items_[j].socket == events[i].socket)
                || (!items_[j].socket && items_[j].fd == events[i].fd)) {
                items_[j].revents = events[i].events;
                break;
            }
        }
    }
    if (error_out_)
        *error_out_ = ZLINK_CONFIG_OK;
    return rc;
}

void *zlink_poller_new (void)
{
    poller_handle_t *poller = new (std::nothrow) poller_handle_t;
    if (!poller) {
        errno = ENOMEM;
        return NULL;
    }
    return static_cast<void *> (poller);
}

zlink_close_result_t zlink_poller_destroy (void **poller_p_)
{
    if (!poller_p_ || !*poller_p_) {
        errno = EFAULT;
        return ZLINK_CLOSE_INVALID_HANDLE;
    }
    poller_handle_t *poller = as_poller_handle (*poller_p_);
    if (!poller)
        return ZLINK_CLOSE_INVALID_HANDLE;
    for (size_t i = 0; i < poller->registrations.size (); ++i)
        release_poller_registration (poller->registrations[i]);
    poller->tag = 0xdeadbeef;
    delete poller;
    *poller_p_ = NULL;
    return ZLINK_CLOSE_OK;
}

int zlink_poller_size (void *poller_, zlink_config_result_t *error_out_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller) {
        if (error_out_)
            *error_out_ = ZLINK_CONFIG_INVALID_HANDLE;
        return -1;
    }
    if (error_out_)
        *error_out_ = ZLINK_CONFIG_OK;
    return poller->poller.size ();
}

zlink_config_result_t zlink_poller_add_fd (void *poller_,
                                           zlink_fd_t fd_,
                                           void *user_data_,
                                           short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return ZLINK_CONFIG_INVALID_HANDLE;
    return zlink::config_result_internal::from_rc (
      poller_add_fd_registration (poller, fd_, user_data_, events_, NULL,
                                  poller_subject_fd));
}

zlink_config_result_t zlink_poller_modify_fd (void *poller_,
                                              zlink_fd_t fd_,
                                              short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return ZLINK_CONFIG_INVALID_HANDLE;
    const int index =
      poller_find_fd_registration_index (poller, fd_, poller_subject_fd);
    if (index < 0) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    if (poller->poller.modify_fd (fd_, events_) != 0)
        return zlink::config_result_internal::from_errno (errno);
    poller->registrations[static_cast<size_t> (index)].events = events_;
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_poller_remove_fd (void *poller_, zlink_fd_t fd_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return ZLINK_CONFIG_INVALID_HANDLE;
    const int index =
      poller_find_fd_registration_index (poller, fd_, poller_subject_fd);
    if (index < 0) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    return zlink::config_result_internal::from_rc (
      poller_remove_registration_at (poller, index));
}

zlink_config_result_t zlink_poller_add_timer (void *poller_,
                                              void *timer_,
                                              void *user_data_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    timer_handle_t *timer = as_timer_handle (timer_);
    if (!poller || !timer)
        return ZLINK_CONFIG_INVALID_HANDLE;

    if (timer_handle_acquire_poller_ref (timer) != 0)
        return zlink::config_result_internal::from_errno (errno);

    zlink_fd_t fd = 0;
    if (timer_handle_signaler_fd (timer, &fd) != 0
        || poller_add_fd_registration (poller, fd, user_data_, ZLINK_POLLIN,
                                       timer_, poller_subject_timer)
             != 0) {
        const int err = errno;
        timer_handle_release_poller_ref (timer);
        errno = err;
        return zlink::config_result_internal::from_errno (err);
    }
    return ZLINK_CONFIG_OK;
}

zlink_config_result_t zlink_poller_remove_timer (void *poller_, void *timer_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    timer_handle_t *timer = as_timer_handle (timer_);
    if (!poller || !timer)
        return ZLINK_CONFIG_INVALID_HANDLE;

    const int index =
      poller_find_registration_index (poller, timer_, poller_subject_timer);
    if (index < 0) {
        errno = ENOENT;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    return zlink::config_result_internal::from_rc (
      poller_remove_registration_at (poller, index));
}

int zlink_poller_wait (void *poller_,
                       zlink_poller_event_t *event_,
                       long timeout_,
                       zlink_config_result_t *error_out_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller || !event_) {
        if (poller && !event_)
            errno = EINVAL;
        if (error_out_)
            *error_out_ = !poller ? ZLINK_CONFIG_INVALID_HANDLE
                                  : ZLINK_CONFIG_INVALID_ARGUMENT;
        return -1;
    }
    zlink::socket_poller_t::event_t native_event;
    const int rc = poller->poller.wait (&native_event, 1, timeout_);
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
    if (fill_public_poller_event (poller, native_event, event_) != 0) {
        if (error_out_)
            *error_out_ = zlink::config_result_internal::from_errno (errno);
        return -1;
    }
    if (error_out_)
        *error_out_ = ZLINK_CONFIG_OK;
    return rc;
}

int zlink_poller_wait_all (void *poller_,
                           zlink_poller_event_t *events_,
                           int n_events_,
                           long timeout_,
                           zlink_config_result_t *error_out_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller) {
        if (error_out_)
            *error_out_ = ZLINK_CONFIG_INVALID_HANDLE;
        return -1;
    }
    if (n_events_ < 0 || (n_events_ > 0 && !events_)) {
        errno = EINVAL;
        if (error_out_)
            *error_out_ = ZLINK_CONFIG_INVALID_ARGUMENT;
        return -1;
    }
    std::vector<zlink::socket_poller_t::event_t> native_events (
      static_cast<size_t> (n_events_));
    const int rc =
      poller->poller.wait (native_events.data (), n_events_, timeout_);
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
        if (fill_public_poller_event (poller, native_events[i], &events_[i])
            != 0) {
            if (error_out_)
                *error_out_ = zlink::config_result_internal::from_errno (errno);
            return -1;
        }
    }
    if (error_out_)
        *error_out_ = ZLINK_CONFIG_OK;
    return rc;
}
