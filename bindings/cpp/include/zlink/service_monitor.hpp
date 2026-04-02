/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICE_MONITOR_HPP_INCLUDED
#define ZLINK_CPP_SERVICE_MONITOR_HPP_INCLUDED

#include "error.hpp"
#include "services/discovery.hpp"
#include "services/spot.hpp"
#include "types.hpp"

namespace zlink
{

namespace detail
{

inline service_monitor_event
normalize_spot_service_monitor_events (service_monitor_event events_) noexcept
{
    if (events_ != service_monitor_event::all)
        return events_;

    return service_monitor_event::error
           | service_monitor_event::spot_filter_applied
           | service_monitor_event::spot_subscription_ready_changed
           | service_monitor_event::spot_sub_delivery_ready_changed;
}

} // namespace detail

class service_monitor_handle_t
{
  public:
    service_monitor_handle_t () : _monitor (NULL) {}

    explicit service_monitor_handle_t (void *monitor_) : _monitor (monitor_) {}

    explicit service_monitor_handle_t (
      void *target_, service_monitor_event events_ = service_monitor_event::all)
        : _monitor (NULL)
    {
        zlink_service_monitor_open_options_t options;
        options.events =
          static_cast<zlink_service_monitor_event_mask_t> (events_);
        _monitor = zlink_service_monitor_open (target_, &options);
    }

    explicit service_monitor_handle_t (
      service::discovery_t &discovery_,
      service_monitor_event events_ = service_monitor_event::all)
        : service_monitor_handle_t (discovery_.handle (), events_)
    {
    }

    explicit service_monitor_handle_t (
      service::spot_t &spot_,
      service_monitor_event events_ = service_monitor_event::all)
        : service_monitor_handle_t (
            spot_.handle (),
            detail::normalize_spot_service_monitor_events (events_))
    {
    }

    explicit service_monitor_handle_t (
      service::spot_node_t &node_,
      service_monitor_event events_ = service_monitor_event::all)
        : service_monitor_handle_t (
            node_.handle (),
            detail::normalize_spot_service_monitor_events (events_))
    {
    }

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

    static service_monitor_handle_t
    open (void *target_, service_monitor_event events_ = service_monitor_event::all)
    {
        return service_monitor_handle_t (target_, events_);
    }

    bool valid () const noexcept { return _monitor != NULL; }

    void *handle () noexcept { return _monitor; }
    const void *handle () const noexcept { return _monitor; }

    int on_event (zlink_service_monitor_handler_fn handler_,
                  void *userdata_ = NULL)
    {
        return zlink_service_monitor_handler (_monitor, handler_, userdata_);
    }

    ZLINK_CPP_NODISCARD zlink_service_monitor_event_t recv ()
    {
        zlink_service_monitor_event_t event;
        const int rc = zlink_service_monitor_recv (_monitor, &event, 0);
        throw_on_error (rc);
        return event;
    }

    ZLINK_CPP_NODISCARD maybe_t<zlink_service_monitor_event_t> try_recv ()
    {
        zlink_service_monitor_event_t event;
        const int rc =
          zlink_service_monitor_recv (_monitor, &event, ZLINK_DONTWAIT);
        if (rc == 0)
            return maybe_t<zlink_service_monitor_event_t> (event);
        if (errno == EAGAIN)
            return maybe_t<zlink_service_monitor_event_t> ();
        throw_on_error (rc);
        return maybe_t<zlink_service_monitor_event_t> ();
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

inline service_monitor_handle_t
service::discovery_t::monitor_open (service_monitor_event events_)
{
    return service_monitor_handle_t (*this, events_);
}

inline service_monitor_handle_t
service::spot_t::monitor_open (service_monitor_event events_)
{
    return service_monitor_handle_t (*this, events_);
}

inline service_monitor_handle_t
service::spot_node_t::monitor_open (service_monitor_event events_)
{
    return service_monitor_handle_t (*this, events_);
}

} // namespace zlink

#endif
