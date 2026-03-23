/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <new>
#include <vector>

#include "api/poller_api_internal.hpp"

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
    registration.subject = subject_;
    registration.subject_kind = subject_kind_;
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

int poller_remove_registration_at (poller_handle_t *poller_, int index_)
{
    if (!poller_ || index_ < 0
        || static_cast<size_t> (index_) >= poller_->registrations.size ()) {
        errno = EINVAL;
        return -1;
    }

    zlink::socket_base_t *socket = static_cast<zlink::socket_base_t *> (
      poller_->registrations[static_cast<size_t> (index_)].socket);
    const int rc = poller_->poller.remove (socket);
    if (rc == 0) {
        release_poller_registration (
          poller_->registrations[static_cast<size_t> (index_)]);
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

int zlink_poll (zlink_pollitem_t *items_, int nitems_, long timeout_)
{
    if (nitems_ < 0 || (nitems_ > 0 && !items_)) {
        errno = EINVAL;
        return -1;
    }
    if (nitems_ == 0)
        return 0;

    zlink::socket_poller_t poller;
    for (int i = 0; i < nitems_; ++i) {
        items_[i].revents = 0;
        if (items_[i].socket) {
            socket_handle_t handle = as_socket_handle (items_[i].socket);
            if (!handle.socket)
                return -1;
            if (validate_socket_callback_poller_events (handle,
                                                        items_[i].events)
                != 0)
                return -1;
            if (poller.add (handle.socket, NULL, items_[i].events) != 0)
                return -1;
        } else if (poller.add_fd (items_[i].fd, NULL, items_[i].events) != 0) {
            return -1;
        }
    }

    std::vector<zlink::socket_poller_t::event_t> events (
      static_cast<size_t> (nitems_));
    const int rc = poller.wait (events.data (), nitems_, timeout_);
    if (rc <= 0)
        return rc;

    for (int i = 0; i < rc; ++i) {
        for (int j = 0; j < nitems_; ++j) {
            if ((items_[j].socket && items_[j].socket == events[i].socket)
                || (!items_[j].socket && items_[j].fd == events[i].fd)) {
                items_[j].revents = events[i].events;
                break;
            }
        }
    }
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

int zlink_poller_destroy (void **poller_p_)
{
    if (!poller_p_ || !*poller_p_) {
        errno = EFAULT;
        return -1;
    }
    poller_handle_t *poller = as_poller_handle (*poller_p_);
    if (!poller)
        return -1;
    for (size_t i = 0; i < poller->registrations.size (); ++i)
        release_poller_registration (poller->registrations[i]);
    poller->tag = 0xdeadbeef;
    delete poller;
    *poller_p_ = NULL;
    return 0;
}

int zlink_poller_size (void *poller_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    return poller->poller.size ();
}

int zlink_poller_add_fd (void *poller_,
                         zlink_fd_t fd_,
                         void *user_data_,
                         short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    return poller->poller.add_fd (fd_, user_data_, events_);
}

int zlink_poller_modify_fd (void *poller_, zlink_fd_t fd_, short events_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    return poller->poller.modify_fd (fd_, events_);
}

int zlink_poller_remove_fd (void *poller_, zlink_fd_t fd_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    return poller->poller.remove_fd (fd_);
}

int zlink_poller_wait (void *poller_,
                       zlink_poller_event_t *event_,
                       long timeout_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller || !event_) {
        if (poller && !event_)
            errno = EINVAL;
        return -1;
    }
    zlink::socket_poller_t::event_t native_event;
    const int rc = poller->poller.wait (&native_event, 1, timeout_);
    if (rc <= 0)
        return rc;
    event_->socket = native_event.socket;
    event_->fd = native_event.fd;
    event_->user_data = native_event.user_data;
    event_->events = native_event.events;
    return rc;
}

int zlink_poller_wait_all (void *poller_,
                           zlink_poller_event_t *events_,
                           int n_events_,
                           long timeout_)
{
    poller_handle_t *poller = as_poller_handle (poller_);
    if (!poller)
        return -1;
    if (n_events_ < 0 || (n_events_ > 0 && !events_)) {
        errno = EINVAL;
        return -1;
    }
    return poller->poller.wait (
      reinterpret_cast<zlink::socket_poller_t::event_t *> (events_), n_events_,
      timeout_);
}
