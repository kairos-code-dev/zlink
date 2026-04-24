/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"
#include "utils/macros.hpp"
#ifndef ZLINK_HAVE_WINDOWS
#include <unistd.h>
#endif

#include <limits>
#include <climits>
#include <cstdlib>
#include <new>
#include <stdio.h>
#include <sstream>
#include <string.h>

#include "core/ctx.hpp"
#include "core/auto_hwm_policy.hpp"
#include "core/ctx_bootstrap.hpp"
#include "core/ctx_termination.hpp"
#include "sockets/socket_base.hpp"
#include "core/io_thread.hpp"
#include "core/reaper.hpp"
#include "core/pipe.hpp"
#include "services/control/service_control_runtime.hpp"
#include "utils/err.hpp"
#include "utils/random.hpp"

#ifdef ZLINK_USE_NSS
#include <nss.h>
#endif

#ifdef ZLINK_USE_GNUTLS
#include <gnutls/gnutls.h>
#endif

#define ZLINK_CTX_TAG_VALUE_GOOD 0xabadcafe
#define ZLINK_CTX_TAG_VALUE_BAD 0xdeadbeef

static int clipped_maxsocket (int max_requested_)
{
    if (max_requested_ >= zlink::poller_t::max_fds ()
        && zlink::poller_t::max_fds () != -1)
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
    _max_sockets (clipped_maxsocket (ZLINK_MAX_SOCKETS_DFLT)),
    _max_msgsz (INT_MAX),
    _io_thread_count (ZLINK_IO_THREADS_DFLT),
    _spot_worker_thread_count (ZLINK_SPOT_WORKER_THREADS_DFLT),
    _auto_hwm_enabled (ZLINK_CTX_AUTO_HWM_ENABLE_DFLT != 0),
    _auto_hwm_total_memory_budget_mb (
      ZLINK_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB_DFLT),
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

zlink::service_control_runtime_t *zlink::ctx_t::service_data_runtime ()
{
    if (!ctx_bootstrap_t::ensure_service_runtime (*this))
        return NULL;
    return _runtime_resources.service_data_runtime ();
}

zlink::service_control_runtime_t *zlink::ctx_t::service_data_runtime_for_key (
  uint32_t key_)
{
    if (!ctx_bootstrap_t::ensure_service_runtime (*this))
        return NULL;
    return _runtime_resources.service_data_runtime_for_key (key_);
}

zlink::service_control_runtime_t *zlink::ctx_t::spot_worker_runtime_for_key (
  uint32_t key_)
{
    if (!ctx_bootstrap_t::ensure_service_runtime (*this))
        return NULL;
    return _runtime_resources.spot_worker_runtime_for_key (key_);
}

void zlink::ctx_t::debug_dump_sockets_locked (const char *phase_) const
{
    if (!std::getenv ("ZLINK_CTX_DEBUG_SOCKETS"))
        return;

    std::vector<socket_base_t *> sockets;
    _socket_registry.collect_sockets (&sockets);
    fprintf (stderr, "[ctx] %s socket_count=%u\n", phase_ ? phase_ : "state",
             static_cast<unsigned> (sockets.size ()));
    for (std::vector<socket_base_t *>::size_type i = 0, size = sockets.size ();
         i != size; ++i) {
        const socket_base_t *socket = sockets[i];
        if (!socket)
            continue;

        fprintf (stderr, "[ctx]   socket[%u]=%p type=%s(%d) sid=%d\n",
                 static_cast<unsigned> (i), static_cast<const void *> (socket),
                 socket_type_name (socket->socket_type ()),
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

    //  Context is API-created on heap; shutdown path owns final deletion once
    //  reaper confirms all sockets are gone.
    delete this;

    return 0;
}

int zlink::ctx_t::shutdown ()
{
    scoped_lock_t locker (_slot_sync);
    (void) ctx_termination_t::begin_shutdown_locked (*this, false);
    return 0;
}

int zlink::ctx_t::set (int option_, const void *optval_, size_t optvallen_)
{
    const bool is_int = (optvallen_ == sizeof (int));
    int value = 0;
    if (is_int)
        memcpy (&value, optval_, sizeof (int));

    bool refresh_auto_hwm = false;

    switch (option_) {
        case ZLINK_MAX_SOCKETS:
            if (is_int && value >= 1 && value == clipped_maxsocket (value)) {
                scoped_lock_t locker (_opt_sync);
                _max_sockets = value;
                return 0;
            }
            break;

        case ZLINK_IO_THREADS:
            if (is_int && value >= 0) {
                scoped_lock_t locker (_opt_sync);
                _io_thread_count = value;
                return 0;
            }
            break;

        case ZLINK_SPOT_WORKER_THREADS:
            if (is_int && value >= 0) {
                scoped_lock_t runtime_lock (_slot_sync);
                if (!_starting) {
                    errno = EINVAL;
                    return -1;
                }
                scoped_lock_t locker (_opt_sync);
                _spot_worker_thread_count = value;
                return 0;
            }
            break;

        case ZLINK_CTX_OPT_AUTO_HWM_ENABLE:
            if (is_int && (value == 0 || value == 1)) {
                scoped_lock_t locker (_opt_sync);
                _auto_hwm_enabled = (value != 0);
                refresh_auto_hwm = true;
                break;
            }
            break;

        case ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB:
            if (is_int && value >= 1) {
                scoped_lock_t locker (_opt_sync);
                _auto_hwm_total_memory_budget_mb = value;
                refresh_auto_hwm = true;
                break;
            }
            break;

        case ZLINK_INTERNAL_OPT_IPV6:
            if (is_int && value >= 0) {
                scoped_lock_t locker (_opt_sync);
                _ipv6 = (value != 0);
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_BLOCKY:
        case ZLINK_CTX_OPT_BLOCKY:
            if (is_int && value >= 0) {
                scoped_lock_t locker (_opt_sync);
                _blocky = (value != 0);
                return 0;
            }
            break;

        case ZLINK_MAX_MSGSZ:
            if (is_int && value >= 0) {
                scoped_lock_t locker (_opt_sync);
                _max_msgsz = value < INT_MAX ? value : INT_MAX;
                return 0;
            }
            break;

        default: {
            return thread_ctx_t::set (option_, optval_, optvallen_);
        }
    }

    if (refresh_auto_hwm) {
        std::vector<socket_base_t *> sockets;
        scoped_lock_t runtime_lock (_slot_sync);
        _socket_registry.collect_sockets (&sockets);
        for (size_t i = 0; i < sockets.size (); ++i) {
            if (sockets[i])
                sockets[i]->refresh_auto_hwm_policy ();
        }
        return 0;
    }

    errno = EINVAL;
    return -1;
}

int zlink::ctx_t::get (int option_, void *optval_, const size_t *optvallen_)
{
    const bool is_int = (*optvallen_ == sizeof (int));
    int *value = static_cast<int *> (optval_);

    switch (option_) {
        case ZLINK_MAX_SOCKETS:
            if (is_int) {
                scoped_lock_t locker (_opt_sync);
                *value = _max_sockets;
                return 0;
            }
            break;

        case ZLINK_SOCKET_LIMIT:
            if (is_int) {
                *value = clipped_maxsocket (65535);
                return 0;
            }
            break;

        case ZLINK_IO_THREADS:
            if (is_int) {
                scoped_lock_t locker (_opt_sync);
                *value = _io_thread_count;
                return 0;
            }
            break;

        case ZLINK_SPOT_WORKER_THREADS:
            if (is_int) {
                scoped_lock_t locker (_opt_sync);
                *value = _spot_worker_thread_count;
                return 0;
            }
            break;

        case ZLINK_CTX_OPT_AUTO_HWM_ENABLE:
            if (is_int) {
                scoped_lock_t locker (_opt_sync);
                *value = _auto_hwm_enabled ? 1 : 0;
                return 0;
            }
            break;

        case ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB:
            if (is_int) {
                scoped_lock_t locker (_opt_sync);
                *value = _auto_hwm_total_memory_budget_mb;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_IPV6:
            if (is_int) {
                scoped_lock_t locker (_opt_sync);
                *value = _ipv6;
                return 0;
            }
            break;

        case ZLINK_INTERNAL_OPT_BLOCKY:
        case ZLINK_CTX_OPT_BLOCKY:
            if (is_int) {
                scoped_lock_t locker (_opt_sync);
                *value = _blocky;
                return 0;
            }
            break;

        case ZLINK_MAX_MSGSZ:
            if (is_int) {
                scoped_lock_t locker (_opt_sync);
                *value = _max_msgsz;
                return 0;
            }
            break;

        case ZLINK_MSG_T_SIZE:
            if (is_int) {
                scoped_lock_t locker (_opt_sync);
                *value = sizeof (zlink_msg_t);
                return 0;
            }
            break;

        default: {
            return thread_ctx_t::get (option_, optval_, optvallen_);
        }
    }

    errno = EINVAL;
    return -1;
}

int zlink::ctx_t::get (int option_)
{
    int optval = 0;
    size_t optvallen = sizeof (int);

    if (get (option_, &optval, &optvallen) == 0)
        return optval;

    errno = EINVAL;
    return -1;
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

int zlink::ctx_t::wait_for_socket_removal (const socket_base_t *socket_,
                                           int timeout_ms_)
{
    if (!socket_)
        return 0;

    scoped_lock_t locker (_slot_sync);
    return _socket_registry.wait_for_socket_removal (&_slot_sync, socket_,
                                                     timeout_ms_);
}

int zlink::ctx_t::close_socket_and_wait (socket_base_t *&socket_,
                                         int timeout_ms_)
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

int zlink::ctx_t::wait_for_socket_count_at_most (size_t max_count_,
                                                 int timeout_ms_)
{
    scoped_lock_t locker (_slot_sync);
    return _socket_registry.wait_for_socket_count_at_most (&_slot_sync,
                                                           max_count_,
                                                           timeout_ms_);
}

zlink::object_t *zlink::ctx_t::get_reaper () const
{
    return _runtime_resources.reaper_object ();
}

zlink::thread_ctx_t::thread_ctx_t () :
    _thread_priority (ZLINK_THREAD_PRIORITY_DFLT),
    _thread_sched_policy (ZLINK_THREAD_SCHED_POLICY_DFLT)
{
}

void zlink::thread_ctx_t::start_thread (thread_t &thread_,
                                      thread_fn *tfn_,
                                      void *arg_,
                                      const char *name_) const
{
    thread_.setSchedulingParameters (_thread_priority, _thread_sched_policy,
                                     _thread_affinity_cpus);

    char namebuf[16] = "";
    snprintf (namebuf, sizeof (namebuf), "%s%sZLINKbg%s%s",
              _thread_name_prefix.empty () ? "" : _thread_name_prefix.c_str (),
              _thread_name_prefix.empty () ? "" : "/", name_ ? "/" : "",
              name_ ? name_ : "");
    thread_.start (tfn_, arg_, namebuf);
}

int zlink::thread_ctx_t::set (int option_, const void *optval_, size_t optvallen_)
{
    const bool is_int = (optvallen_ == sizeof (int));
    int value = 0;
    if (is_int)
        memcpy (&value, optval_, sizeof (int));

    switch (option_) {
        case ZLINK_THREAD_SCHED_POLICY:
            if (is_int && value >= 0) {
                scoped_lock_t locker (_opt_sync);
                _thread_sched_policy = value;
                return 0;
            }
            break;

        case ZLINK_THREAD_AFFINITY_CPU_ADD:
            if (is_int && value >= 0) {
                scoped_lock_t locker (_opt_sync);
                _thread_affinity_cpus.insert (value);
                return 0;
            }
            break;

        case ZLINK_THREAD_AFFINITY_CPU_REMOVE:
            if (is_int && value >= 0) {
                scoped_lock_t locker (_opt_sync);
                if (0 == _thread_affinity_cpus.erase (value)) {
                    errno = EINVAL;
                    return -1;
                }
                return 0;
            }
            break;

        case ZLINK_THREAD_PRIORITY:
            if (is_int && value >= 0) {
                scoped_lock_t locker (_opt_sync);
                _thread_priority = value;
                return 0;
            }
            break;

        case ZLINK_THREAD_NAME_PREFIX:
            // start_thread() allows max 16 chars for thread name
            if (is_int) {
                std::ostringstream s;
                s << value;
                scoped_lock_t locker (_opt_sync);
                _thread_name_prefix = s.str ();
                return 0;
            } else if (optvallen_ > 0 && optvallen_ <= 16) {
                scoped_lock_t locker (_opt_sync);
                _thread_name_prefix.assign (static_cast<const char *> (optval_),
                                            optvallen_);
                return 0;
            }
            break;
    }

    errno = EINVAL;
    return -1;
}

int zlink::thread_ctx_t::get (int option_,
                            void *optval_,
                            const size_t *optvallen_)
{
    const bool is_int = (*optvallen_ == sizeof (int));
    int *value = static_cast<int *> (optval_);

    switch (option_) {
        case ZLINK_THREAD_SCHED_POLICY:
            if (is_int) {
                scoped_lock_t locker (_opt_sync);
                *value = _thread_sched_policy;
                return 0;
            }
            break;

        case ZLINK_THREAD_NAME_PREFIX:
            if (is_int) {
                scoped_lock_t locker (_opt_sync);
                *value = atoi (_thread_name_prefix.c_str ());
                return 0;
            } else if (*optvallen_ >= _thread_name_prefix.size ()) {
                scoped_lock_t locker (_opt_sync);
                memcpy (optval_, _thread_name_prefix.data (),
                        _thread_name_prefix.size ());
                return 0;
            }
            break;
    }

    errno = EINVAL;
    return -1;
}

void zlink::ctx_t::send_command (uint32_t tid_, const command_t &command_)
{
    _socket_registry.mailbox (tid_)->send (command_);
}

zlink::io_thread_t *zlink::ctx_t::choose_io_thread (uint64_t affinity_)
{
    return _runtime_resources.choose_io_thread (affinity_);
}

zlink::io_thread_t *zlink::ctx_t::choose_io_thread_stream (uint64_t affinity_)
{
    return _runtime_resources.choose_io_thread_stream (affinity_);
}

int zlink::ctx_t::register_endpoint (const char *addr_,
                                     const endpoint_t &endpoint_)
{
    return _inproc_registry.register_endpoint (addr_, endpoint_);
}

int zlink::ctx_t::unregister_endpoint (const std::string &addr_,
                                       const socket_base_t *const socket_)
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

void zlink::ctx_t::connect_pending (const char *addr_,
                                    zlink::socket_base_t *bind_socket_)
{
    _inproc_registry.connect_pending (addr_, bind_socket_);
}

//  The last used socket ID, or 0 if no socket was used so far. Note that this
//  is a global variable. Thus, even sockets created in different contexts have
//  unique IDs.
zlink::atomic_counter_t zlink::ctx_t::max_socket_id;
