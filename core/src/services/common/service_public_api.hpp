/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SERVICES_COMMON_SERVICE_PUBLIC_API_HPP_INCLUDED__
#define __ZLINK_SERVICES_COMMON_SERVICE_PUBLIC_API_HPP_INCLUDED__

#include <atomic>
#include <cerrno>
#include <stdint.h>

namespace zlink
{
class service_public_api_guard_t
{
  public:
    service_public_api_guard_t () : _state (0) {}

    bool enter_public_api ()
    {
        const uint32_t old = _state.fetch_add (1, std::memory_order_acq_rel);
        if ((old & closing_bit) == 0)
            return true;

        _state.fetch_sub (1, std::memory_order_acq_rel);
        errno = ESHUTDOWN;
        return false;
    }

    void leave_public_api ()
    {
        _state.fetch_sub (1, std::memory_order_acq_rel);
    }

    bool begin_close_or_fail_busy ()
    {
        uint32_t old = _state.load (std::memory_order_acquire);
        while (true) {
            if ((old & closing_bit) != 0) {
                errno = EALREADY;
                return false;
            }

            if ((old & inflight_mask) != 0) {
                errno = EBUSY;
                return false;
            }

            const uint32_t desired = old | closing_bit;
            if (_state.compare_exchange_weak (
                  old, desired, std::memory_order_acq_rel,
                  std::memory_order_acquire)) {
                return true;
            }
        }
    }

    void cancel_close ()
    {
        const uint32_t old =
          _state.fetch_and (~closing_bit, std::memory_order_acq_rel);
        zlink_assert ((old & closing_bit) != 0);
    }

  private:
    static const uint32_t closing_bit = 0x80000000u;
    static const uint32_t inflight_mask = ~closing_bit;

    std::atomic<uint32_t> _state;
};

class service_public_api_scope_t
{
  public:
    explicit service_public_api_scope_t (service_public_api_guard_t &guard_) :
        _guard (&guard_),
        _entered (guard_.enter_public_api ())
    {
    }

    ~service_public_api_scope_t ()
    {
        if (_entered)
            _guard->leave_public_api ();
    }

    bool acquired () const { return _entered; }

  private:
    service_public_api_guard_t *_guard;
    bool _entered;
};
}

#endif
