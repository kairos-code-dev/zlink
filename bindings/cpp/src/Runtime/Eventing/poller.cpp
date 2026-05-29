/* SPDX-License-Identifier: MPL-2.0 */
#include "zlink/Contracts/Eventing/poller.hpp"

#include <Runtime/Eventing/monitor_access.hpp>
#include <Runtime/Eventing/timer_access.hpp>
#include <Runtime/Service/spot_access.hpp>
#include <Runtime/Sockets/socket_access.hpp>

#include <zlink.h>

#include <algorithm>
#include <cerrno>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zlink
{

namespace
{

struct poller_item_t
{
    void *socket_handle = NULL;
    int fd = 0;
    void *timer_handle = NULL;
    zlink_timer_t *timer = NULL;
    poll_source_kind_t source_kind = poll_source_kind_t::socket;
    poll_event_flag_t events = poll_event_flag_t::none;
    std::uintptr_t slot = 0;
    bool native_poller_only = false;
};

bool is_socket_poll_item_active (const poller_item_t &item_,
                                 short events_) noexcept
{
    return events_ != 0 && !item_.native_poller_only
           && item_.source_kind == poll_source_kind_t::socket;
}

poll_source_kind_t
to_source_kind (zlink_poller_source_kind_t source_kind_) noexcept
{
    switch (source_kind_) {
        case 2:
            return poll_source_kind_t::fd;
        case 3:
            return poll_source_kind_t::timer;
        case 1:
        default:
            return poll_source_kind_t::socket;
    }
}

} // namespace

struct poller_t::impl
{
    void *poller = NULL;
    std::vector<std::unique_ptr<poller_item_t> > items;
    std::vector<zlink_poller_event_t> native_events;
    std::vector<zlink_pollitem_t> poll_items;
    std::vector<size_t> poll_item_indexes;
    std::vector<size_t> poll_item_positions;
    std::unordered_map<const void *, size_t> socket_item_indexes;
    bool socket_native_events_dirty = false;
    bool socket_poll_items_dirty = true;
    size_t non_socket_item_count = 0;
    static constexpr size_t npos = static_cast<size_t> (-1);

    impl () : poller (zlink_poller_new ()) {}

    ~impl () { destroy_noexcept (); }

    bool is_socket_only () const noexcept { return non_socket_item_count == 0; }

    void ensure_addable () const
    {
        if (!poller)
            throw config_error_t (config_result_t::invalid_handle,
                                  detail::current_errno ());
    }

    void delete_items () noexcept
    {
        items.clear ();
        socket_item_indexes.clear ();
    }

    void clear_vectors ()
    {
        delete_items ();
        native_events.clear ();
        poll_items.clear ();
        poll_item_indexes.clear ();
        poll_item_positions.clear ();
        socket_native_events_dirty = false;
        socket_poll_items_dirty = true;
        non_socket_item_count = 0;
    }

    void destroy_noexcept () noexcept
    {
        clear_vectors ();
        if (!poller)
            return;
        void *handle = poller;
        (void) zlink_poller_destroy (&handle);
        poller = NULL;
    }

    void close ()
    {
        if (!poller) {
            clear_vectors ();
            return;
        }

        void *handle = poller;
        const close_result_t result =
          static_cast<close_result_t> (zlink_poller_destroy (&handle));
        detail::throw_if_failed<close_error_t> (result);
        poller = NULL;
        clear_vectors ();
    }

    int find_socket (const void *socket_handle_) const noexcept
    {
        const auto cached = socket_item_indexes.find (socket_handle_);
        if (cached != socket_item_indexes.end ()) {
            const size_t index = cached->second;
            if (index < items.size ()
                && items[index]->source_kind == poll_source_kind_t::socket
                && items[index]->socket_handle == socket_handle_) {
                return static_cast<int> (index);
            }
        }

        for (size_t i = 0; i < items.size (); ++i) {
            if (items[i]->source_kind == poll_source_kind_t::socket
                && items[i]->socket_handle == socket_handle_) {
                return static_cast<int> (i);
            }
        }
        return -1;
    }

    int find_fd (int fd_) const noexcept
    {
        for (size_t i = 0; i < items.size (); ++i) {
            if (items[i]->source_kind == poll_source_kind_t::fd
                && items[i]->fd == fd_)
                return static_cast<int> (i);
        }
        return -1;
    }

    int find_timer (const void *timer_handle_) const noexcept
    {
        for (size_t i = 0; i < items.size (); ++i) {
            if (items[i]->source_kind == poll_source_kind_t::timer
                && items[i]->timer_handle == timer_handle_)
                return static_cast<int> (i);
        }
        return -1;
    }

    void rebuild_socket_item_indexes ()
    {
        socket_item_indexes.clear ();
        socket_item_indexes.reserve (items.size ());
        for (size_t i = 0; i < items.size (); ++i) {
            const poller_item_t &item = *items[i];
            if (item.source_kind == poll_source_kind_t::socket)
                socket_item_indexes[item.socket_handle] = i;
        }
    }

    void commit_added_item (std::unique_ptr<poller_item_t> item_,
                            config_result_t rc_)
    {
        detail::throw_if_failed<config_error_t> (rc_);
        const bool socket_item =
          item_->source_kind == poll_source_kind_t::socket;
        const void *socket_handle = item_->socket_handle;
        const size_t index = items.size ();
        items.push_back (std::move (item_));
        if (socket_item)
            socket_item_indexes[socket_handle] = index;
        socket_poll_items_dirty = true;
    }

    void add_socket (void *socket_handle_,
                     poll_event_flag_t events_,
                     std::uintptr_t slot_,
                     bool native_poller_only_)
    {
        ensure_addable ();
        if (native_poller_only_)
            sync_socket_native_events_if_needed ();
        if (find_socket (socket_handle_) >= 0)
            throw config_error_t (config_result_t::invalid_argument,
                                  detail::current_errno ());

        std::unique_ptr<poller_item_t> item (new poller_item_t ());
        item->socket_handle = socket_handle_;
        item->source_kind = poll_source_kind_t::socket;
        item->events = events_;
        item->slot = slot_;
        item->native_poller_only = native_poller_only_;

        poller_item_t *raw_item = item.get ();
        const config_result_t rc =
          static_cast<config_result_t> (zlink_poller_add (
            poller, socket_handle_, raw_item, static_cast<short> (events_)));
        commit_added_item (std::move (item), rc);
        if (native_poller_only_)
            ++non_socket_item_count;
    }

    void add_fd (int fd_, poll_event_flag_t events_, std::uintptr_t slot_)
    {
        ensure_addable ();
        sync_socket_native_events_if_needed ();
        if (find_fd (fd_) >= 0)
            throw config_error_t (config_result_t::invalid_argument,
                                  detail::current_errno ());

        std::unique_ptr<poller_item_t> item (new poller_item_t ());
        item->fd = fd_;
        item->source_kind = poll_source_kind_t::fd;
        item->events = events_;
        item->slot = slot_;

        poller_item_t *raw_item = item.get ();
        const config_result_t rc =
          static_cast<config_result_t> (zlink_poller_add_fd (
            poller, fd_, raw_item, static_cast<short> (events_)));
        commit_added_item (std::move (item), rc);
        ++non_socket_item_count;
    }

    void add_timer (zlink_timer_t &timer_, std::uintptr_t slot_)
    {
        ensure_addable ();
        sync_socket_native_events_if_needed ();
        void *timer_handle = detail::native_handle (timer_);
        if (find_timer (timer_handle) >= 0)
            throw config_error_t (config_result_t::invalid_argument,
                                  detail::current_errno ());

        std::unique_ptr<poller_item_t> item (new poller_item_t ());
        item->timer_handle = timer_handle;
        item->timer = &timer_;
        item->source_kind = poll_source_kind_t::timer;
        item->events = poll_event_flag_t::pollin;
        item->slot = slot_;

        poller_item_t *raw_item = item.get ();
        const config_result_t rc = static_cast<config_result_t> (
          zlink_poller_add_timer (poller, timer_handle, raw_item));
        commit_added_item (std::move (item), rc);
        ++non_socket_item_count;
    }

    void modify_socket (void *socket_handle_, poll_event_flag_t events_)
    {
        if (!poller)
            throw config_error_t (config_result_t::invalid_handle,
                                  detail::current_errno ());
        const int index = find_socket (socket_handle_);
        if (index < 0)
            throw config_error_t (config_result_t::invalid_argument,
                                  detail::current_errno ());

        if (is_socket_only ()) {
            socket_native_events_dirty = true;
            update_socket_poll_item_if_clean (static_cast<size_t> (index),
                                              events_);
            return;
        }

        const config_result_t rc =
          static_cast<config_result_t> (zlink_poller_modify (
            poller, socket_handle_, static_cast<short> (events_)));
        detail::throw_if_failed<config_error_t> (rc);
        items[static_cast<size_t> (index)]->events = events_;
    }

    void modify_fd (int fd_, poll_event_flag_t events_)
    {
        if (!poller)
            throw config_error_t (config_result_t::invalid_handle,
                                  detail::current_errno ());
        const int index = find_fd (fd_);
        if (index < 0)
            throw config_error_t (config_result_t::invalid_argument,
                                  detail::current_errno ());

        const config_result_t rc = static_cast<config_result_t> (
          zlink_poller_modify_fd (poller, fd_, static_cast<short> (events_)));
        detail::throw_if_failed<config_error_t> (rc);
        items[static_cast<size_t> (index)]->events = events_;
    }

    bool remove_socket (void *socket_handle_)
    {
        if (!poller)
            throw config_error_t (config_result_t::invalid_handle,
                                  detail::current_errno ());
        const int index = find_socket (socket_handle_);
        if (index < 0)
            throw config_error_t (config_result_t::invalid_argument,
                                  detail::current_errno ());

        const config_result_t rc = static_cast<config_result_t> (
          zlink_poller_remove (poller, socket_handle_));
        detail::throw_if_failed<config_error_t> (rc);

        const bool native_only =
          items[static_cast<size_t> (index)]->native_poller_only;
        items.erase (items.begin () + index);
        rebuild_socket_item_indexes ();
        if (native_only)
            --non_socket_item_count;
        socket_poll_items_dirty = true;
        return true;
    }

    bool remove_timer (zlink_timer_t &timer_)
    {
        if (!poller)
            throw config_error_t (config_result_t::invalid_handle,
                                  detail::current_errno ());
        const int index = find_timer (detail::native_handle (timer_));
        if (index < 0)
            throw config_error_t (config_result_t::invalid_argument,
                                  detail::current_errno ());

        const config_result_t rc = static_cast<config_result_t> (
          zlink_poller_remove_timer (poller, detail::native_handle (timer_)));
        detail::throw_if_failed<config_error_t> (rc);

        items.erase (items.begin () + index);
        rebuild_socket_item_indexes ();
        --non_socket_item_count;
        socket_poll_items_dirty = true;
        return true;
    }

    bool remove_fd (int fd_)
    {
        if (!poller)
            throw config_error_t (config_result_t::invalid_handle,
                                  detail::current_errno ());
        const int index = find_fd (fd_);
        if (index < 0)
            throw config_error_t (config_result_t::invalid_argument,
                                  detail::current_errno ());

        const config_result_t rc =
          static_cast<config_result_t> (zlink_poller_remove_fd (poller, fd_));
        detail::throw_if_failed<config_error_t> (rc);

        items.erase (items.begin () + index);
        rebuild_socket_item_indexes ();
        --non_socket_item_count;
        socket_poll_items_dirty = true;
        return true;
    }

    void fill_event (poll_event_t &event_,
                     const zlink_poller_event_t &native_event_) const noexcept
    {
        const poller_item_t *item =
          static_cast<const poller_item_t *> (native_event_.user_data);
        event_.source_kind =
          item ? item->source_kind : to_source_kind (native_event_.source_kind);
        event_.slot = item ? item->slot : 0;
        event_.fd =
          event_.source_kind == poll_source_kind_t::fd ? native_event_.fd : 0;
        event_.revents = static_cast<poll_event_flag_t> (native_event_.events);
    }

    void fill_event_from_item (poll_event_t &event_,
                               const poller_item_t &item_,
                               short revents_) const noexcept
    {
        event_.source_kind = item_.source_kind;
        event_.slot = item_.slot;
        event_.fd = item_.source_kind == poll_source_kind_t::fd ? item_.fd : 0;
        event_.revents = static_cast<poll_event_flag_t> (revents_);
    }

    size_t wait (poll_event_t *events_, size_t capacity_, long timeout_)
    {
        if (!poller)
            throw recv_error_t (recv_result_t::invalid_handle,
                                detail::current_errno ());
        if (!events_ || capacity_ == 0)
            throw config_error_t (config_result_t::invalid_argument, EINVAL);
        if (items.empty ())
            return 0;
        if (is_socket_only ())
            return wait_socket_items (events_, capacity_, timeout_);

        sync_socket_native_events_if_needed ();
        const size_t capacity = std::min (capacity_, items.size ());

        native_events.resize (capacity);
        zlink_config_result_t error = static_cast<zlink_config_result_t> (0);
        const int rc =
          zlink_poller_wait (poller, &native_events[0],
                             static_cast<int> (capacity), timeout_, &error);
        if (rc <= 0) {
            if (rc == 0)
                return 0;
            const int err = detail::current_errno ();
            if (err == EINTR || err == EAGAIN) {
                errno = err;
                return 0;
            }
            throw recv_error_t (recv_result_t::invalid_handle, err);
        }

        for (int i = 0; i < rc; ++i)
            fill_event (events_[static_cast<size_t> (i)],
                        native_events[static_cast<size_t> (i)]);
        return static_cast<size_t> (rc);
    }

    void sync_socket_native_events_if_needed ()
    {
        if (!socket_native_events_dirty)
            return;
        for (size_t i = 0; i < items.size (); ++i) {
            const poller_item_t &item = *items[i];
            if (item.source_kind != poll_source_kind_t::socket)
                continue;
            const config_result_t rc =
              static_cast<config_result_t> (zlink_poller_modify (
                poller, item.socket_handle, static_cast<short> (item.events)));
            detail::throw_if_failed<config_error_t> (rc);
        }
        socket_native_events_dirty = false;
    }

    size_t
    wait_socket_items (poll_event_t *events_, size_t capacity_, long timeout_)
    {
        const size_t capacity = std::min (capacity_, items.size ());
        rebuild_socket_poll_items_if_needed ();

        if (poll_items.empty ())
            return 0;

        for (size_t i = 0; i < poll_items.size (); ++i)
            poll_items[i].revents = 0;

        zlink_config_result_t error = static_cast<zlink_config_result_t> (0);
        const int rc =
          zlink_poll (&poll_items[0], static_cast<int> (poll_items.size ()),
                      timeout_, &error);
        if (rc <= 0) {
            if (rc == 0)
                return 0;
            const int err = detail::current_errno ();
            if (err == EINTR || err == EAGAIN) {
                errno = err;
                return 0;
            }
            throw recv_error_t (recv_result_t::invalid_handle, err);
        }

        size_t out = 0;
        for (size_t i = 0; i < poll_items.size () && out < capacity; ++i) {
            const short revents = poll_items[i].revents;
            if (revents == 0)
                continue;
            fill_event_from_item (events_[out], *items[poll_item_indexes[i]],
                                  revents);
            ++out;
        }
        return out;
    }

    void rebuild_socket_poll_items_if_needed ()
    {
        if (!socket_poll_items_dirty)
            return;

        poll_items.clear ();
        poll_item_indexes.clear ();
        poll_item_positions.assign (items.size (), npos);
        poll_items.reserve (items.size ());
        poll_item_indexes.reserve (items.size ());

        for (size_t i = 0; i < items.size (); ++i) {
            const poller_item_t &item = *items[i];
            if (item.source_kind != poll_source_kind_t::socket
                || item.native_poller_only)
                continue;
            const short events = static_cast<short> (item.events);
            if (events == 0)
                continue;

            zlink_pollitem_t poll_item;
            poll_item.socket = item.socket_handle;
            poll_item.fd = 0;
            poll_item.events = events;
            poll_item.revents = 0;
            poll_item_positions[i] = poll_items.size ();
            poll_items.push_back (poll_item);
            poll_item_indexes.push_back (i);
        }

        socket_poll_items_dirty = false;
    }

    void update_socket_poll_item_if_clean (size_t index_,
                                           poll_event_flag_t events_)
    {
        poller_item_t &item = *items[index_];
        if (socket_poll_items_dirty) {
            item.events = events_;
            return;
        }

        if (poll_item_positions.size () != items.size ()) {
            socket_poll_items_dirty = true;
            item.events = events_;
            return;
        }

        const short old_events = static_cast<short> (item.events);
        const short new_events = static_cast<short> (events_);
        const bool old_active = is_socket_poll_item_active (item, old_events);
        const bool new_active = is_socket_poll_item_active (item, new_events);

        item.events = events_;

        const size_t position = poll_item_positions[index_];
        if (old_active && position >= poll_items.size ()) {
            socket_poll_items_dirty = true;
            return;
        }

        if (old_active && new_active) {
            poll_items[position].events = new_events;
            return;
        }

        if (old_active && !new_active) {
            poll_items.erase (poll_items.begin () + position);
            poll_item_indexes.erase (poll_item_indexes.begin () + position);
            poll_item_positions[index_] = npos;
            for (size_t i = position; i < poll_item_indexes.size (); ++i)
                poll_item_positions[poll_item_indexes[i]] = i;
            return;
        }

        if (!old_active && new_active) {
            size_t insert_at = poll_item_indexes.size ();
            for (size_t i = 0; i < poll_item_indexes.size (); ++i) {
                if (poll_item_indexes[i] > index_) {
                    insert_at = i;
                    break;
                }
            }
            zlink_pollitem_t poll_item;
            poll_item.socket = item.socket_handle;
            poll_item.fd = 0;
            poll_item.events = new_events;
            poll_item.revents = 0;
            poll_items.insert (poll_items.begin () + insert_at, poll_item);
            poll_item_indexes.insert (poll_item_indexes.begin () + insert_at,
                                      index_);
            for (size_t i = insert_at; i < poll_item_indexes.size (); ++i)
                poll_item_positions[poll_item_indexes[i]] = i;
        }
    }
};

poller_t::poller_t () : _impl (new impl ())
{
}

poller_t::~poller_t () = default;

poller_t::poller_t (poller_t &&other_) noexcept = default;

poller_t &poller_t::operator= (poller_t &&other_) noexcept = default;

bool poller_t::valid () const noexcept
{
    return _impl && _impl->poller != NULL;
}

int poller_t::size () const
{
    if (!valid ())
        throw config_error_t (config_result_t::invalid_handle, EINVAL);
    return static_cast<int> (_impl->items.size ());
}

void poller_t::add_fd (int fd_, poll_event_flag_t events_, std::uintptr_t slot_)
{
    _impl->add_fd (fd_, events_, slot_);
}

void poller_t::add (zlink_timer_t &timer_, std::uintptr_t slot_)
{
    _impl->add_timer (timer_, slot_);
}

void poller_t::add (service::spot_t &spot_,
                    poll_event_flag_t events_,
                    std::uintptr_t slot_)
{
    _impl->add_socket (zlink::detail::native_handle (spot_), events_, slot_,
                       true);
}

void poller_t::add (socket_monitor_t &monitor_,
                    poll_event_flag_t events_,
                    std::uintptr_t slot_)
{
    _impl->add_socket (zlink::detail::native_handle (monitor_), events_, slot_,
                       true);
}

void poller_t::add (socket_t &socket_,
                    poll_event_flag_t events_,
                    std::uintptr_t slot_)
{
    const bool native_only =
      (static_cast<short> (events_)
       & static_cast<short> (poll_event_flag_t::pollcompletion))
      != 0;
    _impl->add_socket (zlink::detail::native_handle (socket_), events_, slot_,
                       native_only);
}

void poller_t::modify_fd (int fd_, poll_event_flag_t events_)
{
    _impl->modify_fd (fd_, events_);
}

void poller_t::modify (socket_monitor_t &monitor_, poll_event_flag_t events_)
{
    _impl->modify_socket (zlink::detail::native_handle (monitor_), events_);
}

void poller_t::modify (socket_t &socket_, poll_event_flag_t events_)
{
    _impl->modify_socket (zlink::detail::native_handle (socket_), events_);
}

bool poller_t::remove (zlink_timer_t &timer_)
{
    return _impl->remove_timer (timer_);
}

bool poller_t::remove (service::spot_t &spot_)
{
    return _impl->remove_socket (zlink::detail::native_handle (spot_));
}

bool poller_t::remove (socket_monitor_t &monitor_)
{
    return _impl->remove_socket (zlink::detail::native_handle (monitor_));
}

bool poller_t::remove (socket_t &socket_)
{
    return _impl->remove_socket (zlink::detail::native_handle (socket_));
}

bool poller_t::remove_fd (int fd_)
{
    return _impl->remove_fd (fd_);
}

size_t poller_t::wait (poll_event_t *events_,
                       size_t capacity_,
                       std::chrono::milliseconds timeout_)
{
    return _impl->wait (events_, capacity_,
                        static_cast<long> (timeout_.count ()));
}

void poller_t::close ()
{
    if (!_impl)
        return;
    _impl->close ();
}

} // namespace zlink
