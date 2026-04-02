/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_MONITOR_HPP_INCLUDED
#define ZLINK_CPP_MONITOR_HPP_INCLUDED

#include "error.hpp"
#include "types.hpp"

namespace zlink
{

class monitor_handle_t
{
  public:
    monitor_handle_t () : _monitor (NULL) {}

    explicit monitor_handle_t (void *monitor_) : _monitor (monitor_) {}

    template<typename SocketLike>
    explicit monitor_handle_t (SocketLike &socket_,
                               monitor_event events_ = monitor_event::all)
        : _monitor (NULL)
    {
        zlink_socket_monitor_open_options_t options;
        options.events =
          static_cast<zlink_socket_monitor_event_mask_t> (events_);
        _monitor = zlink_socket_monitor_open (socket_.handle (), &options);
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

    template<typename SocketLike>
    static monitor_handle_t open (SocketLike &socket_,
                                  monitor_event events_ = monitor_event::all)
    {
        return monitor_handle_t (socket_, events_);
    }

    bool valid () const noexcept { return _monitor != NULL; }

    void *handle () noexcept { return _monitor; }
    const void *handle () const noexcept { return _monitor; }

    int handler (zlink_socket_monitor_handler_fn handler_,
                 void *userdata_ = NULL)
    {
        return zlink_socket_monitor_handler (_monitor, handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD zlink_socket_monitor_event_t recv ()
    {
        zlink_socket_monitor_event_t event;
        const int rc = zlink_socket_monitor_recv (_monitor, &event, 0);
        throw_on_error (rc);
        return event;
    }

    ZLINK_CPP_NODISCARD maybe_t<zlink_socket_monitor_event_t> try_recv ()
    {
        zlink_socket_monitor_event_t event;
        const int rc = zlink_socket_monitor_recv (_monitor, &event, ZLINK_DONTWAIT);
        if (rc == 0)
            return maybe_t<zlink_socket_monitor_event_t> (event);
        if (errno == EAGAIN)
            return maybe_t<zlink_socket_monitor_event_t> ();
        throw_on_error (rc);
        return maybe_t<zlink_socket_monitor_event_t> ();
    }

    int snapshot (zlink_monitor_snapshot_t &snapshot_) const
    {
        return zlink_monitor_snapshot (_monitor, &snapshot_);
    }

    int close () noexcept
    {
        if (!_monitor)
            return 0;

        void *monitor = _monitor;
        const int rc = zlink_monitor_close (&monitor);
        if (rc == 0)
            _monitor = NULL;
        return rc;
    }

  private:
    void *_monitor;
};

typedef monitor_handle_t monitor_socket_t;

} // namespace zlink

#endif
