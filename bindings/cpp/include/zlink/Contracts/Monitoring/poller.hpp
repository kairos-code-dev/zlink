/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_POLLER_HPP_INCLUDED
#define ZLINK_CPP_POLLER_HPP_INCLUDED

#include "../Errors/error.hpp"
#include "timers.hpp"

#include <algorithm>
#include <any>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace zlink
{

namespace service
{
class spot_t;
class spot_node_t;
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
    void *raw_tag = NULL;
    std::any tag;
    poll_event_flag_t events;
    poll_event_flag_t revents;
};

class poller_t
{
  public:
    poller_t ()
        : _poller (zlink_poller_new ()),
          _socket_native_events_dirty (false),
          _socket_poll_items_dirty (true),
          _non_socket_item_count (0)
    {
    }

    ~poller_t () { destroy_noexcept (); }

    poller_t (poller_t &&other) noexcept
        : _poller (other._poller),
          _items (std::move (other._items)),
          _native_events (std::move (other._native_events)),
          _poll_items (std::move (other._poll_items)),
          _poll_item_indexes (std::move (other._poll_item_indexes)),
          _socket_item_indexes (std::move (other._socket_item_indexes)),
          _socket_native_events_dirty (other._socket_native_events_dirty),
          _socket_poll_items_dirty (other._socket_poll_items_dirty),
          _non_socket_item_count (other._non_socket_item_count)
    {
        other._poller = NULL;
        other._socket_item_indexes.clear ();
        other._socket_native_events_dirty = false;
        other._socket_poll_items_dirty = true;
        other._non_socket_item_count = 0;
    }

    poller_t &operator= (poller_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        destroy_noexcept ();
        _poller = other._poller;
        _items = std::move (other._items);
        _native_events = std::move (other._native_events);
        _poll_items = std::move (other._poll_items);
        _poll_item_indexes = std::move (other._poll_item_indexes);
        _socket_item_indexes = std::move (other._socket_item_indexes);
        _socket_native_events_dirty = other._socket_native_events_dirty;
        _socket_poll_items_dirty = other._socket_poll_items_dirty;
        _non_socket_item_count = other._non_socket_item_count;
        other._poller = NULL;
        other._socket_item_indexes.clear ();
        other._socket_native_events_dirty = false;
        other._socket_poll_items_dirty = true;
        other._non_socket_item_count = 0;
        return *this;
    }

    poller_t (const poller_t &) = delete;
    poller_t &operator= (const poller_t &) = delete;

    bool valid () const noexcept { return _poller != NULL; }

    int size () const
    {
        if (!_poller)
            throw config_error_t (config_result_t::invalid_handle, EINVAL);
        return static_cast<int> (_items.size ());
    }

    template<typename SocketLike>
    void add (SocketLike &socket_,
              poll_event_flag_t events_,
              std::any tag_ = {})
    {
        add_socket_impl (socket_, events_, std::move (tag_), NULL, false);
    }

    template<typename SocketLike>
    void add (SocketLike &socket_, poll_event_flag_t events_, void *raw_tag_)
    {
        add_socket_impl (socket_, events_, {}, raw_tag_, true);
    }

    void add_fd (zlink_fd_t fd_,
                 poll_event_flag_t events_,
                 std::any tag_ = {})
    {
        add_fd_impl (fd_, events_, std::move (tag_), NULL, false);
    }

    void add_fd (zlink_fd_t fd_, poll_event_flag_t events_, void *raw_tag_)
    {
        add_fd_impl (fd_, events_, {}, raw_tag_, true);
    }

    void add (timer_t &timer_, std::any tag_ = {})
    {
        add_timer_impl (timer_, std::move (tag_), NULL, false);
    }

    void add (timer_t &timer_, void *raw_tag_)
    {
        add_timer_impl (timer_, {}, raw_tag_, true);
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

        if (is_socket_only ()) {
            _items[static_cast<size_t> (index)]->events = events_;
            _socket_native_events_dirty = true;
            _socket_poll_items_dirty = true;
            return;
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

        _items.erase (_items.begin () + index);
        rebuild_socket_item_indexes ();
        _socket_poll_items_dirty = true;
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

        _items.erase (_items.begin () + index);
        rebuild_socket_item_indexes ();
        --_non_socket_item_count;
        _socket_poll_items_dirty = true;
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

        _items.erase (_items.begin () + index);
        rebuild_socket_item_indexes ();
        --_non_socket_item_count;
        _socket_poll_items_dirty = true;
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

        if (is_socket_only ()) {
            std::vector<poll_event_t> events;
            if (wait_socket_items_impl (events, 1, timeout_) == 0)
                return std::nullopt;
            return std::optional<poll_event_t> (std::move (events[0]));
        }

        sync_socket_native_events_if_needed ();

        zlink_poller_event_t native_event;
        zlink_config_result_t error = ZLINK_CONFIG_OK;
        const int rc = zlink_poller_wait (
          _poller, &native_event, 1, timeout_, &error);
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
    std::vector<poll_event_t> wait (
      size_t max_events_,
      std::optional<std::chrono::milliseconds> timeout_ = std::nullopt)
    {
        std::vector<poll_event_t> events;
        wait_into_impl (
          events, max_events_,
          timeout_ ? static_cast<long> (timeout_->count ()) : -1L);
        return events;
    }

    size_t wait (std::vector<poll_event_t> &events_,
                 size_t max_events_ = 0,
                 std::optional<std::chrono::milliseconds> timeout_ = std::nullopt)
    {
        return wait_into_impl (
          events_, max_events_,
          timeout_ ? static_cast<long> (timeout_->count ()) : -1L);
    }

    void destroy ()
    {
        if (!_poller) {
            delete_items ();
            _native_events.clear ();
            _poll_items.clear ();
            _poll_item_indexes.clear ();
            _socket_item_indexes.clear ();
            _socket_native_events_dirty = false;
            _socket_poll_items_dirty = true;
            _non_socket_item_count = 0;
            return;
        }

        void *poller = _poller;
        const close_result_t result =
          static_cast<close_result_t> (zlink_poller_destroy (&poller));
        detail::throw_if_failed<close_error_t> (result);
        _poller = NULL;
        delete_items ();
        _native_events.clear ();
        _poll_items.clear ();
        _poll_item_indexes.clear ();
        _socket_item_indexes.clear ();
        _socket_native_events_dirty = false;
        _socket_poll_items_dirty = true;
        _non_socket_item_count = 0;
    }

  private:
    size_t wait_into_impl (std::vector<poll_event_t> &events_,
                           size_t max_events_,
                           long timeout_)
    {
        if (!_poller) {
            throw recv_error_t (recv_result_t::invalid_handle, zlink_errno ());
        }

        const size_t registered = _items.size ();
        if (registered == 0) {
            events_.clear ();
            return 0;
        }

        if (is_socket_only ())
            return wait_socket_items_impl (events_, max_events_, timeout_);

        sync_socket_native_events_if_needed ();

        const size_t capacity =
          max_events_ > 0
            ? std::min (max_events_, registered)
            : registered;

        _native_events.resize (capacity);
        zlink_config_result_t error = ZLINK_CONFIG_OK;
        const int rc = zlink_poller_wait (
          _poller, &_native_events[0], static_cast<int> (capacity), timeout_, &error);
        if (rc <= 0) {
            if (rc == 0) {
                events_.clear ();
                return 0;
            }
            const int err = zlink_errno ();
            if (err == EINTR || err == EAGAIN) {
                errno = err;
                events_.clear ();
                return 0;
            }
            throw recv_error_t (recv_result_t::invalid_handle, err);
        }

        events_.resize (static_cast<size_t> (rc));
        for (int i = 0; i < rc; ++i)
            fill_event (
              events_[static_cast<size_t> (i)],
              _native_events[static_cast<size_t> (i)]);
        return static_cast<size_t> (rc);
    }

    void destroy_noexcept () noexcept
    {
        delete_items ();
        _native_events.clear ();
        _poll_items.clear ();
        _poll_item_indexes.clear ();
        _socket_item_indexes.clear ();
        _socket_native_events_dirty = false;
        _socket_poll_items_dirty = true;
        _non_socket_item_count = 0;

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
        void *raw_tag;
        std::any tag;
    };

    void ensure_addable () const
    {
        if (!_poller)
            throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    }

    void commit_added_item (std::unique_ptr<item_t> item_, config_result_t rc_)
    {
        detail::throw_if_failed<config_error_t> (rc_);
        const bool socket_item = item_->source_kind == poll_source_kind_t::socket;
        const void *socket_handle = item_->socket_handle;
        const size_t index = _items.size ();
        _items.push_back (std::move (item_));
        if (socket_item)
            _socket_item_indexes[socket_handle] = index;
        _socket_poll_items_dirty = true;
    }

    template<typename SocketLike>
    void add_socket_impl (SocketLike &socket_,
                          poll_event_flag_t events_,
                          std::any tag_,
                          void *raw_tag_,
                          bool use_raw_tag_)
    {
        ensure_addable ();
        void *socket_handle = detail::native_handle (socket_);
        if (find_socket (socket_handle) >= 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());

        std::unique_ptr<item_t> item (new item_t ());
        item->socket_handle = socket_handle;
        item->fd = 0;
        item->timer_handle = NULL;
        item->timer = NULL;
        item->source_kind = poll_source_kind_t::socket;
        item->events = events_;
        item->raw_tag = use_raw_tag_ ? raw_tag_ : NULL;
        item->tag = use_raw_tag_ ? std::any () : std::move (tag_);

        item_t *raw_item = item.get ();
        const config_result_t rc = static_cast<config_result_t> (
          zlink_poller_add (
            _poller, socket_handle, raw_item, static_cast<short> (events_)));
        commit_added_item (std::move (item), rc);
    }

    void add_fd_impl (zlink_fd_t fd_,
                      poll_event_flag_t events_,
                      std::any tag_,
                      void *raw_tag_,
                      bool use_raw_tag_)
    {
        ensure_addable ();
        sync_socket_native_events_if_needed ();
        if (find_fd (fd_) >= 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());

        std::unique_ptr<item_t> item (new item_t ());
        item->socket_handle = NULL;
        item->fd = fd_;
        item->timer_handle = NULL;
        item->timer = NULL;
        item->source_kind = poll_source_kind_t::fd;
        item->events = events_;
        item->raw_tag = use_raw_tag_ ? raw_tag_ : NULL;
        item->tag = use_raw_tag_ ? std::any () : std::move (tag_);

        item_t *raw_item = item.get ();
        const config_result_t rc = static_cast<config_result_t> (
          zlink_poller_add_fd (
            _poller, fd_, raw_item, static_cast<short> (events_)));
        commit_added_item (std::move (item), rc);
        ++_non_socket_item_count;
    }

    void add_timer_impl (timer_t &timer_,
                         std::any tag_,
                         void *raw_tag_,
                         bool use_raw_tag_)
    {
        ensure_addable ();
        sync_socket_native_events_if_needed ();
        void *timer_handle = detail::native_handle (timer_);
        if (find_timer (timer_handle) >= 0)
            throw config_error_t (config_result_t::invalid_argument, zlink_errno ());

        std::unique_ptr<item_t> item (new item_t ());
        item->socket_handle = NULL;
        item->fd = 0;
        item->timer_handle = timer_handle;
        item->timer = &timer_;
        item->source_kind = poll_source_kind_t::timer;
        item->events = poll_event_flag_t::pollin;
        item->raw_tag = use_raw_tag_ ? raw_tag_ : NULL;
        item->tag = use_raw_tag_ ? std::any () : std::move (tag_);

        item_t *raw_item = item.get ();
        const config_result_t rc = static_cast<config_result_t> (
          zlink_poller_add_timer (_poller, timer_handle, raw_item));
        commit_added_item (std::move (item), rc);
        ++_non_socket_item_count;
    }

    int find_socket (const void *socket_handle_) const noexcept
    {
        const auto cached = _socket_item_indexes.find (socket_handle_);
        if (cached != _socket_item_indexes.end ()) {
            const size_t index = cached->second;
            if (index < _items.size ()
                && _items[index]->source_kind == poll_source_kind_t::socket
                && _items[index]->socket_handle == socket_handle_) {
                return static_cast<int> (index);
            }
        }

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
        poll_event_t event;
        fill_event (event, native_event_);
        return event;
    }

    void fill_event (poll_event_t &event_,
                     const zlink_poller_event_t &native_event_) const noexcept
    {
        const item_t *item =
          static_cast<const item_t *> (native_event_.user_data);
        if ((item && item->source_kind == poll_source_kind_t::fd)
            || (!item && native_event_.source_kind == ZLINK_POLLER_SOURCE_FD))
            event_.fd = native_event_.fd;
        else
            event_.fd = std::nullopt;
        event_.timer =
          item && item->source_kind == poll_source_kind_t::timer
            ? item->timer
            : NULL;
        fill_event_metadata (
          event_, item, item ? item->events : poll_event_flag_t::none,
          static_cast<poll_event_flag_t> (native_event_.events));
    }

    void fill_event_from_item (poll_event_t &event_,
                               const item_t &item_,
                               short revents_) const noexcept
    {
        event_.fd = std::nullopt;
        event_.timer = NULL;
        fill_event_metadata (
          event_, &item_, item_.events,
          static_cast<poll_event_flag_t> (revents_));
    }

    void fill_event_metadata (poll_event_t &event_,
                              const item_t *item_,
                              poll_event_flag_t events_,
                              poll_event_flag_t revents_) const noexcept
    {
        event_.raw_tag = item_ ? item_->raw_tag : NULL;
        if (item_ && item_->tag.has_value ())
            event_.tag = item_->tag;
        else if (event_.tag.has_value ())
            event_.tag.reset ();
        event_.events = events_;
        event_.revents = revents_;
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
        _items.clear ();
        _socket_item_indexes.clear ();
    }

    void rebuild_socket_item_indexes ()
    {
        _socket_item_indexes.clear ();
        _socket_item_indexes.reserve (_items.size ());
        for (size_t i = 0; i < _items.size (); ++i) {
            const item_t &item = *_items[i];
            if (item.source_kind == poll_source_kind_t::socket)
                _socket_item_indexes[item.socket_handle] = i;
        }
    }

    bool is_socket_only () const noexcept
    {
        return _non_socket_item_count == 0;
    }

    void sync_socket_native_events_if_needed ()
    {
        if (!_socket_native_events_dirty)
            return;

        for (size_t i = 0; i < _items.size (); ++i) {
            const item_t &item = *_items[i];
            if (item.source_kind != poll_source_kind_t::socket)
                continue;
            const config_result_t rc = static_cast<config_result_t> (
              zlink_poller_modify (_poller, item.socket_handle,
                                   static_cast<short> (item.events)));
            detail::throw_if_failed<config_error_t> (rc);
        }
        _socket_native_events_dirty = false;
    }

    size_t wait_socket_items_impl (std::vector<poll_event_t> &events_,
                                   size_t max_events_,
                                   long timeout_)
    {
        const size_t registered = _items.size ();
        const size_t capacity =
          max_events_ > 0
            ? std::min (max_events_, registered)
            : registered;

        rebuild_socket_poll_items_if_needed ();

        if (_poll_items.empty ()) {
            events_.clear ();
            return 0;
        }

        for (size_t i = 0; i < _poll_items.size (); ++i)
            _poll_items[i].revents = 0;

        zlink_config_result_t error = ZLINK_CONFIG_OK;
        const int rc = zlink_poll (
          &_poll_items[0], static_cast<int> (_poll_items.size ()),
          timeout_, &error);
        if (rc <= 0) {
            if (rc == 0) {
                events_.clear ();
                return 0;
            }
            const int err = zlink_errno ();
            if (err == EINTR || err == EAGAIN) {
                errno = err;
                events_.clear ();
                return 0;
            }
            throw recv_error_t (recv_result_t::invalid_handle, err);
        }

        events_.resize (capacity);
        size_t out = 0;
        for (size_t i = 0; i < _poll_items.size () && out < capacity; ++i) {
            const short revents = _poll_items[i].revents;
            if (revents == 0)
                continue;
            fill_event_from_item (
              events_[out], *_items[_poll_item_indexes[i]], revents);
            ++out;
        }
        events_.resize (out);
        return out;
    }

    void rebuild_socket_poll_items_if_needed ()
    {
        if (!_socket_poll_items_dirty)
            return;

        _poll_items.clear ();
        _poll_item_indexes.clear ();
        _poll_items.reserve (_items.size ());
        _poll_item_indexes.reserve (_items.size ());

        for (size_t i = 0; i < _items.size (); ++i) {
            const item_t &item = *_items[i];
            const short events = static_cast<short> (item.events);
            if (events == 0)
                continue;

            zlink_pollitem_t poll_item;
            poll_item.socket = item.socket_handle;
            poll_item.fd = 0;
            poll_item.events = events;
            poll_item.revents = 0;
            _poll_items.push_back (poll_item);
            _poll_item_indexes.push_back (i);
        }

        _socket_poll_items_dirty = false;
    }

    void *_poller;
    std::vector<std::unique_ptr<item_t>> _items;
    std::vector<zlink_poller_event_t> _native_events;
    std::vector<zlink_pollitem_t> _poll_items;
    std::vector<size_t> _poll_item_indexes;
    std::unordered_map<const void *, size_t> _socket_item_indexes;
    bool _socket_native_events_dirty;
    bool _socket_poll_items_dirty;
    size_t _non_socket_item_count;
};

} // namespace zlink

#endif
