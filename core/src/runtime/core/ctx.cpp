/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "utils/macros.hpp"
#include "utils/debug_log.hpp"
#ifndef ZLINK_HAVE_WINDOWS
#include <unistd.h>
#endif

#include <limits>
#include <climits>
#include <new>
#include <stdio.h>
#include <string.h>

#include "core/ctx.hpp"
#include "core/auto_hwm_policy.hpp"
#include "core/ctx_bootstrap.hpp"
#include "core/ctx_termination.hpp"
#include "sockets/common/socket_base.hpp"
#include "core/io_thread.hpp"
#include "core/reaper.hpp"
#include "core/pipe.hpp"
#include "services/control/service_control_runtime.hpp"
#include "utils/err.hpp"
#include "utils/heap_owner.hpp"
#include "utils/random.hpp"

#define ZLINK_CTX_TAG_VALUE_GOOD 0xabadcafe
#define ZLINK_CTX_TAG_VALUE_BAD 0xdeadbeef

int zlink::ctx_t::clipped_maxsocket (int max_requested_)
{
    if (max_requested_ >= zlink::poller_t::max_fds () && zlink::poller_t::max_fds () != -1)
        // -1 because we need room for the reaper mailbox.
        max_requested_ = zlink::poller_t::max_fds () - 1;

    return max_requested_;
}

namespace
{
const char *socket_type_name (int type_)
{
    switch (type_) {
        case ZLINK_CORE_SOCKET_PAIR:
            return "PAIR";
        case ZLINK_CORE_SOCKET_PUB:
            return "PUB";
        case ZLINK_CORE_SOCKET_SUB:
            return "SUB";
        case ZLINK_CORE_SOCKET_DEALER:
            return "DEALER";
        case ZLINK_CORE_SOCKET_ROUTER:
            return "ROUTER";
        case ZLINK_CORE_SOCKET_STREAM:
            return "STREAM";
        case ZLINK_CORE_SOCKET_XPUB:
            return "XPUB";
        case ZLINK_CORE_SOCKET_XSUB:
            return "XSUB";
        default:
            return "UNKNOWN";
    }
}

}

zlink::ctx_t::ctx_t () :
    _tag (ZLINK_CTX_TAG_VALUE_GOOD),
    _starting (true),
    _terminating (false),
    _max_sockets (ctx_t::clipped_maxsocket (ZLINK_MAX_SOCKETS_DFLT)),
    _max_msgsz (INT_MAX),
    _io_thread_count (ZLINK_IO_THREADS_DFLT),
    _auto_hwm_enabled (ZLINK_CTX_AUTO_HWM_ENABLE_DFLT != 0),
    _auto_hwm_recalc_debounce_ms (ZLINK_CTX_AUTO_HWM_RECALC_DEBOUNCE_MS_DFLT),
    _auto_hwm_profile (ZLINK_CTX_AUTO_HWM_PROFILE_DFLT),
    _auto_hwm_msg_unit_bytes (ZLINK_CTX_AUTO_HWM_MSG_UNIT_BYTES_DFLT),
    _auto_hwm_recalc_pending (false),
    _auto_hwm_last_change_ms (0),
    _auto_hwm_recalc_deadline_ms (0),
    _auto_hwm_pending_generation (0),
    _auto_hwm_last_applied_generation (0),
    _auto_hwm_recalc_task_id (0),
    _blocky (true),
    _ipv6 (false)
{
#ifdef HAVE_FORK
    _pid = getpid ();
#endif

    //  Initialise crypto library, if needed.
    zlink::random_open ();
}

bool zlink::ctx_t::check_tag () const
{
    return _tag == ZLINK_CTX_TAG_VALUE_GOOD;
}

zlink::ctx_t::~ctx_t ()
{
    //  Check that there are no remaining _sockets.
    zlink_assert (_socket_registry.empty ());
    stop_auto_hwm_recalc_task ();
    ctx_termination_t::teardown_runtime (*this);

    //  De-initialise crypto library, if needed.
    zlink::random_close ();

    //  Remove the tag, so that the object is considered dead.
    _tag = ZLINK_CTX_TAG_VALUE_BAD;
}

bool zlink::ctx_t::valid () const
{
    return _term_mailbox.valid ();
}

zlink::service_control_runtime_t *zlink::ctx_t::service_control_runtime ()
{
    return ctx_bootstrap_t::ensure_service_runtime (*this);
}

void zlink::ctx_t::auto_hwm_recalc_task_main (void *arg_)
{
    static_cast<ctx_t *> (arg_)->auto_hwm_recalc_task ();
}

void zlink::ctx_t::ensure_auto_hwm_recalc_task_started ()
{
    if (_auto_hwm_recalc_task_id != 0)
        return;

    service_control_runtime_t *runtime = service_control_runtime ();
    if (!runtime)
        return;

    _auto_hwm_recalc_task_id =
      runtime->add_periodic_task (&ctx_t::auto_hwm_recalc_task_main, this, 10, false);
}

void zlink::ctx_t::stop_auto_hwm_recalc_task ()
{
    const uint64_t task_id = _auto_hwm_recalc_task_id;
    _auto_hwm_recalc_task_id = 0;
    if (task_id == 0)
        return;

    service_control_runtime_t *runtime = _runtime_resources.service_control_runtime ();
    if (runtime)
        (void) runtime->remove_task (task_id);
}

void zlink::ctx_t::schedule_auto_hwm_recalculate ()
{
    const uint64_t now_ms = zlink::clock_t ().now_ms ();
    int debounce_ms = 0;
    {
        scoped_lock_t locker (_opt_sync);
        debounce_ms = _auto_hwm_recalc_debounce_ms;
    }

    {
        scoped_lock_t lock (_slot_sync);
        _auto_hwm_last_change_ms = now_ms;
        ++_auto_hwm_pending_generation;

        if (debounce_ms <= 0) {
            _auto_hwm_recalc_pending = false;
            _auto_hwm_recalc_deadline_ms = now_ms;
        } else {
            _auto_hwm_recalc_pending = true;
            _auto_hwm_recalc_deadline_ms = now_ms + static_cast<uint64_t> (debounce_ms);
            ensure_auto_hwm_recalc_task_started ();
            if (_auto_hwm_recalc_task_id != 0) {
                service_control_runtime_t *runtime = _runtime_resources.service_control_runtime ();
                if (runtime)
                    (void) runtime->wakeup_task (_auto_hwm_recalc_task_id);
            }
        }
    }

    if (debounce_ms <= 0)
        (void) auto_hwm_recalculate_now ();
}

int zlink::ctx_t::auto_hwm_recalculate_now ()
{
    bool enabled = false;
    zlink_auto_hwm_profile_t profile = ZLINK_CTX_AUTO_HWM_PROFILE_DFLT;
    int message_unit_bytes = ZLINK_CTX_AUTO_HWM_MSG_UNIT_BYTES_DFLT;
    {
        scoped_lock_t locker (_opt_sync);
        enabled = _auto_hwm_enabled;
        profile = _auto_hwm_profile;
        message_unit_bytes = _auto_hwm_msg_unit_bytes;
    }

    scoped_lock_t runtime_lock (_slot_sync);
    _auto_hwm_recalc_pending = false;
    _auto_hwm_recalc_deadline_ms = 0;
    _auto_hwm_last_applied_generation = _auto_hwm_pending_generation;

    if (!enabled)
        return 0;

    std::vector<socket_base_t *> sockets;
    _socket_registry.collect_sockets (&sockets);
    if (sockets.empty ())
        return 0;

    auto_hwm_context_plan_t context_plan;
    auto_hwm_context_plan_make (enabled, profile, &context_plan, message_unit_bytes);

    std::vector<auto_hwm_socket_plan_t> plans;
    plans.reserve (sockets.size ());
    for (size_t i = 0; i < sockets.size (); ++i) {
        socket_base_t *socket = sockets[i];
        if (!socket) {
            plans.push_back (auto_hwm_socket_plan_t ());
            continue;
        }

        auto_hwm_socket_plan_t plan = socket->prepare_auto_hwm_socket_plan (context_plan);
        if (!socket->auto_hwm_policy_enabled ())
            plan.socket_message_slots = 0;
        plans.push_back (plan);
    }

    if (!plans.empty ())
        auto_hwm_context_finalize (&context_plan, &plans[0], plans.size ());

    for (size_t i = 0; i < sockets.size (); ++i) {
        if (!sockets[i])
            continue;
        sockets[i]->apply_auto_hwm_socket_plan (context_plan, plans[i], false,
                                                ZLINK_AUTO_HWM_RECALC_REASON_REFRESH);
    }
    return 0;
}

void zlink::ctx_t::auto_hwm_recalc_task ()
{
    bool should_run = false;
    {
        scoped_lock_t lock (_slot_sync);
        if (_auto_hwm_recalc_pending && zlink::clock_t ().now_ms () >= _auto_hwm_recalc_deadline_ms
            && _auto_hwm_pending_generation != _auto_hwm_last_applied_generation)
            should_run = true;
    }

    if (should_run)
        (void) auto_hwm_recalculate_now ();
}

zlink_auto_hwm_profile_t zlink::ctx_t::auto_hwm_profile () const
{
    scoped_lock_t locker (const_cast<mutex_t &> (_opt_sync));
    return _auto_hwm_profile;
}

bool zlink::ctx_t::auto_hwm_enabled () const
{
    scoped_lock_t locker (const_cast<mutex_t &> (_opt_sync));
    return _auto_hwm_enabled;
}

int zlink::ctx_t::auto_hwm_msg_unit_bytes () const
{
    scoped_lock_t locker (const_cast<mutex_t &> (_opt_sync));
    return _auto_hwm_msg_unit_bytes;
}

void zlink::ctx_t::debug_dump_sockets_locked (const char *phase_) const
{
    if (!debug_env_enabled ("ZLINK_CTX_DEBUG_SOCKETS"))
        return;

    std::vector<socket_base_t *> sockets;
    _socket_registry.collect_sockets (&sockets);
    fprintf (stderr, "[ctx] %s socket_count=%u\n", phase_ ? phase_ : "state",
             static_cast<unsigned> (sockets.size ()));
    for (std::vector<socket_base_t *>::size_type i = 0, size = sockets.size (); i != size; ++i) {
        const socket_base_t *socket = sockets[i];
        if (!socket)
            continue;

        fprintf (stderr, "[ctx]   socket[%u]=%p type=%s(%d) sid=%d\n", static_cast<unsigned> (i),
                 static_cast<const void *> (socket), socket_type_name (socket->socket_type ()),
                 socket->socket_type (), socket->socket_id ());
    }
    fflush (stderr);
}

int zlink::ctx_t::terminate ()
{
    _slot_sync.lock ();
    ctx_termination_t::flush_pending_inproc_locked (*this);

    if (ctx_termination_t::begin_shutdown_locked (*this, true)) {
        _slot_sync.unlock ();
        if (ctx_termination_t::wait_for_reaper_done (*this) == -1)
            return -1;
        _slot_sync.lock ();
        zlink_assert (_socket_registry.empty ());
    }
    _slot_sync.unlock ();

    if (debug_env_enabled ("ZLINK_CTX_DEBUG_SOCKETS")) {
        std::fprintf (stderr, "[ctx] before-delete\n");
        std::fflush (stderr);
    }

    //  Context is API-created on heap; shutdown path owns final deletion once
    //  reaper confirms all sockets are gone.
    zlink::release_heap_owned (this);

    return 0;
}

int zlink::ctx_t::shutdown ()
{
    scoped_lock_t locker (_slot_sync);
    (void) ctx_termination_t::begin_shutdown_locked (*this, false);
    return 0;
}

bool zlink::ctx_t::start ()
{
    return ctx_bootstrap_t::start_runtime_locked (*this);
}

zlink::socket_base_t *zlink::ctx_t::create_socket (int type_)
{
    scoped_lock_t locker (_slot_sync);

    //  Once zlink_ctx_term() or zlink_ctx_shutdown() was called, we can't create
    //  new sockets.
    if (_terminating) {
        errno = ETERM;
        return NULL;
    }

    if (unlikely (_starting)) {
        if (!start ())
            return NULL;
    }

    //  If max_sockets limit was reached, return error.
    if (!_socket_registry.has_available_socket_slot ()) {
        errno = EMFILE;
        return NULL;
    }

    //  Choose a slot for the socket.
    const uint32_t slot = _socket_registry.claim_socket_slot ();

    //  Generate new unique socket ID.
    const int sid = (static_cast<int> (max_socket_id.add (1))) + 1;

    //  Create the socket and register its mailbox.
    socket_base_t *s = socket_base_t::create (type_, this, slot, sid);
    if (!s) {
        _socket_registry.release_unused_socket_slot (slot);
        return NULL;
    }
    _socket_registry.publish_socket (s);

    return s;
}

void zlink::ctx_t::destroy_socket (class socket_base_t *socket_)
{
    scoped_lock_t locker (_slot_sync);

    //  Free the associated thread slot.
    _socket_registry.remove_socket (socket_);
    debug_dump_sockets_locked ("destroy-socket");

    //  If zlink_ctx_term() was already called and there are no more socket
    //  we can ask reaper thread to terminate.
    if (_terminating && _socket_registry.empty ())
        _runtime_resources.stop_reaper ();
}

int zlink::ctx_t::wait_for_socket_removal (const socket_base_t *socket_, int timeout_ms_)
{
    if (!socket_)
        return 0;

    scoped_lock_t locker (_slot_sync);
    return _socket_registry.wait_for_socket_removal (&_slot_sync, socket_, timeout_ms_);
}

int zlink::ctx_t::close_socket_and_wait (socket_base_t *&socket_, int timeout_ms_)
{
    if (!socket_)
        return 0;

    socket_base_t *socket = socket_;
    socket->stop ();
    socket->close ();
    socket_ = NULL;
    return wait_for_socket_removal (socket, timeout_ms_);
}

size_t zlink::ctx_t::socket_count () const
{
    scoped_lock_t locker (const_cast<mutex_t &> (_slot_sync));
    return _socket_registry.socket_count ();
}

int zlink::ctx_t::wait_for_socket_count_at_most (size_t max_count_, int timeout_ms_)
{
    scoped_lock_t locker (_slot_sync);
    return _socket_registry.wait_for_socket_count_at_most (&_slot_sync, max_count_, timeout_ms_);
}

zlink::object_t *zlink::ctx_t::get_reaper () const
{
    return _runtime_resources.reaper_object ();
}

void zlink::ctx_t::send_command (uint32_t tid_, const command_t &command_)
{
    _socket_registry.mailbox (tid_)->send (command_);
    if (tid_ == term_tid)
        _term_mailbox.signal ();
}

zlink::io_thread_t *zlink::ctx_t::choose_io_thread (uint64_t affinity_)
{
    return _runtime_resources.choose_io_thread (affinity_);
}

zlink::io_thread_t *zlink::ctx_t::choose_io_thread_stream (uint64_t affinity_)
{
    return _runtime_resources.choose_io_thread_stream (affinity_);
}

int zlink::ctx_t::register_endpoint (const char *addr_, const endpoint_t &endpoint_)
{
    return _inproc_registry.register_endpoint (addr_, endpoint_);
}

int zlink::ctx_t::unregister_endpoint (const std::string &addr_, const socket_base_t *const socket_)
{
    return _inproc_registry.unregister_endpoint (addr_, socket_);
}

void zlink::ctx_t::unregister_endpoints (const socket_base_t *const socket_)
{
    _inproc_registry.unregister_endpoints (socket_);
}

zlink::endpoint_t zlink::ctx_t::find_endpoint (const char *addr_)
{
    return _inproc_registry.find_endpoint (addr_);
}

bool zlink::ctx_t::pend_connection (const std::string &addr_,
                                    const endpoint_t &endpoint_,
                                    pipe_t **pipes_)
{
    return _inproc_registry.pend_connection (addr_, endpoint_, pipes_);
}

void zlink::ctx_t::connect_pending (const char *addr_, zlink::socket_base_t *bind_socket_)
{
    _inproc_registry.connect_pending (addr_, bind_socket_);
}

//  The last used socket ID, or 0 if no socket was used so far. Note that this
//  is a global variable. Thus, even sockets created in different contexts have
//  unique IDs.
zlink::atomic_counter_t zlink::ctx_t::max_socket_id;
