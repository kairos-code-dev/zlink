/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/common/socket_runtime.hpp"

#include "core/io_thread.hpp"
#include "core/mailbox.hpp"
#include "sockets/common/socket_base.hpp"
#include "utils/config.hpp"
#include "utils/err.hpp"
#include "utils/macros.hpp"

namespace
{
const uint32_t public_api_closing_bit = 0x80000000u;
const uint32_t public_api_sync_bit = 0x40000000u;
const uint32_t public_api_inflight_mask = ~(public_api_closing_bit | public_api_sync_bit);
zlink::socket_base_t *&send_ready_dispatch_socket_tls ()
{
    static thread_local zlink::socket_base_t *socket = NULL;
    return socket;
}

std::string make_monitor_ready_key (const zlink::endpoint_uri_pair_t &endpoint_uri_pair_,
                                    const unsigned char *routing_id_,
                                    size_t routing_id_size_)
{
    std::string key = endpoint_uri_pair_.identifier ();
    if (key.empty ())
        key = endpoint_uri_pair_.remote;
    key.push_back ('\0');
    if (routing_id_ && routing_id_size_ > 0)
        key.append (reinterpret_cast<const char *> (routing_id_), routing_id_size_);
    return key;
}

std::string
make_monitor_ready_endpoint_prefix (const zlink::endpoint_uri_pair_t &endpoint_uri_pair_)
{
    return make_monitor_ready_key (endpoint_uri_pair_, NULL, 0);
}
}

uint32_t zlink::socket_monitor_runtime_t::ready_count () const
{
    return static_cast<uint32_t> (ready_connections.size ());
}

bool zlink::socket_monitor_runtime_t::mark_ready_connection (
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  const unsigned char *routing_id_,
  size_t routing_id_size_,
  uint32_t *ready_count_out_)
{
    const bool inserted =
      ready_connections
        .insert (make_monitor_ready_key (endpoint_uri_pair_, routing_id_, routing_id_size_))
        .second;
    if (inserted && ready_count_out_)
        *ready_count_out_ = ready_count ();
    return inserted;
}

bool zlink::socket_monitor_runtime_t::erase_ready_connection (
  const endpoint_uri_pair_t &endpoint_uri_pair_,
  const unsigned char *routing_id_,
  size_t routing_id_size_,
  uint32_t *ready_count_out_)
{
    const bool erased = ready_connections.erase (make_monitor_ready_key (
                          endpoint_uri_pair_, routing_id_, routing_id_size_))
                        != 0;
    if (erased && ready_count_out_)
        *ready_count_out_ = ready_count ();
    return erased;
}

bool zlink::socket_monitor_runtime_t::erase_ready_connection_for_endpoint (
  const endpoint_uri_pair_t &endpoint_uri_pair_, uint32_t *ready_count_out_)
{
    const std::string prefix = make_monitor_ready_endpoint_prefix (endpoint_uri_pair_);
    for (std::set<std::string>::iterator it = ready_connections.begin ();
         it != ready_connections.end (); ++it) {
        if (it->compare (0, prefix.size (), prefix) != 0)
            continue;

        ready_connections.erase (it);
        if (ready_count_out_)
            *ready_count_out_ = ready_count ();
        return true;
    }

    return false;
}

void zlink::socket_monitor_runtime_t::reset_worker_state ()
{
    queue_sync.lock ();
    queue.clear ();
    queue_stop = false;
    task_running = false;
    queue_sync.unlock ();
}

void zlink::socket_monitor_runtime_t::start_task (uint64_t task_id_)
{
    queue_sync.lock ();
    task_id = task_id_;
    task_running = task_id_ != 0;
    queue_sync.unlock ();
}

bool zlink::socket_monitor_runtime_t::dequeue_worker_event_nowait (
  socket_monitor_event_record_t *out_)
{
    if (!out_)
        return false;

    queue_sync.lock ();
    if (queue_stop || queue.empty ()) {
        queue_sync.unlock ();
        return false;
    }

    *out_ = queue.front ();
    queue.pop_front ();
    queue_sync.unlock ();
    return true;
}

void zlink::socket_monitor_runtime_t::requeue_worker_event_front (
  const socket_monitor_event_record_t &record_)
{
    queue_sync.lock ();
    if (!queue_stop) {
        queue.push_front (record_);
        queue_cv.broadcast ();
    }
    queue_sync.unlock ();
}

void zlink::socket_monitor_runtime_t::enqueue_worker_event (
  const socket_monitor_event_record_t &record_, size_t hwm_)
{
    queue_sync.lock ();
    if (!queue_stop && (queue.size () < hwm_ || !lossy)) {
        queue.push_back (record_);
        queue_cv.broadcast ();
    }
    queue_sync.unlock ();
}

void zlink::socket_monitor_runtime_t::stop_task ()
{
    queue_sync.lock ();
    queue_stop = true;
    queue.clear ();
    queue_cv.broadcast ();
    task_running = false;
    task_id = 0;
    queue_sync.unlock ();
}

void zlink::socket_endpoint_runtime_t::attach_pipe (pipe_t *pipe_)
{
    attached_pipes.push_back (pipe_);
}

void zlink::socket_endpoint_runtime_t::detach_pipe (pipe_t *pipe_)
{
    attached_pipes.erase (pipe_);
}

size_t zlink::socket_endpoint_runtime_t::attached_pipe_count () const
{
    return attached_pipes.size ();
}

bool zlink::socket_endpoint_runtime_t::has_attached_pipes () const
{
    return !attached_pipes.empty ();
}

zlink::pipe_t *zlink::socket_endpoint_runtime_t::attached_pipe (size_t index_)
{
    return attached_pipes[index_];
}

const zlink::pipe_t *zlink::socket_endpoint_runtime_t::attached_pipe (size_t index_) const
{
    return attached_pipes[index_];
}

void zlink::socket_endpoint_runtime_t::store_last_recv_source_rid (
  const zlink_routing_id_t *source_rid_)
{
    if (!source_rid_) {
        clear_last_recv_source_rid ();
        return;
    }

    last_recv_source_rid = *source_rid_;
    last_recv_source_rid_valid = true;
}

void zlink::socket_endpoint_runtime_t::clear_last_recv_source_rid ()
{
    memset (&last_recv_source_rid, 0, sizeof (last_recv_source_rid));
    last_recv_source_rid_valid = false;
}

bool zlink::socket_endpoint_runtime_t::copy_last_recv_source_rid (zlink_routing_id_t *out_) const
{
    if (out_)
        memset (out_, 0, sizeof (*out_));

    if (!last_recv_source_rid_valid || !out_)
        return false;

    *out_ = last_recv_source_rid;
    return true;
}

void zlink::socket_endpoint_runtime_t::set_last_endpoint (const std::string &endpoint_)
{
    last_endpoint = endpoint_;
}

const std::string &zlink::socket_endpoint_runtime_t::last_endpoint_uri () const
{
    return last_endpoint;
}

bool zlink::socket_command_runtime_t::should_skip_throttled_command_poll (uint64_t tsc_)
{
    if (!tsc_)
        return false;

    if (tsc_ >= last_command_tsc && tsc_ - last_command_tsc <= max_command_delay)
        return true;

    last_command_tsc = tsc_;
    return false;
}

bool zlink::socket_command_runtime_t::should_poll_commands_after_recv (int inbound_poll_rate_)
{
    return ++recv_ticks == inbound_poll_rate_;
}

void zlink::socket_command_runtime_t::reset_recv_ticks ()
{
    recv_ticks = 0;
}

bool zlink::socket_command_runtime_t::should_block_on_recv () const
{
    return recv_ticks != 0;
}

bool zlink::socket_dispatch_bridge_t::load_send_ready_handler (
  zlink_send_ready_handler_fn *handler_out_, void **subject_out_, void **userdata_out_) const
{
    if (!handler_out_ || !subject_out_ || !userdata_out_)
        return false;

    while (true) {
        const uint32_t s1 = send_ready_seq.load (std::memory_order_acquire);
        if ((s1 & 1u) != 0)
            continue;

        zlink_send_ready_handler_fn handler = send_ready_handler.load (std::memory_order_acquire);
        void *subject = send_ready_handler_subject.load (std::memory_order_acquire);
        void *userdata = send_ready_handler_userdata.load (std::memory_order_acquire);
        const uint32_t s2 = send_ready_seq.load (std::memory_order_acquire);
        if (s1 != s2 || (s2 & 1u) != 0)
            continue;

        *handler_out_ = handler;
        *subject_out_ = subject;
        *userdata_out_ = userdata;
        return handler != NULL;
    }
}

void zlink::socket_dispatch_bridge_t::store_send_ready_handler (
  zlink_send_ready_handler_fn handler_, void *subject_, void *userdata_)
{
    scoped_lock_t writer_lock (send_ready_writer_sync);
    send_ready_seq.fetch_add (1, std::memory_order_acq_rel);
    send_ready_handler.store (handler_, std::memory_order_release);
    send_ready_handler_subject.store (subject_, std::memory_order_release);
    send_ready_handler_userdata.store (userdata_, std::memory_order_release);
    send_ready_seq.fetch_add (1, std::memory_order_acq_rel);
}

bool zlink::socket_dispatch_bridge_t::arm_send_ready_notification ()
{
    zlink_send_ready_handler_fn handler = NULL;
    void *subject = NULL;
    void *userdata = NULL;
    if (!load_send_ready_handler (&handler, &subject, &userdata))
        return false;

    send_ready_armed.store (true, std::memory_order_release);
    return true;
}

bool zlink::socket_dispatch_bridge_t::consume_send_ready_notification ()
{
    bool expected = true;
    return send_ready_armed.compare_exchange_strong (expected, false, std::memory_order_acq_rel,
                                                     std::memory_order_acquire);
}

void zlink::socket_dispatch_bridge_t::mark_send_recovery_pending ()
{
    send_ready_recovery_pending.store (true, std::memory_order_release);
    send_ready_recovery_ready.store (false, std::memory_order_release);
}

void zlink::socket_dispatch_bridge_t::clear_send_recovery_pending ()
{
    send_ready_recovery_pending.store (false, std::memory_order_release);
    send_ready_recovery_ready.store (false, std::memory_order_release);
}

void zlink::socket_dispatch_bridge_t::mark_send_recovery_ready ()
{
    if (!send_ready_recovery_pending.load (std::memory_order_acquire))
        return;
    send_ready_recovery_ready.store (true, std::memory_order_release);
}

void zlink::socket_dispatch_bridge_t::clear_send_recovery_ready ()
{
    send_ready_recovery_ready.store (false, std::memory_order_release);
}

bool zlink::socket_dispatch_bridge_t::send_recovery_pending () const
{
    return send_ready_recovery_pending.load (std::memory_order_acquire);
}

bool zlink::socket_dispatch_bridge_t::send_recovery_ready () const
{
    return send_ready_recovery_ready.load (std::memory_order_acquire);
}

bool zlink::socket_lifecycle_coordinator_t::enter_public_api ()
{
    const uint32_t old = public_api_state.fetch_add (1, std::memory_order_acq_rel);
    if ((old & public_api_closing_bit) == 0)
        return true;

    const uint32_t reverted = public_api_state.fetch_sub (1, std::memory_order_acq_rel);
    zlink_assert ((reverted & public_api_inflight_mask) > 0);
    errno = ESHUTDOWN;
    return false;
}

bool zlink::socket_lifecycle_coordinator_t::enter_public_api_and_lock_sync ()
{
    uint32_t expected = 0;
    if (public_api_state.compare_exchange_strong (expected, 1u | public_api_sync_bit,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire)) {
        return true;
    }

    if (!enter_public_api ())
        return false;

    lock_public_api_sync ();
    return true;
}

void zlink::socket_lifecycle_coordinator_t::leave_public_api ()
{
    const uint32_t old = public_api_state.fetch_sub (1, std::memory_order_acq_rel);
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
    const uint32_t depth = callback_api_depth.fetch_sub (1, std::memory_order_acq_rel) - 1;
    leave_public_api ();
    return depth == 0 && close_deferred.load (std::memory_order_acquire)
           && public_close_requested ();
}

bool zlink::socket_lifecycle_coordinator_t::begin_close_or_fail_busy (bool from_self_callback_)
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
        if (public_api_state.compare_exchange_weak (old, desired, std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
            if (from_self_callback_)
                close_deferred.store (true, std::memory_order_release);
            return true;
        }
    }
}

bool zlink::socket_lifecycle_coordinator_t::public_close_requested () const
{
    return (public_api_state.load (std::memory_order_acquire) & public_api_closing_bit) != 0;
}

bool zlink::socket_lifecycle_coordinator_t::public_api_sync_held () const
{
    return (public_api_state.load (std::memory_order_acquire) & public_api_sync_bit) != 0;
}

void zlink::socket_lifecycle_coordinator_t::lock_public_api_sync ()
{
    uint32_t old = public_api_state.load (std::memory_order_acquire);
    while (true) {
        if ((old & public_api_sync_bit) == 0) {
            const uint32_t desired = old | public_api_sync_bit;
            if (public_api_state.compare_exchange_weak (old, desired, std::memory_order_acquire,
                                                        std::memory_order_acquire)) {
                return;
            }
            continue;
        }

        old = public_api_state.load (std::memory_order_acquire);
    }
}

void zlink::socket_lifecycle_coordinator_t::unlock_public_api_sync ()
{
    const uint32_t old =
      public_api_state.fetch_and (~public_api_sync_bit, std::memory_order_release);
    zlink_assert ((old & public_api_sync_bit) != 0);
}

void zlink::socket_lifecycle_coordinator_t::unlock_public_api_sync_and_leave ()
{
    const uint32_t old =
      public_api_state.fetch_sub (public_api_sync_bit | 1u, std::memory_order_acq_rel);
    zlink_assert ((old & public_api_sync_bit) != 0);
    zlink_assert ((old & public_api_inflight_mask) > 0);
}

zlink::socket_callback_scope_t::socket_callback_scope_t (
  socket_base_t *socket_, socket_lifecycle_coordinator_t &coordinator_) :
    _socket (socket_), _coordinator (&coordinator_), _entered (coordinator_.enter_callback_api ())
{
}

zlink::socket_callback_scope_t::~socket_callback_scope_t ()
{
    if (!_entered)
        return;

    if (_coordinator->leave_callback_api () && _socket)
        _socket->finish_close_handoff ();
}

zlink::socket_public_send_scope_t::socket_public_send_scope_t (
  socket_lifecycle_coordinator_t &coordinator_, bool needs_sync_) :
    _coordinator (&coordinator_), _entered (false), _needs_sync (needs_sync_), _sync_locked (false)
{
    if (_needs_sync) {
        _entered = _coordinator->enter_public_api_and_lock_sync ();
        _sync_locked = _entered;
        return;
    }

    _entered = _coordinator->enter_public_api ();
}

zlink::socket_public_send_scope_t::~socket_public_send_scope_t ()
{
    if (!_entered)
        return;

    if (_sync_locked)
        _coordinator->unlock_public_api_sync_and_leave ();
    else
        _coordinator->leave_public_api ();
}

bool zlink::socket_public_send_scope_t::should_hold_sync_during_retry (
  bool send_ready_handler_active_) const
{
    return _sync_locked && !send_ready_handler_active_;
}

void zlink::socket_public_send_scope_t::release_sync_for_retry ()
{
    unlock_sync ();
}

void zlink::socket_public_send_scope_t::reacquire_sync_after_retry ()
{
    relock_sync ();
}

void zlink::socket_public_send_scope_t::unlock_sync ()
{
    if (!_entered || !_needs_sync || !_sync_locked)
        return;

    _coordinator->unlock_public_api_sync ();
    _sync_locked = false;
}

void zlink::socket_public_send_scope_t::relock_sync ()
{
    if (!_entered || !_needs_sync || _sync_locked)
        return;

    _coordinator->lock_public_api_sync ();
    _sync_locked = true;
}

zlink::socket_send_ready_dispatch_scope_t::socket_send_ready_dispatch_scope_t (
  socket_base_t *socket_) :
    _previous (send_ready_dispatch_socket_tls ())
{
    send_ready_dispatch_socket_tls () = socket_;
}

zlink::socket_send_ready_dispatch_scope_t::~socket_send_ready_dispatch_scope_t ()
{
    send_ready_dispatch_socket_tls () = _previous;
}

zlink::socket_base_t *zlink::socket_send_ready_dispatch_scope_t::current_socket ()
{
    return send_ready_dispatch_socket_tls ();
}

bool zlink::socket_send_ready_dispatch_scope_t::dispatching_socket (const socket_base_t *socket_)
{
    return send_ready_dispatch_socket_tls () == socket_;
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
    async_processing_started.store (false, std::memory_order_release);
    mailbox_->set_io_context (&io_thread_->get_io_context (), handler_, handler_arg_, pre_post_);
    mailbox_->schedule_if_needed ();
    return 0;
}

void zlink::socket_lifecycle_coordinator_t::mark_async_processing_started ()
{
    async_processing_started.store (true, std::memory_order_release);
    scoped_lock_t lock (async_done_mu);
    async_done_cv.broadcast ();
}

void zlink::socket_lifecycle_coordinator_t::wait_async_started (int timeout_ms_)
{
    if (async_processing_started.load (std::memory_order_acquire))
        return;

    scoped_lock_t lock (async_done_mu);
    while (!async_processing_started.load (std::memory_order_acquire)) {
        const int rc = async_done_cv.wait (&async_done_mu, timeout_ms_ > 0 ? timeout_ms_ : 2000);
        if (rc != 0)
            break;
    }
}

void zlink::socket_lifecycle_coordinator_t::stop_async_mailbox_processing (mailbox_t *mailbox_)
{
    async_mailbox_active.store (false, std::memory_order_release);
    async_processing_done.store (false, std::memory_order_release);
    async_quiesce_pending.store (true, std::memory_order_release);
    if (mailbox_)
        mailbox_->schedule_if_needed ();
}

void zlink::socket_lifecycle_coordinator_t::mark_async_processing_stopped (mailbox_t *mailbox_)
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

void zlink::socket_lifecycle_coordinator_t::wait_async_quiesced (int timeout_ms_)
{
    if (async_processing_done.load (std::memory_order_acquire))
        return;

    scoped_lock_t lock (async_done_mu);
    while (!async_processing_done.load (std::memory_order_acquire)) {
        const int rc = async_done_cv.wait (&async_done_mu, timeout_ms_ > 0 ? timeout_ms_ : 2000);
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

void zlink::socket_lifecycle_coordinator_t::complete_deferred_close_handoff (mailbox_t *mailbox_,
                                                                             int timeout_ms_)
{
    clear_deferred_close ();

    if (is_async_mailbox_active ()) {
        stop_async_mailbox_processing (mailbox_);
        wait_async_quiesced (timeout_ms_);
    } else if (is_async_quiesce_pending ()) {
        wait_async_quiesced (timeout_ms_);
    }

    if (mailbox_)
        mailbox_->clear_signalers ();
}

void zlink::socket_lifecycle_coordinator_t::clear_deferred_close ()
{
    close_deferred.store (false, std::memory_order_release);
}

void zlink::socket_lifecycle_coordinator_t::set_monitor_async_mailbox_owned (bool owned_)
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

void zlink::socket_lifecycle_coordinator_t::set_reaper_poller (poller_t *poller_)
{
    reaper_poller_value = poller_;
}

zlink::poller_t *zlink::socket_lifecycle_coordinator_t::reaper_poller () const
{
    return reaper_poller_value;
}

void zlink::socket_lifecycle_coordinator_t::mark_destroyed ()
{
    destroyed = true;
}

bool zlink::socket_lifecycle_coordinator_t::is_destroyed () const
{
    return destroyed;
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

int zlink::socket_inprocs_t::erase_pipes (const std::string &endpoint_uri_str_)
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
    for (map_t::iterator it = _inprocs.begin (), end = _inprocs.end (); it != end; ++it)
        if (it->second == pipe_) {
            _inprocs.erase (it);
            break;
        }
}
