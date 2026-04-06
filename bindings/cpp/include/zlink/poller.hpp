/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_POLLER_HPP_INCLUDED
#define ZLINK_CPP_POLLER_HPP_INCLUDED

#include <cerrno>
#include <vector>

namespace zlink
{

struct poll_event_t
{
    void *socket_handle;
    void *socket;
    zlink_fd_t fd;
    void *user;
    short events;
    short revents;
};

class poller_t
{
  public:
    poller_t () : _poller (zlink_poller_new ()) {}

    ~poller_t () { destroy (); }

    poller_t (poller_t &&other) noexcept
        : _poller (other._poller),
          _items (std::move (other._items)),
          _native_events (std::move (other._native_events))
    {
        other._poller = NULL;
    }

    poller_t &operator= (poller_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        destroy ();
        _poller = other._poller;
        _items = std::move (other._items);
        _native_events = std::move (other._native_events);
        other._poller = NULL;
        return *this;
    }

    poller_t (const poller_t &) = delete;
    poller_t &operator= (const poller_t &) = delete;

    bool valid () const noexcept { return _poller != NULL; }
    void *handle () noexcept { return _poller; }
    const void *handle () const noexcept { return _poller; }

    int size () const
    {
        return _poller ? zlink_poller_size (_poller) : -1;
    }

    template<typename SocketLike>
    int add (SocketLike &socket_, poll_event events_, void *user_ = NULL)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        if (find_socket (socket_.handle ()) >= 0) {
            errno = EINVAL;
            return -1;
        }

        item_t *item = new item_t ();
        item->socket_handle = socket_.handle ();
        item->socket = &socket_;
        item->fd = 0;
        item->events = static_cast<short> (events_);
        item->user = user_;
        item->is_socket = true;

        const int rc = zlink_poller_add (
          _poller, socket_.handle (), item, static_cast<short> (events_));
        if (rc != 0)
            delete item;
        if (rc != 0)
            return rc;

        _items.push_back (item);
        return 0;
    }

    int add (zlink_fd_t fd_, poll_event events_, void *user_ = NULL)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        if (find_fd (fd_) >= 0) {
            errno = EINVAL;
            return -1;
        }

        item_t *item = new item_t ();
        item->socket_handle = NULL;
        item->socket = NULL;
        item->fd = fd_;
        item->events = static_cast<short> (events_);
        item->user = user_;
        item->is_socket = false;

        const int rc =
          zlink_poller_add_fd (_poller, fd_, item, static_cast<short> (events_));
        if (rc != 0)
            delete item;
        if (rc != 0)
            return rc;

        _items.push_back (item);
        return 0;
    }

    template<typename SocketLike>
    int modify (SocketLike &socket_, poll_event events_)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        const int index = find_socket (socket_.handle ());
        if (index < 0) {
            errno = EINVAL;
            return -1;
        }

        const int rc = zlink_poller_modify (
          _poller, socket_.handle (), static_cast<short> (events_));
        if (rc != 0)
            return rc;

        _items[static_cast<size_t> (index)]->events = static_cast<short> (events_);
        return 0;
    }

    int modify (zlink_fd_t fd_, poll_event events_)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        const int index = find_fd (fd_);
        if (index < 0) {
            errno = EINVAL;
            return -1;
        }

        const int rc =
          zlink_poller_modify_fd (_poller, fd_, static_cast<short> (events_));
        if (rc != 0)
            return rc;

        _items[static_cast<size_t> (index)]->events = static_cast<short> (events_);
        return 0;
    }

    template<typename SocketLike>
    int remove (SocketLike &socket_)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        const int index = find_socket (socket_.handle ());
        if (index < 0) {
            errno = EINVAL;
            return -1;
        }

        const int rc = zlink_poller_remove (_poller, socket_.handle ());
        if (rc != 0)
            return rc;

        delete _items[static_cast<size_t> (index)];
        _items.erase (_items.begin () + index);
        return 0;
    }

    int remove (zlink_fd_t fd_)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        const int index = find_fd (fd_);
        if (index < 0) {
            errno = EINVAL;
            return -1;
        }

        const int rc = zlink_poller_remove_fd (_poller, fd_);
        if (rc != 0)
            return rc;

        delete _items[static_cast<size_t> (index)];
        _items.erase (_items.begin () + index);
        return 0;
    }

    int wait (poll_event_t *event_, long timeout_ = -1)
    {
        if (!_poller || !event_) {
            errno = EINVAL;
            return -1;
        }

        zlink_poller_event_t native_event;
        const int rc = zlink_poller_wait (_poller, &native_event, timeout_);
        if (rc <= 0)
            return rc;

        fill_event (native_event, event_);
        return rc;
    }

    int wait_all (std::vector<poll_event_t> &events_, long timeout_ = -1)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }

        const int registered = zlink_poller_size (_poller);
        if (registered < 0)
            return -1;

        if (registered == 0) {
            events_.clear ();
            return 0;
        }

        _native_events.resize (static_cast<size_t> (registered));
        const int rc = zlink_poller_wait_all (
          _poller, &_native_events[0], registered, timeout_);
        if (rc <= 0) {
            if (rc == 0)
                events_.clear ();
            return rc;
        }

        events_.resize (static_cast<size_t> (rc));
        for (int i = 0; i < rc; ++i)
            fill_event (_native_events[static_cast<size_t> (i)],
                        &events_[static_cast<size_t> (i)]);
        return rc;
    }

    int destroy () noexcept
    {
        delete_items ();
        _native_events.clear ();

        if (!_poller)
            return 0;

        void *poller = _poller;
        const int rc = zlink_poller_destroy (&poller);
        if (rc == 0)
            _poller = NULL;
        return rc;
    }

  private:
    struct item_t
    {
        void *socket_handle;
        void *socket;
        zlink_fd_t fd;
        short events;
        void *user;
        bool is_socket;
    };

    int find_socket (const void *socket_handle_) const noexcept
    {
        for (size_t i = 0; i < _items.size (); ++i) {
            if (_items[i]->is_socket
                && _items[i]->socket_handle == socket_handle_) {
                return static_cast<int> (i);
            }
        }
        return -1;
    }

    int find_fd (zlink_fd_t fd_) const noexcept
    {
        for (size_t i = 0; i < _items.size (); ++i) {
            if (!_items[i]->is_socket && _items[i]->fd == fd_)
                return static_cast<int> (i);
        }
        return -1;
    }

    void fill_event (const zlink_poller_event_t &native_event_,
                     poll_event_t *event_) const noexcept
    {
        const item_t *item =
          static_cast<const item_t *> (native_event_.user_data);
        event_->socket_handle = native_event_.socket;
        event_->socket = item ? item->socket : NULL;
        event_->fd = native_event_.fd;
        event_->user = item ? item->user : NULL;
        event_->events = native_event_.events;
        event_->revents = native_event_.events;
    }

    void delete_items () noexcept
    {
        for (size_t i = 0; i < _items.size (); ++i)
            delete _items[i];
        _items.clear ();
    }

    void *_poller;
    std::vector<item_t *> _items;
    std::vector<zlink_poller_event_t> _native_events;
};

} // namespace zlink

#endif
