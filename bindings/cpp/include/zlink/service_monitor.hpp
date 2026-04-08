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

    ~service_monitor_handle_t () { (void) close (); }

    service_monitor_handle_t (service_monitor_handle_t &&other) noexcept
        : _monitor (other._monitor)
    {
        other._monitor = NULL;
    }

    service_monitor_handle_t &
    operator= (service_monitor_handle_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        (void) close ();
        _monitor = other._monitor;
        other._monitor = NULL;
        return *this;
    }

    service_monitor_handle_t (const service_monitor_handle_t &) = delete;
    service_monitor_handle_t &
    operator= (const service_monitor_handle_t &) = delete;

    bool valid () const noexcept { return _monitor != NULL; }

    void *handle () noexcept { return _monitor; }
    const void *handle () const noexcept { return _monitor; }

    int on_event (service_event_handler_fn handler_, void *userdata_ = NULL)
    {
        if (!handler_) {
            errno = EINVAL;
            return -1;
        }
        service_event_handler_fn previous_handler = _event_handler;
        void *previous_userdata = _event_userdata;
        _event_handler = handler_;
        _event_userdata = userdata_;
        const int rc = zlink_service_monitor_handler (
          _monitor, &service_monitor_handle_t::event_trampoline, this);
        if (rc != 0) {
            _event_handler = previous_handler;
            _event_userdata = previous_userdata;
        }
        return rc;
    }

    ZLINK_CPP_NODISCARD service_event_t recv ()
    {
        zlink_service_event_t event;
        const int rc = zlink_service_monitor_recv (_monitor, &event, 0);
        throw_on_error (rc);
        return service_event_t (event);
    }

    ZLINK_CPP_NODISCARD maybe_t<service_event_t> try_recv ()
    {
        zlink_service_event_t event;
        const int rc =
          zlink_service_monitor_recv (_monitor, &event, ZLINK_DONTWAIT);
        if (rc == 0)
            return maybe_t<service_event_t> (service_event_t (event));
        if (errno == EAGAIN)
            return maybe_t<service_event_t> ();
        throw_on_error (rc);
        return maybe_t<service_event_t> ();
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
    return service_monitor_handle_t (
      zlink_service_monitor_open (handle (), &options));
}

} // namespace zlink

#endif
