/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_POLLER_HPP_INCLUDED
#define ZLINK_CPP_POLLER_HPP_INCLUDED

#include "socket.hpp"

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
    /**
     * @brief Register a socket for polling.
     * @param socket_ Socket to monitor.
     * @param events_ Event mask.
     * @param user_ Opaque user pointer returned in results.
     * @return 0 on success.
     */
    int add (socket_t &socket_, poll_event events_, void *user_ = NULL)
    {
        item_t item;
        item.socket = &socket_;
        item.socket_handle = socket_.handle ();
        item.fd = 0;
        item.events = static_cast<short> (events_);
        item.user = user_;
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
        item_t item;
        item.socket = NULL;
        item.socket_handle = NULL;
        item.fd = fd_;
        item.events = static_cast<short> (events_);
        item.user = user_;
        _items.push_back (item);
        return 0;
    }

    /**
     * @brief Remove a previously registered socket.
     * @param socket_ Socket to remove.
     * @return 0 on success, -1 if not found.
     */
    int remove (socket_t &socket_)
    {
        for (std::vector<item_t>::iterator it = _items.begin ();
             it != _items.end ();
             ++it) {
            if (it->socket != NULL && it->socket_handle == socket_.handle ()) {
                _items.erase (it);
                return 0;
            }
        }
        return -1;
    }

    /**
     * @brief Remove a previously registered file descriptor.
     * @param fd_ File descriptor to remove.
     * @return 0 on success, -1 if not found.
     */
    int remove (zlink_fd_t fd_)
    {
        for (std::vector<item_t>::iterator it = _items.begin ();
             it != _items.end ();
             ++it) {
            if (it->socket == NULL && it->fd == fd_) {
                _items.erase (it);
                return 0;
            }
        }
        return -1;
    }

    /**
     * @brief Poll for registered events.
     * @param events_ Output vector of ready entries.
     * @param timeout_ms_ Timeout in milliseconds.
     * @return Positive ready count, 0 on timeout, -1 on error.
     */
    int wait (std::vector<poll_event_t> &events_, long timeout_ms_)
    {
        if (_pollitems.size () < _items.size ())
            _pollitems.resize (_items.size ());

        for (size_t i = 0; i < _items.size (); ++i) {
            _pollitems[i].socket = _items[i].socket_handle;
            _pollitems[i].fd = _items[i].fd;
            _pollitems[i].events = _items[i].events;
            _pollitems[i].revents = 0;
        }

        const int rc = zlink_poll (
          _pollitems.data (), static_cast<int> (_items.size ()), timeout_ms_);
        if (rc <= 0)
            return rc;

        events_.clear ();
        events_.reserve (_items.size ());
        for (size_t i = 0; i < _items.size (); ++i) {
            if (_pollitems[i].revents == 0)
                continue;

            poll_event_t ev;
            ev.socket_handle = _items[i].socket_handle;
            ev.socket = _items[i].socket;
            ev.user = _items[i].user;
            ev.events = _pollitems[i].events;
            ev.revents = _pollitems[i].revents;
            events_.push_back (ev);
        }
        return static_cast<int> (events_.size ());
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
    };

    std::vector<item_t> _items;
    std::vector<zlink_pollitem_t> _pollitems;
};

} // namespace zlink

#endif
