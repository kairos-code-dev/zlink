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
    inline static zlink_monitor_handler_fn ignore_handler =
      &zlink_monitor_ignore_handler;

    template<typename SocketLike>
    static monitor_handle_t open (const SocketLike &socket_,
                                  monitor_event events_ = monitor_event::all)
    {
        zlink_socket_monitor_open_options_t options;
        options.events =
          static_cast<zlink_socket_monitor_event_mask_t> (events_);
        void *monitor = zlink_socket_monitor_open (
          const_cast<void *> (socket_.handle ()), &options);
        if (!monitor)
            throw config_error_t (
              config_result_t::invalid_handle, zlink_errno ());
        return monitor_handle_t (monitor);
    }

    ~monitor_handle_t () { close (); }

    monitor_handle_t (monitor_handle_t &&other) noexcept
        : _monitor (other._monitor), _event_handler (other._event_handler),
          _event_userdata (other._event_userdata)
    {
        other._monitor = NULL;
        other._event_handler = NULL;
        other._event_userdata = NULL;
    }

    monitor_handle_t &operator= (monitor_handle_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        close ();
        _monitor = other._monitor;
        _event_handler = other._event_handler;
        _event_userdata = other._event_userdata;
        other._monitor = NULL;
        other._event_handler = NULL;
        other._event_userdata = NULL;
        return *this;
    }

    monitor_handle_t (const monitor_handle_t &) = delete;
    monitor_handle_t &operator= (const monitor_handle_t &) = delete;

    bool valid () const noexcept { return _monitor != NULL; }
    void *handle () noexcept { return _monitor; }
    const void *handle () const noexcept { return _monitor; }

    void on_event (monitor_event_handler_fn handler_, void *userdata_ = NULL)
    {
        detail::throw_if_failed<handler_error_t> (
          static_cast<handler_result_t> (
            zlink_socket_monitor_handler (
              _monitor, &monitor_handle_t::event_trampoline, this)));
        _event_handler = handler_;
        _event_userdata = userdata_;
    }

    void on_event (zlink_socket_monitor_handler_fn handler_,
                   void *userdata_ = NULL)
    {
        detail::throw_if_failed<handler_error_t> (
          static_cast<handler_result_t> (
            zlink_socket_monitor_handler (_monitor, handler_, userdata_)));
        _event_handler = NULL;
        _event_userdata = NULL;
    }

    monitor_event_t recv ()
    {
        zlink_monitor_event_t event;
        detail::throw_if_failed<recv_error_t> (
          static_cast<recv_result_t> (
            zlink_socket_monitor_recv (_monitor, &event, ZLINK_RECV_FLAGS_NONE)));
        return monitor_event_t (event);
    }

    maybe_t<monitor_event_t> recv (non_blocking_t)
    {
        zlink_monitor_event_t event;
        const recv_result_t result = static_cast<recv_result_t> (
          zlink_socket_monitor_recv (
            _monitor, &event, ZLINK_RECV_FLAGS_DONTWAIT));
        if (result == recv_result_t::ok)
            return maybe_t<monitor_event_t> (monitor_event_t (event));
        if (result == recv_result_t::no_data)
            return maybe_t<monitor_event_t> ();
        throw recv_error_t (result, zlink_errno ());
    }

    monitor_snapshot_t snapshot () const
    {
        zlink_monitor_snapshot_t snapshot;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_monitor_snapshot (_monitor, &snapshot)));
        return monitor_snapshot_t (snapshot);
    }

    void close () noexcept
    {
        if (!_monitor)
            return;
        void *monitor = _monitor;
        (void) zlink_monitor_close (&monitor);
        _monitor = NULL;
        _event_handler = NULL;
        _event_userdata = NULL;
    }

  private:
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
