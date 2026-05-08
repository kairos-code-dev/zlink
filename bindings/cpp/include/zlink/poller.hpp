/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_POLLER_HPP_INCLUDED
#define ZLINK_CPP_POLLER_HPP_INCLUDED

#include "error.hpp"
#include "timers.hpp"

#include <algorithm>
#include <any>
#include <cstdint>
#include <optional>
#include <vector>

namespace zlink
{

namespace service
{
class spot_t;
} // namespace service
namespace detail
{
inline void *native_handle (service::spot_t &spot_) noexcept;
inline const void *native_handle (const service::spot_t &spot_) noexcept;
} // namespace detail

struct poll_event_t
{
    std::optional<zlink_fd_t> fd;
    timer_t *timer = NULL;
    std::any tag;
    poll_event_flag_t events;
    poll_event_flag_t revents;
};

class poller_t
{
  public:
    poller_t () : _poller (zlink_poller_new ()) {}

    ~poller_t () { destroy_noexcept (); }

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

        destroy_noexcept ();
        _poller = other._poller;
        _items = std::move (other._items);
        _native_events = std::move (other._native_events);
        other._poller = NULL;
        return *this;
    }

    poller_t (const poller_t &) = delete;
    poller_t &operator= (const poller_t &) = delete;

    bool valid () const noexcept { return _poller != NULL; }

    int size () const
    {
        if (!_poller)
            throw config_error_t (config_result_t::invalid_handle, EINVAL);
        return zlink_poller_size (_poller, nullptr);
    }

    template<typename SocketLike>
    void add (SocketLike &socket_,
              poll_event_flag_t events_,
              std::any tag_ = {})
    {
        if (!_poller) {
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
        }
        if (find_socket (detail::native_handle (socket_)) >= 0) {
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
        }

        item_t *item = new item_t ();
        item->socket_handle = detail::native_handle (socket_);
        item->fd = 0;
        item->timer_handle = NULL;
        item->timer = NULL;
        item->source_kind = poll_source_kind_t::socket;
        item->events = events_;
        item->tag = std::move (tag_);

        const config_result_t rc = static_cast<config_result_t> (
          zlink_poller_add (_poller, detail::native_handle (socket_), item,
                            static_cast<short> (events_)));
        if (rc != config_result_t::ok)
            delete item;
        detail::throw_if_failed<config_error_t> (rc);

        _items.push_back (item);
    }

    void add_fd (zlink_fd_t fd_,
                 poll_event_flag_t events_,
                 std::any tag_ = {})
    {
        if (!_poller) {
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
        }
        if (find_fd (fd_) >= 0) {
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
        }

        item_t *item = new item_t ();
        item->socket_handle = NULL;
        item->fd = fd_;
        item->timer_handle = NULL;
        item->timer = NULL;
        item->source_kind = poll_source_kind_t::fd;
        item->events = events_;
        item->tag = std::move (tag_);

        const config_result_t rc = static_cast<config_result_t> (
          zlink_poller_add_fd (_poller, fd_, item, static_cast<short> (events_)));
        if (rc != config_result_t::ok)
            delete item;
        detail::throw_if_failed<config_error_t> (rc);

        _items.push_back (item);
    }

    void add (timer_t &timer_, std::any tag_ = {})
    {
        if (!_poller) {
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
        }
        if (find_timer (detail::native_handle (timer_)) >= 0) {
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
        }

        item_t *item = new item_t ();
        item->socket_handle = NULL;
        item->fd = 0;
        item->timer_handle = detail::native_handle (timer_);
        item->timer = &timer_;
        item->source_kind = poll_source_kind_t::timer;
        item->events = poll_event_flag_t::pollin;
        item->tag = std::move (tag_);

        const config_result_t rc = static_cast<config_result_t> (
          zlink_poller_add_timer (_poller, detail::native_handle (timer_), item));
        if (rc != config_result_t::ok)
            delete item;
        detail::throw_if_failed<config_error_t> (rc);

        _items.push_back (item);
    }

    template<typename SocketLike>
    void modify (SocketLike &socket_, poll_event_flag_t events_)
    {
        if (!_poller) {
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
        }
        const int index = find_socket (detail::native_handle (socket_));
        if (index < 0) {
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
        }

        const config_result_t rc = static_cast<config_result_t> (
          zlink_poller_modify (_poller, detail::native_handle (socket_),
                               static_cast<short> (events_)));
        detail::throw_if_failed<config_error_t> (rc);

        _items[static_cast<size_t> (index)]->events = events_;
    }

    void modify_fd (zlink_fd_t fd_, poll_event_flag_t events_)
    {
        if (!_poller) {
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
        }
        const int index = find_fd (fd_);
        if (index < 0) {
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
        }

        const config_result_t rc = static_cast<config_result_t> (
          zlink_poller_modify_fd (_poller, fd_, static_cast<short> (events_)));
        detail::throw_if_failed<config_error_t> (rc);

        _items[static_cast<size_t> (index)]->events = events_;
    }

    template<typename SocketLike>
    bool remove (SocketLike &socket_)
    {
        if (!_poller) {
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
        }
        const int index = find_socket (detail::native_handle (socket_));
        if (index < 0) {
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
        }

        const config_result_t rc = static_cast<config_result_t> (
          zlink_poller_remove (_poller, detail::native_handle (socket_)));
        detail::throw_if_failed<config_error_t> (rc);

        delete _items[static_cast<size_t> (index)];
        _items.erase (_items.begin () + index);
        return true;
    }

    bool remove (timer_t &timer_)
    {
        if (!_poller) {
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
        }
        const int index = find_timer (detail::native_handle (timer_));
        if (index < 0) {
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
        }

        const config_result_t rc = static_cast<config_result_t> (
          zlink_poller_remove_timer (_poller, detail::native_handle (timer_)));
        detail::throw_if_failed<config_error_t> (rc);

        delete _items[static_cast<size_t> (index)];
        _items.erase (_items.begin () + index);
        return true;
    }

    bool remove_fd (zlink_fd_t fd_)
    {
        if (!_poller) {
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
        }
        const int index = find_fd (fd_);
        if (index < 0) {
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());
        }

        const config_result_t rc = static_cast<config_result_t> (
          zlink_poller_remove_fd (_poller, fd_));
        detail::throw_if_failed<config_error_t> (rc);

        delete _items[static_cast<size_t> (index)];
        _items.erase (_items.begin () + index);
        return true;
    }

    std::optional<poll_event_t> wait (
      std::optional<std::chrono::milliseconds> timeout_ = std::nullopt)
    {
        return wait_impl (timeout_ ? static_cast<long> (timeout_->count ()) : -1L);
    }

  private:
    std::optional<poll_event_t> wait_impl (long timeout_)
    {
        if (!_poller) {
            throw recv_error_t (recv_result_t::invalid_handle, zlink_errno ());
        }

        zlink_poller_event_t native_event;
        zlink_config_result_t error = ZLINK_CONFIG_OK;
        const int rc = zlink_poller_wait (
          _poller, &native_event, timeout_, &error);
        if (rc <= 0) {
            if (rc == 0)
                return std::nullopt;
            const int err = zlink_errno ();
            if (err == EINTR || err == EAGAIN) {
                errno = err;
                return std::nullopt;
            }
            throw recv_error_t (recv_result_t::invalid_handle, err);
        }

        return std::optional<poll_event_t> (make_event (native_event));
    }

  public:
    std::vector<poll_event_t> wait_all (
      size_t max_events_,
      std::optional<std::chrono::milliseconds> timeout_ = std::nullopt)
    {
        return wait_all_impl (
          max_events_, timeout_ ? static_cast<long> (timeout_->count ()) : -1L);
    }

    void destroy ()
    {
        if (!_poller) {
            delete_items ();
            _native_events.clear ();
            return;
        }

        void *poller = _poller;
        const close_result_t result =
          static_cast<close_result_t> (zlink_poller_destroy (&poller));
        detail::throw_if_failed<close_error_t> (result);
        _poller = NULL;
        delete_items ();
        _native_events.clear ();
    }

  private:
    std::vector<poll_event_t> wait_all_impl (size_t max_events_, long timeout_)
    {
        if (!_poller) {
            throw recv_error_t (recv_result_t::invalid_handle, zlink_errno ());
        }

        zlink_config_result_t error = ZLINK_CONFIG_OK;
        const int registered = zlink_poller_size (_poller, &error);
        if (registered < 0)
            throw recv_error_t (recv_result_t::invalid_handle, zlink_errno ());

        if (registered == 0) {
            return std::vector<poll_event_t> ();
        }

        const size_t capacity =
          max_events_ > 0
            ? std::min (max_events_, static_cast<size_t> (registered))
            : static_cast<size_t> (registered);
        _native_events.resize (capacity);
        error = ZLINK_CONFIG_OK;
        const int rc = zlink_poller_wait_all (
          _poller, &_native_events[0], static_cast<int> (capacity), timeout_, &error);
        if (rc <= 0) {
            if (rc == 0) {
                return std::vector<poll_event_t> ();
            }
            const int err = zlink_errno ();
            if (err == EINTR || err == EAGAIN) {
                errno = err;
                return std::vector<poll_event_t> ();
            }
            throw recv_error_t (recv_result_t::invalid_handle, err);
        }

        std::vector<poll_event_t> events_;
        events_.resize (static_cast<size_t> (rc));
        for (int i = 0; i < rc; ++i)
            events_[static_cast<size_t> (i)] =
              make_event (_native_events[static_cast<size_t> (i)]);
        return events_;
    }

    void destroy_noexcept () noexcept
    {
        delete_items ();
        _native_events.clear ();

        if (!_poller)
            return;

        void *poller = _poller;
        (void) zlink_poller_destroy (&poller);
        _poller = NULL;
    }

    struct item_t
    {
        void *socket_handle;
        zlink_fd_t fd;
        void *timer_handle;
        timer_t *timer;
        poll_source_kind_t source_kind;
        poll_event_flag_t events;
        std::any tag;
    };

    int find_socket (const void *socket_handle_) const noexcept
    {
        for (size_t i = 0; i < _items.size (); ++i) {
            if (_items[i]->source_kind == poll_source_kind_t::socket
                && _items[i]->socket_handle == socket_handle_) {
                return static_cast<int> (i);
            }
        }
        return -1;
    }

    int find_fd (zlink_fd_t fd_) const noexcept
    {
        for (size_t i = 0; i < _items.size (); ++i) {
            if (_items[i]->source_kind == poll_source_kind_t::fd
                && _items[i]->fd == fd_)
                return static_cast<int> (i);
        }
        return -1;
    }

    int find_timer (const void *timer_handle_) const noexcept
    {
        for (size_t i = 0; i < _items.size (); ++i) {
            if (_items[i]->source_kind == poll_source_kind_t::timer
                && _items[i]->timer_handle == timer_handle_)
                return static_cast<int> (i);
        }
        return -1;
    }

    poll_event_t make_event (const zlink_poller_event_t &native_event_) const noexcept
    {
        const item_t *item =
          static_cast<const item_t *> (native_event_.user_data);
        poll_event_t event;
        if ((item && item->source_kind == poll_source_kind_t::fd)
            || (!item && native_event_.source_kind == ZLINK_POLLER_SOURCE_FD))
            event.fd = native_event_.fd;
        else
            event.fd = std::nullopt;
        event.timer =
          item && item->source_kind == poll_source_kind_t::timer
            ? item->timer
            : NULL;
        event.tag = item ? item->tag : std::any ();
        event.events = item ? item->events : poll_event_flag_t::none;
        event.revents = static_cast<poll_event_flag_t> (native_event_.events);
        return event;
    }

    static poll_source_kind_t
    to_source_kind (zlink_poller_source_kind_t source_kind_) noexcept
    {
        switch (source_kind_) {
        case ZLINK_POLLER_SOURCE_FD:
            return poll_source_kind_t::fd;
        case ZLINK_POLLER_SOURCE_TIMER:
            return poll_source_kind_t::timer;
        case ZLINK_POLLER_SOURCE_SOCKET:
        default:
            return poll_source_kind_t::socket;
        }
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
