/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_MONITOR_HPP_INCLUDED
#define ZLINK_CPP_MONITOR_HPP_INCLUDED

#include "error.hpp"
#include "types.hpp"

namespace zlink
{

class base_socket_t;

class monitor_handle_t
{
  public:
    monitor_handle_t () : _monitor (NULL) {}

    template<typename SocketLike>
    static monitor_handle_t open (const SocketLike &socket_,
                                  monitor_event events_ = monitor_event::all)
    {
        return monitor_handle_t (socket_, events_);
    }

    ~monitor_handle_t () { (void) close (); }

    monitor_handle_t (monitor_handle_t &&other) noexcept
        : _monitor (other._monitor)
    {
        other._monitor = NULL;
    }

    monitor_handle_t &operator= (monitor_handle_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        (void) close ();
        _monitor = other._monitor;
        other._monitor = NULL;
        return *this;
    }

    monitor_handle_t (const monitor_handle_t &) = delete;
    monitor_handle_t &operator= (const monitor_handle_t &) = delete;

    bool valid () const noexcept { return _monitor != NULL; }

    void *handle () noexcept { return _monitor; }
    const void *handle () const noexcept { return _monitor; }

    int on_event (monitor_event_handler_fn handler_, void *userdata_ = NULL)
    {
        if (!handler_) {
            errno = EINVAL;
            return -1;
        }
        monitor_event_handler_fn previous_handler = _event_handler;
        void *previous_userdata = _event_userdata;
        _event_handler = handler_;
        _event_userdata = userdata_;
        const int rc = zlink_socket_monitor_handler (
          _monitor, &monitor_handle_t::event_trampoline, this);
        if (rc != 0) {
            _event_handler = previous_handler;
            _event_userdata = previous_userdata;
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD monitor_event_t recv ()
    {
        zlink_monitor_event_t event;
        const int rc = zlink_socket_monitor_recv (_monitor, &event, 0);
        throw_on_error (rc);
        return monitor_event_t (event);
    }

    ZLINK_CPP_NODISCARD maybe_t<monitor_event_t> try_recv ()
    {
        zlink_monitor_event_t event;
        const int rc = zlink_socket_monitor_recv (_monitor, &event, ZLINK_DONTWAIT);
        if (rc == 0)
            return maybe_t<monitor_event_t> (monitor_event_t (event));
        if (errno == EAGAIN)
            return maybe_t<monitor_event_t> ();
        throw_on_error (rc);
        return maybe_t<monitor_event_t> ();
    }

    ZLINK_CPP_NODISCARD monitor_snapshot_t snapshot () const
    {
        zlink_monitor_snapshot_t snapshot;
        const int rc = zlink_monitor_snapshot (_monitor, &snapshot);
        throw_on_error (rc);
        return monitor_snapshot_t (snapshot);
    }

    int close () noexcept
    {
        if (!_monitor)
            return 0;

        void *monitor = _monitor;
        const int rc = zlink_monitor_close (&monitor);
        if (rc == 0) {
            _monitor = NULL;
            _event_handler = NULL;
            _event_userdata = NULL;
        }
        return rc;
    }

  private:
    template<typename SocketLike>
    explicit monitor_handle_t (const SocketLike &socket_,
                               monitor_event events_ = monitor_event::all)
        : _monitor (NULL)
    {
        zlink_socket_monitor_open_options_t options;
        options.events =
          static_cast<zlink_socket_monitor_event_mask_t> (events_);
        _monitor = zlink_socket_monitor_open (
          const_cast<void *> (socket_.handle ()), &options);
    }

    explicit monitor_handle_t (void *monitor_) : _monitor (monitor_) {}

    friend class base_socket_t;

    static void event_trampoline (const zlink_monitor_event_t *event_,
                                  void *userdata_)
    {
        monitor_handle_t *self = static_cast<monitor_handle_t *> (userdata_);
        if (!self || !self->_event_handler || !event_)
            return;
        monitor_event_t event (*event_);
        self->_event_handler (&event, self->_event_userdata);
    }

    void *_monitor;
    monitor_event_handler_fn _event_handler = NULL;
    void *_event_userdata = NULL;
};

} // namespace zlink

#endif
