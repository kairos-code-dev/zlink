/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>

namespace zlink::framework::runtime
{

struct inbound_dispatch_snapshot_t
{
    std::uint64_t application_hwm_bytes = 0;
    std::uint64_t pending_payload_bytes = 0;
    std::uint64_t queued_payload_bytes = 0;
    std::uint64_t active_payload_bytes = 0;
    bool application_receive_paused = false;
};

class inbound_dispatch_budget_t
{
  public:
    explicit inbound_dispatch_budget_t (std::uint64_t application_hwm_bytes) :
        _application_hwm_bytes (application_hwm_bytes)
    {
    }

    bool can_start_application_receive () const noexcept
    {
        std::lock_guard lock (_mutex);
        return can_receive_locked ();
    }

    // Blocks an ingress loop until the host-wide payload budget admits another
    // complete application message or the caller requests shutdown. The
    // condition variable is signalled by terminal completion, so callers do
    // not need a polling delay or retry loop.
    bool wait_for_application_capacity (const std::function<bool ()> &stop_requested = {})
    {
        std::unique_lock lock (_mutex);
        _capacity_changed.wait (lock, [&] {
            return can_receive_locked () || (stop_requested && stop_requested ());
        });
        return can_receive_locked () && !(stop_requested && stop_requested ());
    }

    void wake_waiters () noexcept { _capacity_changed.notify_all (); }

    void received (std::uint64_t payload_bytes)
    {
        std::lock_guard lock (_mutex);
        _cumulative_received_payload_bytes += payload_bytes;
        if (_application_hwm_bytes != 0
            && pending_locked () >= _application_hwm_bytes)
            _application_receive_paused = true;
    }

    void handler_started (std::uint64_t payload_bytes)
    {
        std::lock_guard lock (_mutex);
        _active_payload_bytes += payload_bytes;
    }

    void completed (std::uint64_t payload_bytes,
                    bool handler_started) noexcept
    {
        std::lock_guard lock (_mutex);
        if (handler_started)
            _active_payload_bytes =
              payload_bytes <= _active_payload_bytes
                ? _active_payload_bytes - payload_bytes
                : 0;
        _cumulative_completed_payload_bytes += payload_bytes;
        if (_application_hwm_bytes != 0
            && _application_receive_paused
            && pending_locked () < _application_hwm_bytes)
            _application_receive_paused = false;
        _capacity_changed.notify_all ();
    }

    inbound_dispatch_snapshot_t snapshot () const noexcept
    {
        std::lock_guard lock (_mutex);
        const auto pending = pending_locked ();
        const auto active = std::min (_active_payload_bytes, pending);
        return {
          _application_hwm_bytes,
          pending,
          pending - active,
          active,
          _application_receive_paused};
    }

  private:
    bool can_receive_locked () const noexcept
    {
        return _application_hwm_bytes == 0
               || pending_locked () < _application_hwm_bytes;
    }

    std::uint64_t pending_locked () const noexcept
    {
        return _cumulative_received_payload_bytes >= _cumulative_completed_payload_bytes
                 ? _cumulative_received_payload_bytes - _cumulative_completed_payload_bytes
                 : 0;
    }

    mutable std::mutex _mutex;
    std::condition_variable _capacity_changed;
    std::uint64_t _application_hwm_bytes = 0;
    std::uint64_t _cumulative_received_payload_bytes = 0;
    std::uint64_t _cumulative_completed_payload_bytes = 0;
    std::uint64_t _active_payload_bytes = 0;
    bool _application_receive_paused = false;
};

} // namespace zlink::framework::runtime
