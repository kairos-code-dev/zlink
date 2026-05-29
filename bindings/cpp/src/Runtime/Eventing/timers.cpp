/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Eventing/timers.hpp>

#include <Runtime/Eventing/timer_access.hpp>
#include <Runtime/Service/spot_access.hpp>

#include <zlink.h>

namespace zlink
{

struct zlink_timer_t::impl
{
    void *handle = NULL;
    std::function<void(uint64_t)> handler;
};

namespace detail
{

void *timer_access_t::native_handle (zlink_timer_t &timer_) noexcept
{
    return timer_._impl ? timer_._impl->handle : NULL;
}

const void *timer_access_t::native_handle (const zlink_timer_t &timer_) noexcept
{
    return timer_._impl ? timer_._impl->handle : NULL;
}

} // namespace detail

zlink_timer_t::zlink_timer_t () : _impl (new impl)
{
    _impl->handle = zlink_timer_new ();
}

zlink_timer_t::~zlink_timer_t ()
{
    try {
        close ();
    } catch (...) {
    }
}

zlink_timer_t::zlink_timer_t (zlink_timer_t &&other) noexcept = default;

zlink_timer_t &zlink_timer_t::operator= (zlink_timer_t &&other) noexcept
{
    if (this == &other)
        return *this;
    try {
        close ();
    } catch (...) {
    }
    _impl = std::move (other._impl);
    return *this;
}

zlink_timer_t zlink_timer_t::from_spot (service::spot_t &spot_)
{
    zlink_timer_t out;
    if (out._impl->handle) {
        void *tmp = out._impl->handle;
        (void) zlink_timer_destroy (&tmp);
        out._impl->handle = NULL;
    }
    out._impl->handle =
      zlink_spot_timer_new (zlink::detail::native_handle (spot_));
    if (!out._impl->handle)
        throw config_error_t (config_result_t::invalid_handle, zlink_errno ());
    return out;
}

bool zlink_timer_t::valid () const noexcept { return _impl && _impl->handle != NULL; }

void zlink_timer_t::start_ns (uint64_t interval_ns_, uint64_t repeat_count_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_timer_start (_impl->handle, interval_ns_, repeat_count_)));
}

void zlink_timer_t::stop ()
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_timer_stop (_impl->handle)));
}

std::optional<uint64_t> zlink_timer_t::recv ()
{
    uint64_t fire_count = 0;
    const recv_result_t result =
      static_cast<recv_result_t> (zlink_timer_recv (_impl->handle, &fire_count));
    if (result == recv_result_t::no_data)
        return std::nullopt;
    if (result != recv_result_t::ok)
        throw recv_error_t (result, zlink_errno ());
    return std::optional<uint64_t> (fire_count);
}

void zlink_timer_t::on_fire (std::function<void(uint64_t)> handler_)
{
    _impl->handler = std::move (handler_);
    detail::throw_if_failed<handler_error_t> (
      static_cast<handler_result_t> (
        zlink_timer_handler (
          _impl->handle,
          [] (void *, uint64_t fire_count_, void *userdata_) {
              zlink_timer_t *self = static_cast<zlink_timer_t *> (userdata_);
              if (!self || !self->_impl || !self->_impl->handler)
                  return;
              self->_impl->handler (fire_count_);
          },
          this)));
}

void zlink_timer_t::close ()
{
    if (!_impl || !_impl->handle)
        return;

    void *timer = _impl->handle;
    _impl->handle = NULL;
    detail::throw_if_failed<close_error_t> (
      static_cast<close_result_t> (zlink_timer_destroy (&timer)));
}

} // namespace zlink
