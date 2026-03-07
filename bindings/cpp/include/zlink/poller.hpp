/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_POLLER_HPP_INCLUDED
#define ZLINK_CPP_POLLER_HPP_INCLUDED

#include "socket.hpp"
#include "services/gateway.hpp"
#include "services/spot.hpp"

namespace zlink
{

/**
 * @brief One poll result entry produced by `poller_t::wait`.
 */
struct poll_event_t
{
    /**
     * @brief Native socket handle that became ready, or `NULL` for fd items.
     */
    void *socket_handle;
    /**
     * @brief Optional socket wrapper pointer from registration time.
     */
    socket_t *socket;
    /**
     * @brief User pointer attached during registration.
     */
    void *user;
    /**
     * @brief Requested event mask.
     */
    short events;
    /**
     * @brief Ready event mask.
     */
    short revents;
};

/**
 * @brief Stateful wrapper for zlink poll items.
 */
class poller_t
{
  public:
    poller_t () : _poller (zlink_poller_new ()) {}

    ~poller_t () { release (); }

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

        release ();
        _poller = other._poller;
        _items = std::move (other._items);
        _native_events = std::move (other._native_events);
        other._poller = NULL;
        return *this;
    }

    poller_t (const poller_t &) = delete;
    poller_t &operator= (const poller_t &) = delete;

    /**
     * @brief Register a socket for polling.
     * @param socket_ Socket to monitor.
     * @param events_ Event mask.
     * @param user_ Opaque user pointer returned in results.
     * @return 0 on success.
     */
    int add (socket_t &socket_, poll_event events_, void *user_ = NULL)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        if (find_socket (socket_.handle ()) >= 0) {
            errno = EINVAL;
            return -1;
        }
        const int rc = zlink_poller_add (
          _poller, socket_.handle (), user_, static_cast<short> (events_));
        if (rc != 0)
            return rc;

        item_t item;
        item.socket = &socket_;
        item.socket_handle = socket_.handle ();
        item.fd = 0;
        item.events = static_cast<short> (events_);
        item.user = user_;
        item.is_socket = true;
        _items.push_back (item);
        return 0;
    }

    int add_spot_sub (service::spot_t &spot_,
                      poll_event events_,
                      void *user_ = NULL)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        if (find_socket (spot_.sub_handle ()) >= 0) {
            errno = EINVAL;
            return -1;
        }
        const int rc = zlink_poller_add_spot_sub (
          _poller, spot_.sub_handle (), user_, static_cast<short> (events_));
        if (rc != 0)
            return rc;

        item_t item;
        item.socket = NULL;
        item.socket_handle = spot_.sub_handle ();
        item.fd = 0;
        item.events = static_cast<short> (events_);
        item.user = user_;
        item.is_socket = true;
        _items.push_back (item);
        return 0;
    }

    int add_spot_pub (service::spot_t &spot_,
                      poll_event events_,
                      void *user_ = NULL)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        if (find_socket (spot_.pub_handle ()) >= 0) {
            errno = EINVAL;
            return -1;
        }
        const int rc = zlink_poller_add_spot_pub (
          _poller, spot_.pub_handle (), user_, static_cast<short> (events_));
        if (rc != 0)
            return rc;

        item_t item;
        item.socket = NULL;
        item.socket_handle = spot_.pub_handle ();
        item.fd = 0;
        item.events = static_cast<short> (events_);
        item.user = user_;
        item.is_socket = true;
        _items.push_back (item);
        return 0;
    }

    int add_gateway (service::gateway_t &gateway_,
                     poll_event events_,
                     void *user_ = NULL)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        if (find_socket (gateway_.handle ()) >= 0) {
            errno = EINVAL;
            return -1;
        }
        const int rc = zlink_poller_add_gateway (
          _poller, gateway_.handle (), user_, static_cast<short> (events_));
        if (rc != 0)
            return rc;

        item_t item;
        item.socket = NULL;
        item.socket_handle = gateway_.handle ();
        item.fd = 0;
        item.events = static_cast<short> (events_);
        item.user = user_;
        item.is_socket = true;
        _items.push_back (item);
        return 0;
    }

    int add_receiver (service::receiver_t &receiver_,
                      poll_event events_,
                      void *user_ = NULL)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        if (find_socket (receiver_.handle ()) >= 0) {
            errno = EINVAL;
            return -1;
        }
        const int rc = zlink_poller_add_receiver (
          _poller, receiver_.handle (), user_, static_cast<short> (events_));
        if (rc != 0)
            return rc;

        item_t item;
        item.socket = NULL;
        item.socket_handle = receiver_.handle ();
        item.fd = 0;
        item.events = static_cast<short> (events_);
        item.user = user_;
        item.is_socket = true;
        _items.push_back (item);
        return 0;
    }

    /**
     * @brief Register a raw file descriptor for polling.
     * @param fd_ File descriptor.
     * @param events_ Event mask.
     * @param user_ Opaque user pointer returned in results.
     * @return 0 on success.
     */
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
        const int rc =
          zlink_poller_add_fd (_poller, fd_, user_, static_cast<short> (events_));
        if (rc != 0)
            return rc;

        item_t item;
        item.socket = NULL;
        item.socket_handle = NULL;
        item.fd = fd_;
        item.events = static_cast<short> (events_);
        item.user = user_;
        item.is_socket = false;
        _items.push_back (item);
        return 0;
    }

    /**
     * @brief Update a registered socket's event mask.
     * @param socket_ Registered socket.
     * @param events_ New event mask.
     * @return 0 on success, -1 on failure.
     */
    int modify (socket_t &socket_, poll_event events_)
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
        const int rc =
          zlink_poller_modify (_poller, socket_.handle (), static_cast<short> (events_));
        if (rc != 0)
            return rc;
        _items[static_cast<size_t> (index)].events = static_cast<short> (events_);
        return 0;
    }

    int modify_spot_sub (service::spot_t &spot_, poll_event events_)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        const int index = find_socket (spot_.sub_handle ());
        if (index < 0) {
            errno = EINVAL;
            return -1;
        }
        const int rc = zlink_poller_modify_spot_sub (
          _poller, spot_.sub_handle (), static_cast<short> (events_));
        if (rc != 0)
            return rc;
        _items[static_cast<size_t> (index)].events = static_cast<short> (events_);
        return 0;
    }

    int modify_spot_pub (service::spot_t &spot_, poll_event events_)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        const int index = find_socket (spot_.pub_handle ());
        if (index < 0) {
            errno = EINVAL;
            return -1;
        }
        const int rc = zlink_poller_modify_spot_pub (
          _poller, spot_.pub_handle (), static_cast<short> (events_));
        if (rc != 0)
            return rc;
        _items[static_cast<size_t> (index)].events = static_cast<short> (events_);
        return 0;
    }

    int modify_gateway (service::gateway_t &gateway_, poll_event events_)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        const int index = find_socket (gateway_.handle ());
        if (index < 0) {
            errno = EINVAL;
            return -1;
        }
        const int rc = zlink_poller_modify_gateway (
          _poller, gateway_.handle (), static_cast<short> (events_));
        if (rc != 0)
            return rc;
        _items[static_cast<size_t> (index)].events = static_cast<short> (events_);
        return 0;
    }

    int modify_receiver (service::receiver_t &receiver_, poll_event events_)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        const int index = find_socket (receiver_.handle ());
        if (index < 0) {
            errno = EINVAL;
            return -1;
        }
        const int rc = zlink_poller_modify_receiver (
          _poller, receiver_.handle (), static_cast<short> (events_));
        if (rc != 0)
            return rc;
        _items[static_cast<size_t> (index)].events = static_cast<short> (events_);
        return 0;
    }

    /**
     * @brief Update a registered file descriptor's event mask.
     * @param fd_ Registered file descriptor.
     * @param events_ New event mask.
     * @return 0 on success, -1 on failure.
     */
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
        _items[static_cast<size_t> (index)].events = static_cast<short> (events_);
        return 0;
    }

    /**
     * @brief Remove a previously registered socket.
     * @param socket_ Socket to remove.
     * @return 0 on success, -1 if not found.
     */
    int remove (socket_t &socket_)
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
        _items.erase (_items.begin () + index);
        return 0;
    }

    int remove_spot_sub (service::spot_t &spot_)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        const int index = find_socket (spot_.sub_handle ());
        if (index < 0) {
            errno = EINVAL;
            return -1;
        }
        const int rc = zlink_poller_remove_spot_sub (_poller, spot_.sub_handle ());
        if (rc != 0)
            return rc;
        _items.erase (_items.begin () + index);
        return 0;
    }

    int remove_spot_pub (service::spot_t &spot_)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        const int index = find_socket (spot_.pub_handle ());
        if (index < 0) {
            errno = EINVAL;
            return -1;
        }
        const int rc = zlink_poller_remove_spot_pub (_poller, spot_.pub_handle ());
        if (rc != 0)
            return rc;
        _items.erase (_items.begin () + index);
        return 0;
    }

    int remove_gateway (service::gateway_t &gateway_)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        const int index = find_socket (gateway_.handle ());
        if (index < 0) {
            errno = EINVAL;
            return -1;
        }
        const int rc = zlink_poller_remove_gateway (_poller, gateway_.handle ());
        if (rc != 0)
            return rc;
        _items.erase (_items.begin () + index);
        return 0;
    }

    int remove_receiver (service::receiver_t &receiver_)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        const int index = find_socket (receiver_.handle ());
        if (index < 0) {
            errno = EINVAL;
            return -1;
        }
        const int rc = zlink_poller_remove_receiver (_poller, receiver_.handle ());
        if (rc != 0)
            return rc;
        _items.erase (_items.begin () + index);
        return 0;
    }

    /**
     * @brief Remove a previously registered file descriptor.
     * @param fd_ File descriptor to remove.
     * @return 0 on success, -1 if not found.
     */
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
        _items.erase (_items.begin () + index);
        return 0;
    }

    /**
     * @brief Return the number of registered poll items.
     * @return Item count, or -1 on failure.
     */
    int size () const
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        return zlink_poller_size (_poller);
    }

    /**
     * @brief Poll for registered events.
     * @param events_ Output vector of ready entries.
     * @param timeout_ms_ Timeout in milliseconds.
     * @return Positive ready count, 0 on timeout, -1 on error.
     */
    int wait (std::vector<poll_event_t> &events_, long timeout_ms_)
    {
        if (!_poller) {
            errno = EFAULT;
            return -1;
        }
        if (_items.empty ()) {
            events_.clear ();
            if (timeout_ms_ < 0) {
                errno = EFAULT;
                return -1;
            }
            return 0;
        }
        if (_native_events.size () < _items.size ())
            _native_events.resize (_items.size ());

        const int rc = zlink_poller_wait_all (_poller, _native_events.data (),
                                              static_cast<int> (_items.size ()),
                                              timeout_ms_);
        if (rc <= 0) {
            events_.clear ();
            return rc;
        }

        events_.clear ();
        events_.reserve (static_cast<size_t> (rc));
        for (int i = 0; i < rc; ++i) {
            const zlink_poller_event_t &native = _native_events[static_cast<size_t> (i)];
            poll_event_t ev;
            ev.socket_handle = native.socket;
            ev.socket = NULL;
            ev.user = native.user_data;
            ev.events = native.events;
            ev.revents = native.events;

            const int index = native.socket ? find_socket (native.socket)
                                            : find_fd (native.fd);
            if (index >= 0) {
                const item_t &item = _items[static_cast<size_t> (index)];
                ev.socket = item.socket;
                ev.user = item.user;
                ev.events = item.events;
            }
            events_.push_back (ev);
        }
        return rc;
    }

    /**
     * @brief Poll for registered events.
     * @param events_ Output vector of ready entries.
     * @param timeout_ Timeout duration.
     * @return Positive ready count, 0 on timeout, -1 on error.
     */
    int wait (std::vector<poll_event_t> &events_,
              std::chrono::milliseconds timeout_)
    {
        return wait (events_, static_cast<long> (timeout_.count ()));
    }

  private:
    struct item_t
    {
        void *socket_handle;
        socket_t *socket;
        zlink_fd_t fd;
        short events;
        void *user;
        bool is_socket;
    };

    int find_socket (const void *socket_handle_) const
    {
        for (size_t i = 0; i < _items.size (); ++i) {
            if (_items[i].is_socket
                && _items[i].socket_handle == socket_handle_) {
                return static_cast<int> (i);
            }
        }
        return -1;
    }

    int find_fd (zlink_fd_t fd_) const
    {
        for (size_t i = 0; i < _items.size (); ++i) {
            if (!_items[i].is_socket && _items[i].fd == fd_)
                return static_cast<int> (i);
        }
        return -1;
    }

    void release () noexcept
    {
        if (!_poller)
            return;
        void *poller = _poller;
        (void) zlink_poller_destroy (&poller);
        _poller = NULL;
    }

    void *_poller;
    std::vector<item_t> _items;
    std::vector<zlink_poller_event_t> _native_events;
};

} // namespace zlink

#endif
