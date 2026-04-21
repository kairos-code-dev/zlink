/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICE_MONITOR_HPP_INCLUDED
#define ZLINK_CPP_SERVICE_MONITOR_HPP_INCLUDED

#include "error.hpp"
#include "services/discovery.hpp"
#include "services/spot.hpp"
#include "types.hpp"

namespace zlink
{

namespace service
{
class discovery_t;
}

class service_monitor_handle_t
{
  public:
    service_monitor_handle_t () : _monitor (NULL) {}

    ~service_monitor_handle_t () { close (); }

    service_monitor_handle_t (service_monitor_handle_t &&other) noexcept
        : _monitor (other._monitor), _event_handler (other._event_handler),
          _event_userdata (other._event_userdata)
    {
        other._monitor = NULL;
        other._event_handler = NULL;
        other._event_userdata = NULL;
    }

    service_monitor_handle_t &
    operator= (service_monitor_handle_t &&other) noexcept
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

    service_monitor_handle_t (const service_monitor_handle_t &) = delete;
    service_monitor_handle_t &
    operator= (const service_monitor_handle_t &) = delete;

    bool valid () const noexcept { return _monitor != NULL; }
    void *handle () noexcept { return _monitor; }
    const void *handle () const noexcept { return _monitor; }

    void on_event (service_event_handler_fn handler_, void *userdata_ = NULL)
    {
        _event_handler = handler_;
        _event_userdata = userdata_;
        detail::throw_if_failed<handler_error_t> (
          static_cast<handler_result_t> (
            zlink_service_monitor_handler (
              _monitor, &service_monitor_handle_t::event_trampoline, this)));
    }

    service_event_t recv ()
    {
        zlink_service_event_t event;
        detail::throw_if_failed<recv_error_t> (
          static_cast<recv_result_t> (
            zlink_service_monitor_recv (
              _monitor, &event, ZLINK_RECV_FLAGS_NONE)));
        return service_event_t (event);
    }

    maybe_t<service_event_t> recv (non_blocking_t)
    {
        zlink_service_event_t event;
        const recv_result_t result = static_cast<recv_result_t> (
          zlink_service_monitor_recv (
            _monitor, &event, ZLINK_RECV_FLAGS_DONTWAIT));
        if (result == recv_result_t::ok)
            return maybe_t<service_event_t> (service_event_t (event));
        if (result == recv_result_t::no_data)
            return maybe_t<service_event_t> ();
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
    explicit service_monitor_handle_t (void *monitor_) : _monitor (monitor_) {}

    friend class service::discovery_t;

    static void event_trampoline (const zlink_service_event_t *event_,
                                  void *userdata_)
    {
        service_monitor_handle_t *self =
          static_cast<service_monitor_handle_t *> (userdata_);
        if (!self || !self->_event_handler || !event_)
            return;
        service_event_t event (*event_);
        self->_event_handler (&event, self->_event_userdata);
    }

    void *_monitor;
    service_event_handler_fn _event_handler = NULL;
    void *_event_userdata = NULL;
};

inline service_monitor_handle_t
service::discovery_t::monitor_open (service_monitor_event events_)
{
    zlink_service_monitor_open_options_t options;
    options.events =
      static_cast<zlink_service_monitor_event_mask_t> (events_);
    void *monitor =
      zlink_service_monitor_open (handle (), &options);
    if (!monitor)
        throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    return service_monitor_handle_t (monitor);
}

} // namespace zlink

#endif
