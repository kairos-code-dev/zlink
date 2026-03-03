/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_POLLER_HPP_INCLUDED
#define ZLINK_CPP_POLLER_HPP_INCLUDED

#include "socket.hpp"

namespace zlink
{

struct poll_event_t
{
    socket_t *socket;
    void *user;
    short events;
    short revents;
};

class poller_t
{
  public:
    int add (socket_t &socket_, poll_event events_, void *user_ = NULL)
    {
        item_t item;
        item.socket = &socket_;
        item.fd = 0;
        item.events = static_cast<short> (events_);
        item.user = user_;
        _items.push_back (item);
        return 0;
    }

    int add (zlink_fd_t fd_, poll_event events_, void *user_ = NULL)
    {
        item_t item;
        item.socket = NULL;
        item.fd = fd_;
        item.events = static_cast<short> (events_);
        item.user = user_;
        _items.push_back (item);
        return 0;
    }

    int remove (socket_t &socket_)
    {
        for (std::vector<item_t>::iterator it = _items.begin ();
             it != _items.end ();
             ++it) {
            if (it->socket == &socket_) {
                _items.erase (it);
                return 0;
            }
        }
        return -1;
    }

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

    int wait (std::vector<poll_event_t> &events_, long timeout_ms_)
    {
        if (_pollitems.size () < _items.size ())
            _pollitems.resize (_items.size ());

        for (size_t i = 0; i < _items.size (); ++i) {
            _pollitems[i].socket = _items[i].socket
                                    ? _items[i].socket->handle ()
                                    : NULL;
            _pollitems[i].fd = _items[i].fd;
            _pollitems[i].events = _items[i].events;
            _pollitems[i].revents = 0;
        }

        const int rc = zlink_poll (
          _pollitems.data (), static_cast<int> (_items.size ()), timeout_ms_);
        if (rc <= 0)
            return rc;

        events_.clear ();
        for (size_t i = 0; i < _items.size (); ++i) {
            if (_pollitems[i].revents == 0)
                continue;

            poll_event_t ev;
            ev.socket = _items[i].socket;
            ev.user = _items[i].user;
            ev.events = _pollitems[i].events;
            ev.revents = _pollitems[i].revents;
            events_.push_back (ev);
        }
        return static_cast<int> (events_.size ());
    }

    int wait (std::vector<poll_event_t> &events_,
              std::chrono::milliseconds timeout_)
    {
        return wait (events_, static_cast<long> (timeout_.count ()));
    }

  private:
    struct item_t
    {
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
