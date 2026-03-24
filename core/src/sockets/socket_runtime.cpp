/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/socket_runtime.hpp"

#include "core/io_thread.hpp"
#include "core/mailbox.hpp"
#include "utils/err.hpp"
#include "utils/macros.hpp"

namespace
{
const uint32_t public_api_closing_bit = 0x80000000u;
const uint32_t public_api_inflight_mask = ~public_api_closing_bit;
}

bool zlink::socket_lifecycle_coordinator_t::enter_public_api ()
{
    const uint32_t old =
      public_api_state.fetch_add (1, std::memory_order_acq_rel);
    if ((old & public_api_closing_bit) == 0)
        return true;

    const uint32_t reverted =
      public_api_state.fetch_sub (1, std::memory_order_acq_rel);
    zlink_assert ((reverted & public_api_inflight_mask) > 0);
    errno = ESHUTDOWN;
    return false;
}

void zlink::socket_lifecycle_coordinator_t::leave_public_api ()
{
    const uint32_t old =
      public_api_state.fetch_sub (1, std::memory_order_acq_rel);
    zlink_assert ((old & public_api_inflight_mask) > 0);
}

bool zlink::socket_lifecycle_coordinator_t::enter_callback_api ()
{
    if (!enter_public_api ())
        return false;

    callback_api_depth.fetch_add (1, std::memory_order_acq_rel);
    return true;
}

bool zlink::socket_lifecycle_coordinator_t::leave_callback_api ()
{
    const uint32_t depth =
      callback_api_depth.fetch_sub (1, std::memory_order_acq_rel) - 1;
    leave_public_api ();
    return depth == 0
           && close_deferred.load (std::memory_order_acquire)
           && public_close_requested ();
}

bool zlink::socket_lifecycle_coordinator_t::begin_close_or_fail_busy (
  bool from_self_callback_)
{
    uint32_t old = public_api_state.load (std::memory_order_acquire);
    while (true) {
        if ((old & public_api_closing_bit) != 0) {
            errno = EALREADY;
            return false;
        }

        if (!from_self_callback_ && (old & public_api_inflight_mask) != 0) {
            errno = EBUSY;
            return false;
        }

        const uint32_t desired = old | public_api_closing_bit;
        if (public_api_state.compare_exchange_weak (
              old, desired, std::memory_order_acq_rel,
              std::memory_order_acquire)) {
            if (from_self_callback_)
                close_deferred.store (true, std::memory_order_release);
            return true;
        }
    }
}

bool zlink::socket_lifecycle_coordinator_t::public_close_requested () const
{
    return (public_api_state.load (std::memory_order_acquire)
            & public_api_closing_bit)
           != 0;
}

void zlink::socket_lifecycle_coordinator_t::lock_public_api_sync ()
{
    bool expected = false;
    while (!public_api_sync.compare_exchange_weak (
      expected, true, std::memory_order_acquire, std::memory_order_relaxed)) {
        expected = false;
    }
}

void zlink::socket_lifecycle_coordinator_t::unlock_public_api_sync ()
{
    public_api_sync.store (false, std::memory_order_release);
}

int zlink::socket_lifecycle_coordinator_t::start_async_mailbox_processing (
  mailbox_t *mailbox_,
  io_thread_t *io_thread_,
  mailbox_t::mailbox_handler_t handler_,
  void *handler_arg_,
  mailbox_t::mailbox_pre_post_t pre_post_)
{
    if (!mailbox_ || !io_thread_) {
        errno = EINVAL;
        return -1;
    }

    async_mailbox_active.store (true, std::memory_order_release);
    mailbox_->set_io_context (&io_thread_->get_io_context (), handler_,
                              handler_arg_, pre_post_);
    mailbox_->schedule_if_needed ();
    return 0;
}

void zlink::socket_lifecycle_coordinator_t::stop_async_mailbox_processing (
  mailbox_t *mailbox_)
{
    async_mailbox_active.store (false, std::memory_order_release);
    async_processing_done.store (false, std::memory_order_release);
    async_quiesce_pending.store (true, std::memory_order_release);
    if (mailbox_)
        mailbox_->schedule_if_needed ();
}

void zlink::socket_lifecycle_coordinator_t::mark_async_processing_stopped (
  mailbox_t *mailbox_)
{
    if (mailbox_)
        mailbox_->set_io_context (NULL, NULL, NULL, NULL);

    if (async_quiesce_pending.load (std::memory_order_acquire)) {
        async_quiesce_pending.store (false, std::memory_order_release);
        async_processing_done.store (true, std::memory_order_release);
        scoped_lock_t lock (async_done_mu);
        async_done_cv.broadcast ();
    }
}

void zlink::socket_lifecycle_coordinator_t::wait_async_quiesced (
  int timeout_ms_)
{
    if (async_processing_done.load (std::memory_order_acquire))
        return;

    scoped_lock_t lock (async_done_mu);
    while (!async_processing_done.load (std::memory_order_acquire)) {
        const int rc =
          async_done_cv.wait (&async_done_mu, timeout_ms_ > 0 ? timeout_ms_
                                                              : 2000);
        if (rc != 0)
            break;
    }
}

bool zlink::socket_lifecycle_coordinator_t::is_async_mailbox_active () const
{
    return async_mailbox_active.load (std::memory_order_acquire);
}

bool zlink::socket_lifecycle_coordinator_t::is_async_quiesce_pending () const
{
    return async_quiesce_pending.load (std::memory_order_acquire);
}

void zlink::socket_lifecycle_coordinator_t::clear_deferred_close ()
{
    close_deferred.store (false, std::memory_order_release);
}

void zlink::socket_lifecycle_coordinator_t::set_monitor_async_mailbox_owned (
  bool owned_)
{
    monitor_async_mailbox_owned = owned_;
}

bool zlink::socket_lifecycle_coordinator_t::is_monitor_async_mailbox_owned () const
{
    return monitor_async_mailbox_owned;
}

void zlink::socket_lifecycle_coordinator_t::mark_destroy_pending ()
{
    destroy_pending = true;
}

void zlink::socket_lifecycle_coordinator_t::clear_destroy_pending ()
{
    destroy_pending = false;
}

bool zlink::socket_lifecycle_coordinator_t::is_destroy_pending () const
{
    return destroy_pending;
}

int zlink::socket_lifecycle_coordinator_t::mailbox_refcount ()
{
    return mailbox_refcnt.add (0);
}

void zlink::socket_lifecycle_coordinator_t::inc_mailbox_ref ()
{
    mailbox_refcnt.add (1);
}

bool zlink::socket_lifecycle_coordinator_t::dec_mailbox_ref ()
{
    return mailbox_refcnt.sub (1) != 0;
}

void zlink::socket_inprocs_t::emplace (const char *endpoint_uri_, pipe_t *pipe_)
{
    _inprocs.ZLINK_MAP_INSERT_OR_EMPLACE (std::string (endpoint_uri_), pipe_);
}

int zlink::socket_inprocs_t::erase_pipes (
  const std::string &endpoint_uri_str_)
{
    const std::pair<map_t::iterator, map_t::iterator> range =
      _inprocs.equal_range (endpoint_uri_str_);
    if (range.first == range.second) {
        errno = ENOENT;
        return -1;
    }

    for (map_t::iterator it = range.first; it != range.second; ++it) {
        it->second->send_disconnect_msg ();
        // Explicit endpoint disconnect should not defer pipe teardown.
        // The non-inproc term_endpoint path also uses terminate(false).
        it->second->terminate (false);
    }
    _inprocs.erase (range.first, range.second);
    return 0;
}

void zlink::socket_inprocs_t::erase_pipe (const pipe_t *pipe_)
{
    for (map_t::iterator it = _inprocs.begin (), end = _inprocs.end ();
         it != end; ++it)
        if (it->second == pipe_) {
            _inprocs.erase (it);
            break;
        }
}
